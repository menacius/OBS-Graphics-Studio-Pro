#include "title-audio-runtime.h"
#include "audio-transport-direction.h"
#include "performance-counters.h"
#include "title-logger.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <sys/prctl.h>
#endif

#include <util/platform.h>
#include <algorithm>
#include <cstdarg>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <filesystem>
#include <unordered_map>

#if defined(BGL_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace bgl::audio {
namespace {
constexpr double kEpsilon = 0.000001;

static void audio_log(TitleLogLevel level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const QString message = QString::vasprintf(format, args);
    va_end(args);
    TitleLogger::log(level, "Audio", message);
}

static void set_audio_output_thread_name() noexcept
{
#if defined(_WIN32)
    using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    const HMODULE kernel32 = GetModuleHandleW(L"Kernel32.dll");
    if (!kernel32)
        return;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#endif
    const auto set_thread_description = reinterpret_cast<SetThreadDescriptionFn>(
        GetProcAddress(kernel32, "SetThreadDescription"));
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (set_thread_description)
        (void)set_thread_description(GetCurrentThread(), L"BGL Audio Output");
#elif defined(__APPLE__)
    (void)pthread_setname_np("bgl-audio-out");
#elif defined(__linux__)
    (void)prctl(PR_SET_NAME, "bgl-audio-out", 0, 0, 0);
#endif
}

static uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
static uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static float clamp_sample(float value) { return std::clamp(value, -1.0f, 1.0f); }
static float db_to_linear(float db) { return std::pow(10.0f, db / 20.0f); }
static float fade_shape(double x, AudioFadeCurve curve)
{
    x = std::clamp(x, 0.0, 1.0);
    switch (curve) {
    case AudioFadeCurve::Smooth: return float(x * x * (3.0 - 2.0 * x));
    case AudioFadeCurve::EqualPower: return std::sin(float(x) * 1.57079632679f);
    default: return float(x);
    }
}
static std::string asset_cache_key(const std::string &path, int stream, uint32_t rate)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto stamp = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
    return path + "|" + std::to_string(stream) + "|" + std::to_string(rate) + "|" +
           std::to_string(size) + "|" + std::to_string(stamp);
}

}

struct SourceAudioRuntime::ClipSpec {
    std::string id;
    std::string path;
    int stream_index = -1;
    double timeline_in = 0.0;
    double timeline_out = 0.0;
    double media_in = 0.0;
    double media_out = 0.0;
    float volume = 1.0f;
    float pan = 0.0f;
    AnimatedProperty volume_prop{"audio_volume", 1.0};
    AnimatedProperty pan_prop{"audio_pan", 0.0};
    bool muted = false;
    bool solo = false;
    double fade_in = 0.0;
    double fade_out = 0.0;
    AudioFadeCurve fade_curve = AudioFadeCurve::Linear;
    std::vector<AudioEffect> effects;
    bool loop = false;
    bool independent = false;
    bool hidden = false;
};

struct DecodedAudioAsset {
    std::vector<float> left;
    std::vector<float> right;
    uint32_t sample_rate = 48000;
    std::vector<float> waveform;
    bool valid = false;
};

namespace {
std::mutex &decoded_asset_cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::weak_ptr<const DecodedAudioAsset>> &decoded_asset_cache()
{
    static std::unordered_map<std::string, std::weak_ptr<const DecodedAudioAsset>> cache;
    return cache;
}

void prune_decoded_asset_cache_locked()
{
    auto &cache = decoded_asset_cache();
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->second.expired())
            it = cache.erase(it);
        else
            ++it;
    }
}
} // namespace

struct SourceAudioRuntime::DecodedClip {
    ClipSpec spec;
    std::shared_ptr<const DecodedAudioAsset> asset;
    float hp_l = 0.0f, hp_r = 0.0f, hp_prev_l = 0.0f, hp_prev_r = 0.0f;
    float lp_l = 0.0f, lp_r = 0.0f;
    float compressor_envelope = 0.0f;
    float smooth_gain_l = 0.0f, smooth_gain_r = 0.0f;
};

SourceAudioRuntime::SourceAudioRuntime(obs_source_t *source,
                                       bool editor_preview)
    : source_(source), editor_preview_(editor_preview)
{
    const char *name = source_ ? obs_source_get_name(source_) : nullptr;
    source_name_ = name && *name ? name : "unnamed";
    if (audio_t *audio = obs_get_audio()) {
        const uint32_t configured = audio_output_get_sample_rate(audio);
        if (configured >= 8000 && configured <= 384000)
            sample_rate_ = configured;
    }
    block_frames_ = std::max<uint32_t>(64, sample_rate_ / 100);
    audio_log(TitleLogLevel::Info,
         "[BGL Audio][runtime-create] source='%s' editor_preview=%s "
         "sample_rate=%u block_frames=%u delivery=%s",
         source_name_.c_str(), editor_preview_ ? "true" : "false",
         sample_rate_, block_frames_,
         editor_preview_ ? "realtime-monitor-worker" : "background-worker");
    worker_ = std::thread([this] { worker_main(); });
    output_worker_ = std::thread([this] { output_worker_main(); });
}

SourceAudioRuntime::~SourceAudioRuntime()
{
    bool editor_preview = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        editor_preview = editor_preview_;
    }
    audio_log(TitleLogLevel::Info,
         "[BGL Audio][runtime-destroy] source='%s' editor_preview=%s "
         "blocks=%llu frames=%llu transport_updates=%llu "
         "discontinuities=%llu late_repairs=%llu",
         source_name_.c_str(), editor_preview ? "true" : "false",
         static_cast<unsigned long long>(
             log_output_blocks_.load(std::memory_order_acquire)),
         static_cast<unsigned long long>(
             log_output_frames_.load(std::memory_order_acquire)),
         static_cast<unsigned long long>(
             log_transport_updates_.load(std::memory_order_acquire)),
         static_cast<unsigned long long>(
             log_discontinuities_.load(std::memory_order_acquire)),
         static_cast<unsigned long long>(
             log_late_repairs_.load(std::memory_order_acquire)));
    quit_.store(true, std::memory_order_release);
    decode_epoch_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rebuild_pending_ = true;
        requested_specs_.clear();
    }
    cv_.notify_all();
    output_cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (output_worker_.joinable())
        output_worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    clips_.clear();
    pending_waveforms_.clear();
}

void SourceAudioRuntime::clear_shared_cache()
{
    std::lock_guard<std::mutex> lock(decoded_asset_cache_mutex());
    decoded_asset_cache().clear();
}

std::size_t SourceAudioRuntime::shared_cache_entry_count()
{
    std::lock_guard<std::mutex> lock(decoded_asset_cache_mutex());
    prune_decoded_asset_cache_locked();
    return decoded_asset_cache().size();
}

void SourceAudioRuntime::set_editor_preview(bool editor_preview)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (editor_preview_ != editor_preview) {
            editor_preview_ = editor_preview;
            discontinuity_ = true;
            editor_monitor_cadence_.clear();
            output_scheduler_.clear();
            log_last_packet_timestamp_ns_.store(0, std::memory_order_release);
            log_last_delivery_ns_.store(0, std::memory_order_release);
            changed = true;
        }
    }
    if (changed) {
        audio_log(TitleLogLevel::Info,
             "[BGL Audio][delivery-mode] source='%s' editor_preview=%s "
             "delivery=%s",
             source_name_.c_str(), editor_preview ? "true" : "false",
             editor_preview ? "realtime-monitor-worker" : "background-worker");
        output_cv_.notify_all();
    }
}

bool SourceAudioRuntime::decode_cancelled(uint64_t decode_epoch) const
{
    return quit_.load(std::memory_order_acquire) ||
           decode_epoch_.load(std::memory_order_acquire) != decode_epoch;
}

void SourceAudioRuntime::request_rebuild(const std::shared_ptr<Title> &title, uint64_t revision)
{
    decode_epoch_.fetch_add(1, std::memory_order_acq_rel);
    std::vector<ClipSpec> specs;
    int64_t duration_ms = 0;
    if (title) {
        duration_ms = static_cast<int64_t>(std::llround(std::max(0.0, title->duration) * 1000.0));
        for (const auto &ptr : title->layers) {
            if (!ptr || ptr->type != LayerType::Audio) continue;
            const Layer &l = *ptr;
            ClipSpec s;
            s.id = l.id; s.path = l.audio_source; s.stream_index = l.audio_stream_index;
            s.timeline_in = std::max(0.0, l.in_time); s.timeline_out = std::max(s.timeline_in, l.out_time);
            s.media_in = std::max(0.0, l.audio_in_point); s.media_out = std::max(0.0, l.audio_out_point);
            s.volume = std::clamp(l.audio_volume, 0.0f, 4.0f);
            s.pan = std::clamp(l.audio_pan, -1.0f, 1.0f);
            s.volume_prop = l.audio_volume_prop;
            s.pan_prop = l.audio_pan_prop;
            bool ancestor_audio_muted = false;
            std::set<std::string> visited_parents;
            std::string parent_id = l.parent_id;
            while (!parent_id.empty() && visited_parents.insert(parent_id).second) {
                const auto parent = title->find_layer(parent_id);
                if (!parent) break;
                ancestor_audio_muted = ancestor_audio_muted || parent->audio_muted;
                parent_id = parent->parent_id;
            }
            s.muted = l.audio_muted || ancestor_audio_muted; s.solo = l.audio_solo;
            s.fade_in = std::max(0.0, l.audio_fade_in); s.fade_out = std::max(0.0, l.audio_fade_out);
            s.fade_curve = l.audio_fade_curve; s.effects = l.audio_effects;
            s.loop = l.audio_loop || l.audio_playback_mode == AudioPlaybackMode::Loop;
            s.independent = l.audio_independent; s.hidden = !l.visible;
            specs.push_back(std::move(s));
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_specs_ = std::move(specs);
        requested_generation_ = revision;
        duration_ms_ = duration_ms;
        rebuild_pending_ = true;
    }
    cv_.notify_one();
}

void SourceAudioRuntime::bind_title(const std::shared_ptr<Title> &title, uint64_t /*revision*/)
{
    /* Publish decoder-generated waveforms back to the document model. */
    if (title) {
        std::unordered_map<std::string, PendingWaveform> waveforms;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            waveforms.swap(pending_waveforms_);
        }
        for (const auto &entry : waveforms) {
            auto layer = title->find_layer(entry.first);
            if (layer && layer->type == LayerType::Audio && !entry.second.peaks.empty()) {
                layer->audio_waveform = entry.second.peaks;
                layer->audio_waveform_duration = entry.second.duration;
            }
        }
    }

    std::vector<ClipSpec> live_specs;
    int64_t duration_ms = 0;
    uint64_t asset_signature = 1469598103934665603ULL;
    auto mix_hash = [&asset_signature](const void *bytes, size_t size) {
        const auto *p = static_cast<const uint8_t *>(bytes);
        for (size_t i = 0; i < size; ++i) {
            asset_signature ^= p[i];
            asset_signature *= 1099511628211ULL;
        }
    };

    if (title) {
        duration_ms = static_cast<int64_t>(std::llround(std::max(0.0, title->duration) * 1000.0));
        for (const auto &ptr : title->layers) {
            if (!ptr || ptr->type != LayerType::Audio) continue;
            const Layer &l = *ptr;
            ClipSpec spec;
            spec.id = l.id; spec.path = l.audio_source; spec.stream_index = l.audio_stream_index;
            spec.timeline_in = std::max(0.0, l.in_time); spec.timeline_out = std::max(spec.timeline_in, l.out_time);
            spec.media_in = std::max(0.0, l.audio_in_point); spec.media_out = std::max(0.0, l.audio_out_point);
            spec.volume = std::clamp(l.audio_volume, 0.0f, 4.0f);
            spec.pan = std::clamp(l.audio_pan, -1.0f, 1.0f);
            spec.volume_prop = l.audio_volume_prop;
            spec.pan_prop = l.audio_pan_prop;
            bool ancestor_audio_muted = false;
            std::set<std::string> visited_parents;
            std::string parent_id = l.parent_id;
            while (!parent_id.empty() && visited_parents.insert(parent_id).second) {
                const auto parent = title->find_layer(parent_id);
                if (!parent) break;
                ancestor_audio_muted = ancestor_audio_muted || parent->audio_muted;
                parent_id = parent->parent_id;
            }
            spec.muted = l.audio_muted || ancestor_audio_muted; spec.solo = l.audio_solo;
            spec.fade_in = std::max(0.0, l.audio_fade_in); spec.fade_out = std::max(0.0, l.audio_fade_out);
            spec.fade_curve = l.audio_fade_curve; spec.effects = l.audio_effects;
            spec.loop = l.audio_loop || l.audio_playback_mode == AudioPlaybackMode::Loop;
            spec.independent = l.audio_independent; spec.hidden = !l.visible;
            live_specs.push_back(spec);

            /* Decode identity only.  Timeline trim, gain, pan, mute, fades and
             * DSP are live mixer parameters and must never reopen the asset. */
            mix_hash(l.id.data(), l.id.size());
            mix_hash(l.audio_source.data(), l.audio_source.size());
            mix_hash(&l.audio_stream_index, sizeof(l.audio_stream_index));
        }
    }

    bool rebuild = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        duration_ms_ = duration_ms;
        if (requested_generation_ != asset_signature || rebuild_pending_) {
            rebuild = requested_generation_ != asset_signature;
        } else {
            std::unordered_map<std::string, ClipSpec> by_id;
            for (auto &spec : live_specs) by_id.emplace(spec.id, std::move(spec));
            for (const auto &clip : clips_) {
                if (!clip) continue;
                auto it = by_id.find(clip->spec.id);
                if (it != by_id.end()) clip->spec = it->second;
            }
        }
    }
    if (rebuild)
        request_rebuild(title, asset_signature);
}

void SourceAudioRuntime::transport(double title_seconds, bool playing,
                                   bool visible, bool discontinuity, bool reverse)
{
    bool log_transition = false;
    bool log_jump = false;
    bool editor_preview = false;
    double logged_time = 0.0;
    bool logged_playing = false;
    bool logged_visible = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const double clamped_time = std::max(0.0, title_seconds);
        const bool play_changed = playing != playing_;
        const bool visibility_changed = visible != visible_;
        const bool direction_changed = reverse != reverse_;
        const bool jumped = discontinuity || direction_changed ||
            std::abs(clamped_time - previous_title_time_) > 0.250;

        title_time_ = clamped_time;
        reverse_ = reverse;
        if (jumped || play_changed || visibility_changed) {
            discontinuity_ = true;
            output_sample_cursor_ = transport_sample_cursor(
                title_time_, sample_rate_, reverse_);
            editor_monitor_cadence_.clear();
            output_scheduler_.clear();
            log_last_packet_timestamp_ns_.store(0, std::memory_order_release);
            log_last_delivery_ns_.store(0, std::memory_order_release);
            ++log_discontinuities_;
            if (jumped) {
                for (const auto &clip : clips_) {
                    if (!clip)
                        continue;
                    clip->hp_l = clip->hp_r = clip->hp_prev_l =
                        clip->hp_prev_r = 0.0f;
                    clip->lp_l = clip->lp_r =
                        clip->compressor_envelope = 0.0f;
                    clip->smooth_gain_l = clip->smooth_gain_r = 0.0f;
                }
            }
            log_transition = true;
            log_jump = jumped;
        }
        playing_ = playing;
        visible_ = visible;
        previous_title_time_ = title_time_;
        ++log_transport_updates_;
        logged_time = title_time_;
        logged_playing = playing_;
        logged_visible = visible_;
        editor_preview = editor_preview_;
        reported_time_ms_.store(
            static_cast<int64_t>(std::llround(title_time_ * 1000.0)),
            std::memory_order_release);
        state_.store(!visible ? OBS_MEDIA_STATE_STOPPED
                             : (playing ? OBS_MEDIA_STATE_PLAYING
                                        : OBS_MEDIA_STATE_PAUSED),
                     std::memory_order_release);
    }
    if (log_transition) {
        audio_log(TitleLogLevel::Info,
             "[BGL Audio][transport] source='%s' editor_preview=%s "
             "time=%.3f playing=%s visible=%s reverse=%s discontinuity=%s",
             source_name_.c_str(), editor_preview ? "true" : "false",
             logged_time, logged_playing ? "true" : "false",
             logged_visible ? "true" : "false",
             reverse ? "true" : "false",
             log_jump ? "true" : "false");
    }
    if (log_transition)
        output_cv_.notify_all();
}

void SourceAudioRuntime::stop(bool)
{
    bool editor_preview = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ = false;
        visible_ = false;
        discontinuity_ = true;
        output_sample_cursor_ = 0;
        independent_time_ = 0.0;
        independent_sample_cursor_ = 0;
        editor_monitor_cadence_.clear();
        output_scheduler_.clear();
        log_last_packet_timestamp_ns_.store(0, std::memory_order_release);
        log_last_delivery_ns_.store(0, std::memory_order_release);
        reported_time_ms_.store(0, std::memory_order_release);
        state_.store(OBS_MEDIA_STATE_STOPPED, std::memory_order_release);
        editor_preview = editor_preview_;
    }
    audio_log(TitleLogLevel::Info, "[BGL Audio][stop] source='%s' editor_preview=%s",
         source_name_.c_str(), editor_preview ? "true" : "false");
    output_cv_.notify_one();
}

void SourceAudioRuntime::worker_main()
{
    for (;;) {
        std::vector<ClipSpec> specs;
        uint64_t generation = 0;
        uint64_t decode_epoch = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return quit_.load(std::memory_order_acquire) || rebuild_pending_;
            });
            if (quit_.load(std::memory_order_acquire))
                return;
            rebuild_pending_ = false;
            specs = requested_specs_;
            generation = requested_generation_;
            decode_epoch = decode_epoch_.load(std::memory_order_acquire);
        }
        std::vector<std::shared_ptr<DecodedClip>> decoded;
        decoded.reserve(specs.size());
        for (const auto &spec : specs) {
            if (decode_cancelled(decode_epoch))
                break;
            audio_log(TitleLogLevel::Info,
                 "[BGL Audio][decode-start] source='%s' clip='%s' "
                 "stream=%d path='%s' epoch=%llu",
                 source_name_.c_str(), spec.id.c_str(), spec.stream_index,
                 spec.path.c_str(),
                 static_cast<unsigned long long>(decode_epoch));
            bgl::perf::BackgroundJobScope job_scope;
            auto clip = decode_clip(spec, sample_rate_, decode_epoch);
            if (decode_cancelled(decode_epoch)) {
                audio_log(TitleLogLevel::Info,
                     "[BGL Audio][decode-cancelled] source='%s' clip='%s' "
                     "epoch=%llu",
                     source_name_.c_str(), spec.id.c_str(),
                     static_cast<unsigned long long>(decode_epoch));
                break;
            }
            if (clip && clip->asset && clip->asset->valid) {
                audio_log(TitleLogLevel::Info,
                     "[BGL Audio][decode-ready] source='%s' clip='%s' "
                     "frames=%llu duration_ms=%.1f sample_rate=%u",
                     source_name_.c_str(), spec.id.c_str(),
                     static_cast<unsigned long long>(clip->asset->left.size()),
                     1000.0 * double(clip->asset->left.size()) /
                         std::max<uint32_t>(1, clip->asset->sample_rate),
                     clip->asset->sample_rate);
            } else {
                audio_log(TitleLogLevel::Warning,
                     "[BGL Audio][decode-failed] source='%s' clip='%s' "
                     "path='%s'",
                     source_name_.c_str(), spec.id.c_str(), spec.path.c_str());
            }
            decoded.push_back(std::move(clip));
        }
        publish_decoded(generation, decode_epoch, std::move(decoded));
    }
}

void SourceAudioRuntime::publish_decoded(
    uint64_t generation, uint64_t decode_epoch,
    std::vector<std::shared_ptr<DecodedClip>> decoded)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quit_.load(std::memory_order_acquire) ||
            decode_epoch_.load(std::memory_order_acquire) != decode_epoch ||
            generation != requested_generation_)
            return;
        pending_waveforms_.clear();
        for (const auto &clip : decoded) {
            if (!clip || !clip->asset || !clip->asset->valid ||
                clip->spec.id.empty() || clip->asset->waveform.empty())
                continue;
            pending_waveforms_[clip->spec.id] = PendingWaveform{
                clip->asset->waveform,
                double(clip->asset->left.size()) /
                    std::max<uint32_t>(1, clip->asset->sample_rate)};
        }
        clips_ = std::move(decoded);
        published_generation_ = generation;
        discontinuity_ = true;
    }
    output_cv_.notify_one();
}

std::shared_ptr<SourceAudioRuntime::DecodedClip>
SourceAudioRuntime::decode_clip(const ClipSpec &spec, uint32_t target_rate,
                                uint64_t decode_epoch)
{
    auto out = std::make_shared<DecodedClip>();
    out->spec = spec;
    if (spec.path.empty() || decode_cancelled(decode_epoch))
        return out;

    const std::string key = asset_cache_key(
        spec.path, spec.stream_index, target_rate);
    {
        std::lock_guard<std::mutex> lock(decoded_asset_cache_mutex());
        auto &cache = decoded_asset_cache();
        const auto it = cache.find(key);
        if (it != cache.end()) {
            if (auto asset = it->second.lock()) {
                out->asset = std::move(asset);
                bgl::perf::add(bgl::perf::Counter::AudioCacheHits);
                return out;
            }
            cache.erase(it);
        }
    }
    bgl::perf::add(bgl::perf::Counter::AudioCacheMisses);

    auto asset = std::make_shared<DecodedAudioAsset>();
    asset->sample_rate = target_rate;
    auto cancelled = [&]() {
        if (!decode_cancelled(decode_epoch))
            return false;
        bgl::perf::add(bgl::perf::Counter::AudioDecodeCancelled);
        return true;
    };
    auto finalize = [&]() {
        if (!asset->valid || cancelled())
            return;
        constexpr size_t buckets = 1024;
        const size_t step = std::max<size_t>(
            1, asset->left.size() / buckets);
        asset->waveform.clear();
        asset->waveform.reserve(buckets * 2);
        for (size_t base = 0; base < asset->left.size(); base += step) {
            if ((base & 0x3ffffU) == 0 && cancelled())
                return;
            float lo = 1.0f;
            float hi = -1.0f;
            const size_t stop = std::min(asset->left.size(), base + step);
            for (size_t index = base; index < stop; ++index) {
                const float value = 0.5f *
                    (asset->left[index] + asset->right[index]);
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
            asset->waveform.push_back(lo);
            asset->waveform.push_back(hi);
        }
        if (cancelled())
            return;
        {
            std::lock_guard<std::mutex> lock(decoded_asset_cache_mutex());
            auto &cache = decoded_asset_cache();
            if (cache.size() > 512)
                prune_decoded_asset_cache_locked();
            cache[key] = asset;
        }
        out->asset = asset;
        bgl::perf::add(bgl::perf::Counter::AudioDecodedSamples,
                       asset->left.size());
    };

#if defined(BGL_HAVE_FFMPEG)
    struct DecodeInterrupt {
        const std::atomic_bool *quit = nullptr;
        const std::atomic<uint64_t> *epoch = nullptr;
        uint64_t expected_epoch = 0;
    } interrupt{&quit_, &decode_epoch_, decode_epoch};

    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt)
        return out;
    fmt->interrupt_callback.callback = [](void *opaque) -> int {
        const auto *state = static_cast<const DecodeInterrupt *>(opaque);
        return state->quit->load(std::memory_order_acquire) ||
                       state->epoch->load(std::memory_order_acquire) !=
                           state->expected_epoch
                   ? 1
                   : 0;
    };
    fmt->interrupt_callback.opaque = &interrupt;
    if (avformat_open_input(&fmt, spec.path.c_str(), nullptr, nullptr) < 0) {
        if (fmt)
            avformat_close_input(&fmt);
        return out;
    }
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> fmt_guard(
        fmt, [](AVFormatContext *context) { avformat_close_input(&context); });
    if (cancelled() || avformat_find_stream_info(fmt, nullptr) < 0)
        return out;
    const int stream = spec.stream_index >= 0
        ? spec.stream_index
        : av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream < 0 || stream >= static_cast<int>(fmt->nb_streams))
        return out;
    AVStream *st = fmt->streams[stream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec)
        return out;
    AVCodecContext *cc = avcodec_alloc_context3(codec);
    if (!cc)
        return out;
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> cc_guard(
        cc, [](AVCodecContext *context) { avcodec_free_context(&context); });
    if (avcodec_parameters_to_context(cc, st->codecpar) < 0 ||
        avcodec_open2(cc, codec, nullptr) < 0)
        return out;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext *swr = nullptr;
    const int swr_alloc_result = swr_alloc_set_opts2(
        &swr, &stereo, AV_SAMPLE_FMT_FLTP, target_rate,
        &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr);
    if (swr_alloc_result < 0 || !swr) {
        if (swr)
            swr_free(&swr);
        return out;
    }
    std::unique_ptr<SwrContext, void (*)(SwrContext *)> swr_guard(
        swr, [](SwrContext *context) { swr_free(&context); });
    if (swr_init(swr) < 0)
        return out;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return out;
    }
    std::vector<float> converted_left;
    std::vector<float> converted_right;
    while (!cancelled() && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == stream &&
            avcodec_send_packet(cc, pkt) >= 0) {
            while (!cancelled() && avcodec_receive_frame(cc, frame) >= 0) {
                const int capacity = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(swr, cc->sample_rate) + frame->nb_samples,
                    target_rate, cc->sample_rate, AV_ROUND_UP));
                converted_left.resize(std::max(0, capacity));
                converted_right.resize(std::max(0, capacity));
                uint8_t *planes[2] = {
                    reinterpret_cast<uint8_t *>(converted_left.data()),
                    reinterpret_cast<uint8_t *>(converted_right.data())};
                const int count = swr_convert(
                    swr, planes, capacity,
                    const_cast<const uint8_t **>(frame->extended_data),
                    frame->nb_samples);
                if (count > 0) {
                    asset->left.insert(asset->left.end(),
                                       converted_left.begin(),
                                       converted_left.begin() + count);
                    asset->right.insert(asset->right.end(),
                                        converted_right.begin(),
                                        converted_right.begin() + count);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    if (cancelled())
        return out;
    asset->valid = !asset->left.empty();
    finalize();
    return out;
#else
    // Dependency-free PCM WAV fallback. Other formats become available
    // automatically when FFmpeg development libraries are found.
    std::ifstream file(spec.path, std::ios::binary);
    if (!file || cancelled())
        return out;
    std::array<uint8_t, 12> riff{};
    file.read(reinterpret_cast<char *>(riff.data()), riff.size());
    if (!file || std::memcmp(riff.data(), "RIFF", 4) ||
        std::memcmp(riff.data() + 8, "WAVE", 4))
        return out;
    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> pcm;
    while (file && !cancelled()) {
        std::array<uint8_t, 8> header{};
        file.read(reinterpret_cast<char *>(header.data()), header.size());
        if (!file)
            break;
        const uint32_t size = le32(header.data() + 4);
        std::vector<uint8_t> chunk(size);
        file.read(reinterpret_cast<char *>(chunk.data()), size);
        if (size & 1)
            file.get();
        if (!std::memcmp(header.data(), "fmt ", 4) && size >= 16) {
            format = le16(chunk.data());
            channels = le16(chunk.data() + 2);
            rate = le32(chunk.data() + 4);
            bits = le16(chunk.data() + 14);
        } else if (!std::memcmp(header.data(), "data", 4)) {
            pcm = std::move(chunk);
        }
    }
    if (cancelled() || (format != 1 && format != 3) || channels < 1 ||
        rate == 0 || pcm.empty())
        return out;
    const size_t bytes = bits / 8;
    if (!bytes)
        return out;
    const size_t frames = pcm.size() / (bytes * channels);
    std::vector<float> source_left(frames);
    std::vector<float> source_right(frames);
    auto read_one = [&](const uint8_t *sample) -> float {
        if (format == 3 && bits == 32) {
            float value;
            std::memcpy(&value, sample, 4);
            return clamp_sample(value);
        }
        if (bits == 16)
            return float(int16_t(le16(sample))) / 32768.0f;
        if (bits == 24) {
            int32_t value = int32_t(sample[0]) |
                (int32_t(sample[1]) << 8) | (int32_t(sample[2]) << 16);
            if (value & 0x800000)
                value |= ~0xffffff;
            return float(value) / 8388608.0f;
        }
        if (bits == 32)
            return float(int32_t(le32(sample))) / 2147483648.0f;
        if (bits == 8)
            return (float(*sample) - 128.0f) / 128.0f;
        return 0.0f;
    };
    for (size_t index = 0; index < frames; ++index) {
        if ((index & 0x3fffU) == 0 && cancelled())
            return out;
        const uint8_t *sample = pcm.data() + index * bytes * channels;
        source_left[index] = read_one(sample);
        source_right[index] = channels > 1 ? read_one(sample + bytes)
                                           : source_left[index];
    }
    const size_t destination_frames = static_cast<size_t>(
        std::ceil(double(frames) * target_rate / rate));
    asset->left.resize(destination_frames);
    asset->right.resize(destination_frames);
    for (size_t index = 0; index < destination_frames; ++index) {
        if ((index & 0x3fffU) == 0 && cancelled())
            return out;
        const double position = double(index) * rate / target_rate;
        const size_t a = std::min<size_t>(
            static_cast<size_t>(position), frames - 1);
        const size_t b = std::min(a + 1, frames - 1);
        const float progress = float(position - a);
        asset->left[index] = source_left[a] +
            (source_left[b] - source_left[a]) * progress;
        asset->right[index] = source_right[a] +
            (source_right[b] - source_right[a]) * progress;
    }
    asset->valid = !asset->left.empty();
    finalize();
    return out;
#endif
}

void SourceAudioRuntime::mix_block(int64_t start_sample, uint32_t frames, bool reverse, std::vector<float> &left, std::vector<float> &right)
{
    bgl::perf::ScopedTimer mix_timer(bgl::perf::Counter::AudioMixNanoseconds);
    bgl::perf::add(bgl::perf::Counter::AudioMixBlocks);
    left.assign(frames, 0.0f);
    right.assign(frames, 0.0f);
    const bool any_solo = std::any_of(clips_.begin(), clips_.end(), [](const auto &clip) {
        return clip && clip->asset && clip->asset->valid && clip->spec.solo &&
               !clip->spec.muted && !clip->spec.hidden;
    });
    for (const auto &clip : clips_) {
        if (!clip || !clip->asset || !clip->asset->valid || clip->spec.muted ||
            clip->spec.hidden || (any_solo && !clip->spec.solo))
            continue;
        const auto &asset = *clip->asset;
        const auto &s = clip->spec;
        const uint64_t media_in = uint64_t(std::llround(s.media_in * sample_rate_));
        uint64_t media_out = s.media_out > 0
            ? uint64_t(std::llround(s.media_out * sample_rate_))
            : asset.left.size();
        media_out = std::min<uint64_t>(media_out, asset.left.size());
        if (media_out <= media_in)
            continue;
        const uint64_t span = media_out - media_in;
        const uint64_t timeline_in = uint64_t(std::llround(s.timeline_in * sample_rate_));
        const uint64_t timeline_out = uint64_t(std::llround(s.timeline_out * sample_rate_));
        float base_gain = 1.0f;
        for (const auto &fx : s.effects) if (fx.enabled && fx.type == AudioEffectType::Gain) base_gain *= db_to_linear(fx.gain_db);
        const float smooth = 1.0f - std::exp(-1.0f / (0.005f * float(sample_rate_)));
        for (uint32_t i = 0; i < frames; ++i) {
            const bool editor_reverse_independent =
                reverse && editor_preview_ && s.independent;
            const int64_t clock =
                (s.independent && !editor_reverse_independent)
                    ? static_cast<int64_t>(independent_sample_cursor_ + i)
                    : transport_sample_at(start_sample, i, reverse);
            if (clock < 0)
                continue;
            const uint64_t unsigned_clock = static_cast<uint64_t>(clock);
            if ((!s.independent || editor_reverse_independent) &&
                (unsigned_clock < timeline_in || unsigned_clock >= timeline_out))
                continue;
            const uint64_t rel =
                (s.independent && !editor_reverse_independent)
                    ? unsigned_clock
                    : unsigned_clock - timeline_in;
            if (!s.loop && rel >= span) continue;
            const uint64_t idx = media_in + (s.loop ? (rel % span) : rel);
            if (idx >= asset.left.size())
                continue;
            const double sec = double(rel) / sample_rate_;
            const double clip_len = double(std::min<uint64_t>(span, timeline_out > timeline_in ? timeline_out - timeline_in : span)) / sample_rate_;
            const float automated_gain = std::clamp(
                static_cast<float>(s.volume_prop.evaluate(sec)), 0.0f, 4.0f);
            const float automated_pan = std::clamp(
                static_cast<float>(s.pan_prop.evaluate(sec)), -1.0f, 1.0f);
            const float target_l = base_gain * automated_gain *
                std::sqrt(0.5f * (1.0f - automated_pan));
            const float target_r = base_gain * automated_gain *
                std::sqrt(0.5f * (1.0f + automated_pan));
            float envelope = 1.0f;
            if (s.fade_in > kEpsilon) envelope *= fade_shape(sec / s.fade_in, s.fade_curve);
            if (s.fade_out > kEpsilon) envelope *= fade_shape((clip_len - sec) / s.fade_out, s.fade_curve);
            for (const auto &fx : s.effects) if (fx.enabled && fx.type == AudioEffectType::Fade) {
                if (fx.fade_in > kEpsilon) envelope *= fade_shape(sec / fx.fade_in, fx.fade_curve);
                if (fx.fade_out > kEpsilon) envelope *= fade_shape((clip_len - sec) / fx.fade_out, fx.fade_curve);
            }
            clip->smooth_gain_l += (target_l * envelope - clip->smooth_gain_l) * smooth;
            clip->smooth_gain_r += (target_r * envelope - clip->smooth_gain_r) * smooth;
            float l = asset.left[idx];
            float r = asset.right[idx];
            for (const auto &fx : s.effects) if (fx.enabled) {
                const float hz = std::clamp(fx.frequency_hz, 10.0f, 0.49f * float(sample_rate_));
                if (fx.type == AudioEffectType::LowPass) {
                    const float a = 1.0f - std::exp(-6.28318530718f * hz / float(sample_rate_));
                    clip->lp_l += a * (l - clip->lp_l); clip->lp_r += a * (r - clip->lp_r); l = clip->lp_l; r = clip->lp_r;
                } else if (fx.type == AudioEffectType::HighPass) {
                    const float rc = 1.0f / (6.28318530718f * hz), dt = 1.0f / float(sample_rate_), a = rc / (rc + dt);
                    const float nl = a * (clip->hp_l + l - clip->hp_prev_l), nr = a * (clip->hp_r + r - clip->hp_prev_r);
                    clip->hp_prev_l = l; clip->hp_prev_r = r; clip->hp_l = nl; clip->hp_r = nr; l = nl; r = nr;
                } else if (fx.type == AudioEffectType::CompressorLimiter) {
                    const float peak = std::max(std::abs(l), std::abs(r));
                    const float attack = std::exp(-1.0f / (std::max(0.1f, fx.attack_ms) * 0.001f * sample_rate_));
                    const float release = std::exp(-1.0f / (std::max(1.0f, fx.release_ms) * 0.001f * sample_rate_));
                    clip->compressor_envelope = peak > clip->compressor_envelope ? attack * clip->compressor_envelope + (1.0f-attack)*peak : release * clip->compressor_envelope + (1.0f-release)*peak;
                    const float threshold = db_to_linear(fx.threshold_db), ratio = std::max(1.0f, fx.ratio);
                    float g = 1.0f;
                    if (clip->compressor_envelope > threshold) {
                        const float over_db = 20.0f * std::log10(std::max(clip->compressor_envelope / threshold, 1.0f));
                        g = db_to_linear(-over_db * (1.0f - 1.0f / ratio));
                    }
                    g *= db_to_linear(fx.makeup_db); l *= g; r *= g;
                }
            }
            left[i] += l * clip->smooth_gain_l; right[i] += r * clip->smooth_gain_r;
        }
    }
    /* Master peak protection: linked, soft-knee limiter guarantees legal float range. */
    float peak = 0.0f; for (uint32_t i=0;i<frames;++i) peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    const float trim = peak > 0.98f ? 0.98f / peak : 1.0f;
    for (uint32_t i=0;i<frames;++i) { left[i] = std::tanh(left[i] * trim); right[i] = std::tanh(right[i] * trim); }
}

void SourceAudioRuntime::log_output_packet(uint64_t timestamp_ns,
                                           uint32_t frames,
                                           const char *path)
{
    const uint64_t now = os_gettime_ns();
    const uint64_t previous_delivery =
        log_last_delivery_ns_.exchange(now, std::memory_order_acq_rel);
    if (previous_delivery != 0 && now > previous_delivery + 40000000ULL) {
        audio_log(TitleLogLevel::Warning,
             "[BGL Audio][delivery-stall] source='%s' path=%s "
             "wall_gap_ms=%.3f",
             source_name_.c_str(), path,
             double(now - previous_delivery) / 1000000.0);
    }
    const uint64_t duration_ns = sample_rate_ > 0
        ? uint64_t(frames) * 1000000000ULL / sample_rate_
        : 0;
    if (log_last_packet_timestamp_ns_ != 0 && duration_ns != 0) {
        const uint64_t expected = log_last_packet_timestamp_ns_ + duration_ns;
        const uint64_t diff = timestamp_ns > expected
            ? timestamp_ns - expected
            : expected - timestamp_ns;
        if (diff > 3000000ULL) {
            audio_log(TitleLogLevel::Warning,
                 "[BGL Audio][timestamp-gap] source='%s' path=%s "
                 "expected=%llu actual=%llu diff_ms=%.3f frames=%u",
                 source_name_.c_str(), path,
                 static_cast<unsigned long long>(expected),
                 static_cast<unsigned long long>(timestamp_ns),
                 double(diff) / 1000000.0, frames);
        }
    }
    log_last_packet_timestamp_ns_ = timestamp_ns;
    ++log_output_blocks_;
    log_output_frames_ += frames;

    if (log_last_summary_ns_ == 0)
        log_last_summary_ns_ = now;
    if (now - log_last_summary_ns_ < 1000000000ULL)
        return;

    double title_time = 0.0;
    bool playing = false;
    bool visible = false;
    bool editor_preview = false;
    int64_t cursor = 0;
    uint64_t lead_timestamp = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        title_time = title_time_;
        playing = playing_;
        visible = visible_;
        editor_preview = editor_preview_;
        cursor = output_sample_cursor_;
        lead_timestamp = editor_preview_
            ? editor_monitor_cadence_.next_timestamp_ns()
            : output_scheduler_.next_timestamp_ns();
    }
    const double lead_ms = lead_timestamp > now
        ? double(lead_timestamp - now) / 1000000.0
        : -double(now - lead_timestamp) / 1000000.0;
    audio_log(TitleLogLevel::Info,
         "[BGL Audio][flow] source='%s' path=%s editor_preview=%s "
         "playing=%s visible=%s title_time=%.3f cursor=%lld "
         "lead_ms=%.2f total_blocks=%llu total_frames=%llu",
         source_name_.c_str(), path, editor_preview ? "true" : "false",
         playing ? "true" : "false", visible ? "true" : "false",
         title_time, static_cast<long long>(cursor), lead_ms,
         static_cast<unsigned long long>(log_output_blocks_),
         static_cast<unsigned long long>(log_output_frames_));
    log_last_summary_ns_ = now;
}

void SourceAudioRuntime::output_worker_main()
{
    set_audio_output_thread_name();
    std::vector<float> left;
    std::vector<float> right;

    bool starts_as_editor_preview = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        starts_as_editor_preview = editor_preview_;
    }
    audio_log(TitleLogLevel::Info,
              "[BGL Audio][output-worker-start] source='%s' editor_preview=%s",
              source_name_.c_str(),
              starts_as_editor_preview ? "true" : "false");

    for (;;) {
        bool editor_mode = false;
        bool active = false;
        bool idle_visible = false;
        bool idle_playing = false;
        std::size_t idle_clip_count = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto ready = [this] {
                return quit_.load(std::memory_order_acquire) ||
                       (source_ && visible_ && playing_ && !clips_.empty());
            };
            if (!ready())
                output_cv_.wait_for(lock, std::chrono::seconds(1), ready);
            if (quit_.load(std::memory_order_acquire))
                return;
            active = source_ && visible_ && playing_ && !clips_.empty();
            editor_mode = editor_preview_;
            idle_visible = visible_;
            idle_playing = playing_;
            idle_clip_count = clips_.size();
        }

        if (!active) {
            const uint64_t now = os_gettime_ns();
            uint64_t last_idle =
                log_last_idle_ns_.load(std::memory_order_acquire);
            if (now > last_idle + 1000000000ULL &&
                log_last_idle_ns_.compare_exchange_strong(
                    last_idle, now, std::memory_order_acq_rel)) {
                audio_log(
                    TitleLogLevel::Debug,
                    "[BGL Audio][worker-idle] source='%s' path=%s "
                    "visible=%s playing=%s decoded_clips=%llu",
                    source_name_.c_str(),
                    editor_mode ? "editor-monitor-sample-locked-worker"
                                : "source-worker",
                    idle_visible ? "true" : "false",
                    idle_playing ? "true" : "false",
                    static_cast<unsigned long long>(idle_clip_count));
            }
            continue;
        }

        if (editor_mode) {
            uint64_t timestamp = 0;
            uint32_t frames = 0;
            bool clock_reset = false;
            bool hard_resync = false;
            bool catchup = false;
            double catchup_late_ms = 0.0;
            double logged_title_time = 0.0;
            int64_t logged_cursor = 0;
            double late_ms = 0.0;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (quit_.load(std::memory_order_acquire))
                    return;
                if (!editor_preview_ || !source_ || !visible_ || !playing_ ||
                    clips_.empty())
                    continue;

                const uint64_t now = os_gettime_ns();
                const uint64_t block_ns =
                    uint64_t(block_frames_) * 1000000000ULL /
                    std::max<uint32_t>(1, sample_rate_);

                if (discontinuity_ || !editor_monitor_cadence_.initialized()) {
                    output_sample_cursor_ = transport_sample_cursor(
                        title_time_, sample_rate_, reverse_);
                    editor_monitor_cadence_ =
                        RealtimeMonitorCadence(block_ns, 3, 70000000ULL);
                    editor_monitor_cadence_.reset(now);
                    discontinuity_ = false;
                    log_last_packet_timestamp_ns_.store(
                        0, std::memory_order_release);
                    log_last_delivery_ns_.store(
                        0, std::memory_order_release);
                    clock_reset = true;
                    logged_title_time = title_time_;
                    logged_cursor = output_sample_cursor_;
                }

                const uint64_t wait_ns = editor_monitor_cadence_.wait_ns(now);
                if (wait_ns > 0) {
                    output_cv_.wait_for(lock,
                                        std::chrono::nanoseconds(wait_ns));
                    continue;
                }

                /* The monitor deadline is sample-locked and advances from the
                 * previous deadline. Small Windows timer oversleep therefore
                 * causes a short contiguous catch-up instead of accumulating
                 * drift and forcing a 20 ms timestamp jump every ~0.5 s. Only
                 * a real stall beyond 70 ms re-anchors to the editor playhead. */
                const uint64_t cadence_lateness_ns =
                    editor_monitor_cadence_.lateness_ns(now);
                if (cadence_lateness_ns >= block_ns &&
                    !editor_monitor_cadence_.hard_resync_needed(now)) {
                    catchup = true;
                    catchup_late_ms =
                        double(cadence_lateness_ns) / 1000000.0;
                }
                if (editor_monitor_cadence_.hard_resync_needed(now)) {
                    late_ms = double(editor_monitor_cadence_.lateness_ns(now)) /
                              1000000.0;
                    output_sample_cursor_ = transport_sample_cursor(
                        title_time_, sample_rate_, reverse_);
                    editor_monitor_cadence_.reset(now);
                    log_last_packet_timestamp_ns_.store(
                        0, std::memory_order_release);
                    log_last_delivery_ns_.store(
                        0, std::memory_order_release);
                    ++log_late_repairs_;
                    bgl::perf::add(
                        bgl::perf::Counter::AudioOutputUnderruns);
                    bgl::perf::add(
                        bgl::perf::Counter::AudioTimestampRepairs);
                    for (const auto &clip : clips_) {
                        if (!clip)
                            continue;
                        clip->hp_l = clip->hp_r = clip->hp_prev_l =
                            clip->hp_prev_r = 0.0f;
                        clip->lp_l = clip->lp_r =
                            clip->compressor_envelope = 0.0f;
                        clip->smooth_gain_l = clip->smooth_gain_r = 0.0f;
                    }
                    hard_resync = true;
                    logged_title_time = title_time_;
                    logged_cursor = output_sample_cursor_;
                }

                frames = block_frames_;
                timestamp = editor_monitor_cadence_.take_timestamp();
                mix_block(output_sample_cursor_, frames, reverse_, left, right);
                advance_transport_cursor(output_sample_cursor_, frames, reverse_);
                independent_sample_cursor_ += frames;
                independent_time_ =
                    double(independent_sample_cursor_) / sample_rate_;
            }

            if (clock_reset) {
                audio_log(
                    TitleLogLevel::Info,
                    "[BGL Audio][monitor-clock-reset] source='%s' "
                    "title_time=%.3f sample_cursor=%lld cadence_ms=%.3f "
                    "prefill_packets=3 deadline_mode=sample-locked",
                    source_name_.c_str(), logged_title_time,
                    static_cast<long long>(logged_cursor),
                    1000.0 * double(block_frames_) /
                        std::max<uint32_t>(1, sample_rate_));
            }
            if (catchup) {
                audio_log(
                    TitleLogLevel::Debug,
                    "[BGL Audio][monitor-catchup] source='%s' "
                    "late_ms=%.3f timestamp_mode=continuous",
                    source_name_.c_str(), catchup_late_ms);
            }
            if (hard_resync) {
                audio_log(
                    TitleLogLevel::Warning,
                    "[BGL Audio][monitor-hard-resync] source='%s' "
                    "late_ms=%.3f title_time=%.3f sample_cursor=%lld",
                    source_name_.c_str(), late_ms, logged_title_time,
                    static_cast<long long>(logged_cursor));
            }

            obs_source_audio audio = {};
            audio.data[0] =
                reinterpret_cast<const uint8_t *>(left.data());
            audio.data[1] =
                reinterpret_cast<const uint8_t *>(right.data());
            audio.frames = frames;
            audio.speakers = SPEAKERS_STEREO;
            audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
            audio.samples_per_sec = sample_rate_;
            audio.timestamp = timestamp;
            obs_source_output_audio(source_, &audio);
            bgl::perf::add(bgl::perf::Counter::AudioOutputBlocks);
            log_output_packet(timestamp, frames,
                              "editor-monitor-sample-locked-worker");
            continue;
        }

        bool produced = false;
        for (int emitted = 0;; ++emitted) {
            uint64_t timestamp = 0;
            uint32_t frames = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (quit_.load(std::memory_order_acquire))
                    return;
                if (editor_preview_ || !source_ || !visible_ || !playing_ ||
                    clips_.empty())
                    break;
                if (emitted >=
                    output_scheduler_.maximum_blocks_per_wake())
                    break;

                const uint64_t now = os_gettime_ns();
                if (discontinuity_ || !output_scheduler_.initialized()) {
                    output_sample_cursor_ = transport_sample_cursor(
                        title_time_, sample_rate_, reverse_);
                    output_scheduler_.set_mode(
                        AudioOutputSchedulerMode::BufferedSource);
                    output_scheduler_.reset(now);
                    discontinuity_ = false;
                    log_last_packet_timestamp_ns_.store(
                        0, std::memory_order_release);
                    log_last_delivery_ns_.store(
                        0, std::memory_order_release);
                    audio_log(
                        TitleLogLevel::Info,
                        "[BGL Audio][worker-clock-reset] source='%s' "
                        "title_time=%.3f sample_cursor=%lld",
                        source_name_.c_str(), title_time_,
                        static_cast<long long>(
                            output_sample_cursor_));
                } else if (output_scheduler_.is_late(now)) {
                    bgl::perf::add(
                        bgl::perf::Counter::AudioOutputUnderruns);
                    bgl::perf::add(
                        bgl::perf::Counter::AudioTimestampRepairs);
                    ++log_late_repairs_;
                    audio_log(
                        TitleLogLevel::Warning,
                        "[BGL Audio][worker-underrun] source='%s' "
                        "title_time=%.3f next_ts=%llu now=%llu",
                        source_name_.c_str(), title_time_,
                        static_cast<unsigned long long>(
                            output_scheduler_.next_timestamp_ns()),
                        static_cast<unsigned long long>(now));
                    output_sample_cursor_ = transport_sample_cursor(
                        title_time_, sample_rate_, reverse_);
                    output_scheduler_.reset(now);
                    log_last_packet_timestamp_ns_.store(
                        0, std::memory_order_release);
                    log_last_delivery_ns_.store(
                        0, std::memory_order_release);
                    for (const auto &clip : clips_) {
                        if (!clip)
                            continue;
                        clip->hp_l = clip->hp_r = clip->hp_prev_l =
                            clip->hp_prev_r = 0.0f;
                        clip->lp_l = clip->lp_r =
                            clip->compressor_envelope = 0.0f;
                        clip->smooth_gain_l = clip->smooth_gain_r = 0.0f;
                    }
                }

                if (!output_scheduler_.should_fill(now))
                    break;

                frames = block_frames_;
                timestamp = output_scheduler_.next_timestamp_ns();
                mix_block(output_sample_cursor_, frames, reverse_, left, right);
                advance_transport_cursor(output_sample_cursor_, frames, reverse_);
                independent_sample_cursor_ += frames;
                independent_time_ =
                    double(independent_sample_cursor_) / sample_rate_;
                output_scheduler_.advance(frames, sample_rate_);
            }

            obs_source_audio audio = {};
            audio.data[0] =
                reinterpret_cast<const uint8_t *>(left.data());
            audio.data[1] =
                reinterpret_cast<const uint8_t *>(right.data());
            audio.frames = frames;
            audio.speakers = SPEAKERS_STEREO;
            audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
            audio.samples_per_sec = sample_rate_;
            audio.timestamp = timestamp;
            obs_source_output_audio(source_, &audio);
            bgl::perf::add(bgl::perf::Counter::AudioOutputBlocks);
            log_output_packet(timestamp, frames, "source-worker");
            produced = true;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (quit_.load(std::memory_order_acquire))
            return;
        if (editor_preview_ || !source_ || !visible_ || !playing_ ||
            clips_.empty())
            continue;

        const uint64_t wait_ns =
            output_scheduler_.wait_ns(os_gettime_ns());
        if (wait_ns == 0) {
            if (!produced)
                output_cv_.wait_for(
                    lock, std::chrono::milliseconds(1));
            continue;
        }
        output_cv_.wait_for(lock, std::chrono::nanoseconds(wait_ns));
    }
}

void SourceAudioRuntime::pump()
{
    /* Audio delivery is always performed by the dedicated output worker.
     * video_tick only publishes transport state and wakes the scheduler. */
    output_cv_.notify_one();
}

int64_t SourceAudioRuntime::duration_ms() const { std::lock_guard<std::mutex> lock(mutex_); return duration_ms_; }
int64_t SourceAudioRuntime::time_ms() const { return reported_time_ms_.load(std::memory_order_acquire); }
enum obs_media_state SourceAudioRuntime::media_state() const { return state_.load(std::memory_order_acquire); }

} // namespace bgl::audio
