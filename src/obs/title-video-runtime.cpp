#include "title-video-runtime.h"

#include "title-logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <thread>

#if defined(BGL_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#endif

namespace bgl::video {
namespace {

static double finite_positive(double value, double fallback = 0.0)
{
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

static double project_frame_rate_or_default(double project_frame_rate)
{
    return finite_positive(project_frame_rate, 60.0);
}

static double video_frame_rate_for_layer(const Layer &layer, double project_frame_rate = 0.0)
{
    return finite_positive(layer.video_frame_rate,
                           project_frame_rate_or_default(project_frame_rate));
}

struct FrameRequest {
    bool valid = false;
    qint64 timeline_frame_number = -1;
    qint64 media_frame_number = -1;
    double media_time = -1.0;
    double project_frame_rate = 0.0;
    double media_frame_rate = 0.0;
};

static FrameRequest frame_request_for_layer(const Layer &layer, double title_time,
                                            double project_frame_rate)
{
    FrameRequest request;
    const double clip_duration = std::max(0.0, layer.out_time - layer.in_time);
    if (clip_duration <= 0.0 || title_time < layer.in_time || title_time >= layer.out_time)
        return request;

    request.project_frame_rate = project_frame_rate_or_default(project_frame_rate);
    request.media_frame_rate = video_frame_rate_for_layer(layer, request.project_frame_rate);

    const double media_duration = finite_positive(layer.video_media_duration);
    const double media_in = std::clamp(layer.video_in_point, 0.0,
                                       media_duration > 0.0 ? media_duration : layer.video_in_point);
    double media_out = layer.video_out_point > media_in
        ? layer.video_out_point
        : (media_duration > media_in ? media_duration : media_in + clip_duration);
    if (media_duration > 0.0)
        media_out = std::min(media_out, media_duration);
    const double span = std::max(0.0, media_out - media_in);
    if (span <= 0.0) {
        request.valid = true;
        request.timeline_frame_number = 0;
        request.media_frame_number = static_cast<qint64>(
            std::floor(std::max(0.0, media_in) * request.media_frame_rate + 1e-9));
        request.media_time = request.media_frame_number / request.media_frame_rate;
        return request;
    }

    /* Development Version 230: the project/timeline frame is authoritative.
     * Media with a different FPS is mapped by a stable duplicate/drop policy:
     * timeline frame N selects floor(N * media_fps / project_fps) from the
     * trimmed source range.  This matches NLE behavior, avoids timestamp jitter
     * from sub-frame playhead values, and prevents cumulative drift across
     * 23.976/25/30/50/60fps combinations. */
    request.timeline_frame_number = static_cast<qint64>(std::llround(
        std::max(0.0, title_time - layer.in_time) * request.project_frame_rate));
    const qint64 media_first_frame = static_cast<qint64>(std::floor(
        std::max(0.0, media_in) * request.media_frame_rate + 1e-9));
    const qint64 media_end_frame_exclusive = std::max<qint64>(
        media_first_frame + 1, static_cast<qint64>(std::ceil(
            std::max(0.0, media_out) * request.media_frame_rate - 1e-9)));
    const qint64 span_frames = std::max<qint64>(1,
        media_end_frame_exclusive - media_first_frame);
    qint64 media_offset_frames = static_cast<qint64>(std::floor(
        (static_cast<double>(request.timeline_frame_number) * request.media_frame_rate) /
        request.project_frame_rate + 1e-9));
    if (layer.video_loop)
        media_offset_frames = ((media_offset_frames % span_frames) + span_frames) % span_frames;
    else
        media_offset_frames = std::clamp<qint64>(media_offset_frames, 0, span_frames - 1);

    request.media_frame_number = media_first_frame + media_offset_frames;
    request.media_time = request.media_frame_number / request.media_frame_rate;
    request.valid = true;
    return request;
}

#if defined(BGL_HAVE_FFMPEG)
static std::string dictionary_value(AVDictionary *metadata, const char *key)
{
    const AVDictionaryEntry *entry = av_dict_get(metadata, key, nullptr, 0);
    return entry && entry->value ? entry->value : std::string();
}

struct Decoder {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    SwsContext *sws = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    int stream_index = -1;
    AVRational time_base{1, 1};
    double last_pts = -1.0;
    std::string path;

    ~Decoder() { close(); }

    void close()
    {
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        if (sws) sws_freeContext(sws);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
        packet = nullptr;
        frame = nullptr;
        sws = nullptr;
        codec = nullptr;
        format = nullptr;
        stream_index = -1;
        last_pts = -1.0;
        path.clear();
    }

    bool open(const std::string &next_path, int requested_stream)
    {
        if (format && path == next_path &&
            (requested_stream < 0 || requested_stream == stream_index))
            return true;
        close();
        if (next_path.empty())
            return false;
        if (avformat_open_input(&format, next_path.c_str(), nullptr, nullptr) < 0)
            return false;
        format->flags |= AVFMT_FLAG_FAST_SEEK;
        if (avformat_find_stream_info(format, nullptr) < 0) {
            close();
            return false;
        }
        const int selected = requested_stream >= 0
            ? requested_stream
            : av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (selected < 0 || selected >= static_cast<int>(format->nb_streams) ||
            format->streams[selected]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            close();
            return false;
        }
        AVStream *stream = format->streams[selected];
        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            close();
            return false;
        }
        codec = avcodec_alloc_context3(decoder);
        if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
            close();
            return false;
        }
        /* Let FFmpeg use frame/slice threading where the codec supports it.
         * Video layers are decoded asynchronously, so this never blocks the
         * OBS/UI thread, but it prevents long-GOP or high-resolution files from
         * falling behind because one worker decodes every frame serially. */
        codec->thread_count = 0;
        codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(codec, decoder, nullptr) < 0) {
            close();
            return false;
        }
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet) {
            close();
            return false;
        }
        stream_index = selected;
        time_base = stream->time_base;
        path = next_path;
        return true;
    }

    bool seek(double seconds)
    {
        if (!format || !codec || stream_index < 0)
            return false;
        const int64_t timestamp = av_rescale_q(
            static_cast<int64_t>(std::llround(std::max(0.0, seconds) * AV_TIME_BASE)),
            AVRational{1, AV_TIME_BASE}, time_base);
        if (av_seek_frame(format, stream_index, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
            return false;
        avcodec_flush_buffers(codec);
        last_pts = -1.0;
        return true;
    }

    QImage convert_current_frame(double *pts_out)
    {
        int64_t pts_value = frame->best_effort_timestamp;
        if (pts_value == AV_NOPTS_VALUE)
            pts_value = frame->pts;
        const double pts = pts_value == AV_NOPTS_VALUE
            ? (last_pts < 0.0 ? 0.0 : last_pts)
            : pts_value * av_q2d(time_base);
        if (pts_out) *pts_out = pts;

        const int width = frame->width;
        const int height = frame->height;
        if (width <= 0 || height <= 0)
            return {};
        sws = sws_getCachedContext(
            sws, width, height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr);
        if (!sws)
            return {};
        QImage converted(width, height, QImage::Format_ARGB32);
        uint8_t *planes[4] = {converted.bits(), nullptr, nullptr, nullptr};
        int strides[4] = {static_cast<int>(converted.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, height, planes, strides);
        return converted;
    }

    QImage decode(double target_seconds)
    {
        if (!format || !codec || !frame || !packet)
            return {};
        const double target = std::max(0.0, target_seconds);
        const bool moving_backward = last_pts >= 0.0 && target + 0.0005 < last_pts;
        const bool jumping_forward = last_pts >= 0.0 && target - last_pts > 0.750;
        if (last_pts < 0.0 || moving_backward || jumping_forward)
            seek(target);

        QImage selected;
        double selected_pts = -1.0;
        const int max_packets = 320;
        int packets_read = 0;
        while (packets_read++ < max_packets && av_read_frame(format, packet) >= 0) {
            if (packet->stream_index != stream_index) {
                av_packet_unref(packet);
                continue;
            }
            const int sent = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (sent < 0)
                continue;
            while (avcodec_receive_frame(codec, frame) >= 0) {
                int64_t pts_value = frame->best_effort_timestamp;
                if (pts_value == AV_NOPTS_VALUE)
                    pts_value = frame->pts;
                const double pts = pts_value == AV_NOPTS_VALUE
                    ? (last_pts < 0.0 ? 0.0 : last_pts)
                    : pts_value * av_q2d(time_base);

                /* The 228 path converted every decoded intermediate frame to
                 * BGRA while chasing the requested frame. Some MP4/MOV files
                 * require many frames after a keyframe seek, and doing
                 * sws_scale() on all of them made playback crawl. Decode and
                 * discard compressed frames until the presentation timestamp is
                 * useful, then convert exactly the frame that will be uploaded. */
                const bool reached_target = pts + 0.0005 >= target;
                if (reached_target || packets_read >= max_packets) {
                    selected = convert_current_frame(&selected_pts);
                    av_frame_unref(frame);
                    if (!selected.isNull())
                        break;
                } else {
                    av_frame_unref(frame);
                }
            }
            if (!selected.isNull())
                break;
        }
        if (!selected.isNull())
            last_pts = selected_pts;
        return selected;
    }
};
#endif

} // namespace

MediaInfo probe_media(const std::string &path)
{
    MediaInfo info;
#if defined(BGL_HAVE_FFMPEG)
    AVFormatContext *format = nullptr;
    if (path.empty() || avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        info.error = "Could not open media file";
        return info;
    }
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> guard(
        format, [](AVFormatContext *context) { avformat_close_input(&context); });
    if (avformat_find_stream_info(format, nullptr) < 0) {
        info.error = "Could not read media stream information";
        return info;
    }
    info.duration = format->duration > 0
        ? static_cast<double>(format->duration) / AV_TIME_BASE : 0.0;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        AVStream *stream = format->streams[i];
        if (!stream || !stream->codecpar)
            continue;
        const AVCodecParameters *parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && info.video_stream_index < 0) {
            info.video_stream_index = static_cast<int>(i);
            info.width = parameters->width;
            info.height = parameters->height;
            const AVRational rate = av_guess_frame_rate(format, stream, nullptr);
            info.frame_rate = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
            const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(
                static_cast<AVPixelFormat>(parameters->format));
            info.has_alpha = descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA);
            if (stream->duration > 0)
                info.duration = std::max(info.duration,
                    stream->duration * av_q2d(stream->time_base));
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            AudioStreamInfo audio;
            audio.stream_index = static_cast<int>(i);
            audio.sample_rate = parameters->sample_rate;
            audio.channels = parameters->ch_layout.nb_channels;
            audio.language = dictionary_value(stream->metadata, "language");
            audio.title = dictionary_value(stream->metadata, "title");
            info.audio_streams.push_back(std::move(audio));
        }
    }
    info.valid = info.video_stream_index >= 0 && info.width > 0 && info.height > 0;
    if (!info.valid)
        info.error = "No decodable video stream was found";
#else
    (void)path;
    info.error = "This build has no FFmpeg video backend";
#endif
    return info;
}

struct FrameRuntime::Impl {
    FrameRuntime *owner = nullptr;
    struct Entry {
        std::mutex mutex;
        std::string path;
        int stream_index = -1;
        double requested_time = 0.0;
        qint64 requested_frame_number = -1;
        double requested_media_frame_rate = 0.0;
        uint64_t requested_generation = 0;
        uint64_t decoded_generation = 0;
        double decoded_time = -1.0;
        qint64 decoded_frame_number = -1;
        qint64 last_requested_frame_number = -1;
        QImage frame;
        std::unordered_map<qint64, QImage> decoded_frame_cache;
        std::deque<qint64> decoded_frame_lru;
        std::chrono::steady_clock::time_point last_request =
            std::chrono::steady_clock::now();
#if defined(BGL_HAVE_FFMPEG)
        Decoder decoder;
#endif
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;
    std::deque<std::string> queue;
    std::unordered_set<std::string> queued;
    std::atomic_bool quit{false};
    std::thread worker;

    static void touch_decoded_frame_locked(Entry &entry, qint64 frame_number)
    {
        auto found = std::find(entry.decoded_frame_lru.begin(),
                               entry.decoded_frame_lru.end(), frame_number);
        if (found != entry.decoded_frame_lru.end())
            entry.decoded_frame_lru.erase(found);
        entry.decoded_frame_lru.push_back(frame_number);
    }

    static void remember_decoded_frame_locked(Entry &entry, qint64 frame_number, const QImage &image)
    {
        if (frame_number < 0 || image.isNull())
            return;
        entry.decoded_frame_cache[frame_number] = image;
        touch_decoded_frame_locked(entry, frame_number);
        /* Keep roughly two seconds of source frames at 60fps. This makes
         * frame-step backwards and small reverse scrubs deterministic without
         * forcing FFmpeg to seek backwards for every single frame. */
        constexpr std::size_t kMaxCachedDecodedFrames = 240;
        while (entry.decoded_frame_lru.size() > kMaxCachedDecodedFrames) {
            const qint64 evict = entry.decoded_frame_lru.front();
            entry.decoded_frame_lru.pop_front();
            if (evict != frame_number)
                entry.decoded_frame_cache.erase(evict);
        }
    }

    static bool cached_decoded_frame_locked(Entry &entry, qint64 frame_number, VideoFrame &out)
    {
        if (frame_number < 0)
            return false;
        const auto found = entry.decoded_frame_cache.find(frame_number);
        if (found == entry.decoded_frame_cache.end() || found->second.isNull())
            return false;
        touch_decoded_frame_locked(entry, frame_number);
        out.image = found->second;
        out.frame_number = frame_number;
        out.media_time = entry.decoded_time >= 0.0 && entry.decoded_frame_number == frame_number
            ? entry.decoded_time : -1.0;
        entry.frame = found->second;
        entry.decoded_frame_number = frame_number;
        return true;
    }

    static bool nearest_cached_frame_at_or_before_locked(Entry &entry, qint64 frame_number, VideoFrame &out)
    {
        if (frame_number < 0 || entry.decoded_frame_cache.empty())
            return false;
        qint64 best = -1;
        for (const auto &item : entry.decoded_frame_cache) {
            if (item.first <= frame_number && (best < 0 || item.first > best) && !item.second.isNull())
                best = item.first;
        }
        return best >= 0 && cached_decoded_frame_locked(entry, best, out);
    }

    explicit Impl(FrameRuntime *runtime)
        : owner(runtime), worker([this] { run(); }) {}

    ~Impl()
    {
        quit.store(true, std::memory_order_release);
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void enqueue_locked(const std::string &id)
    {
        if (queued.insert(id).second)
            queue.push_back(id);
        cv.notify_one();
    }

    void run()
    {
        while (!quit.load(std::memory_order_acquire)) {
            std::shared_ptr<Entry> entry;
            std::string id;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return quit.load(std::memory_order_acquire) || !queue.empty(); });
                if (quit.load(std::memory_order_acquire))
                    break;
                id = std::move(queue.front());
                queue.pop_front();
                queued.erase(id);
                const auto found = entries.find(id);
                if (found != entries.end()) entry = found->second;
            }
            if (!entry)
                continue;

            std::string path;
            int stream = -1;
            double time = 0.0;
            qint64 frame_number = -1;
            double media_frame_rate = 0.0;
            uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                path = entry->path;
                stream = entry->stream_index;
                time = entry->requested_time;
                frame_number = entry->requested_frame_number;
                media_frame_rate = entry->requested_media_frame_rate;
                generation = entry->requested_generation;
            }

            QImage decoded;
#if defined(BGL_HAVE_FFMPEG)
            if (entry->decoder.open(path, stream))
                decoded = entry->decoder.decode(time);
#endif
            bool requeue = false;
            bool published = false;
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                if (!decoded.isNull() && generation >= entry->decoded_generation) {
                    entry->frame = decoded;
                    remember_decoded_frame_locked(*entry, frame_number, decoded);
                    entry->decoded_generation = generation;
#if defined(BGL_HAVE_FFMPEG)
                    entry->decoded_time = entry->decoder.last_pts;
#else
                    entry->decoded_time = time;
#endif
                    entry->decoded_frame_number = frame_number >= 0 ? frame_number : 0;
                    published = true;
                }
                requeue = entry->requested_generation > generation;
            }

            /* Development Version 237: decode a short forward window on the
             * same worker turn.  Playback asks for frames one at a time; if the
             * decoder only publishes the exact requested frame, the render loop
             * wakes FFmpeg for every tick and frequently falls behind.  A small
             * look-ahead cache keeps normal forward playback GPU/upload bound,
             * while the request-generation checks below stop immediately when
             * the user scrubs, reverses or jumps elsewhere. */
#if defined(BGL_HAVE_FFMPEG)
            if (published && !requeue && media_frame_rate > 0.0 && frame_number >= 0) {
                const int prefetch_count = 4;
                for (int ahead = 1; ahead <= prefetch_count; ++ahead) {
                    {
                        std::lock_guard<std::mutex> lock(entry->mutex);
                        if (entry->requested_generation != generation) {
                            requeue = true;
                            break;
                        }
                    }
                    const qint64 next_frame = frame_number + ahead;
                    {
                        std::lock_guard<std::mutex> lock(entry->mutex);
                        if (entry->decoded_frame_cache.find(next_frame) !=
                            entry->decoded_frame_cache.end())
                            continue;
                    }
                    QImage predecoded = entry->decoder.decode(
                        static_cast<double>(next_frame) / media_frame_rate);
                    if (predecoded.isNull())
                        break;
                    std::lock_guard<std::mutex> lock(entry->mutex);
                    if (entry->requested_generation != generation) {
                        requeue = true;
                        break;
                    }
                    remember_decoded_frame_locked(*entry, next_frame, predecoded);
                }
            }
#endif
            if (published && owner)
                emit owner->frameReady(QString::fromStdString(id));
            if (requeue) {
                std::lock_guard<std::mutex> map_lock(mutex);
                enqueue_locked(id);
            }

            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = entries.begin(); it != entries.end();) {
                std::lock_guard<std::mutex> entry_lock(it->second->mutex);
                if (now - it->second->last_request > std::chrono::minutes(2))
                    it = entries.erase(it);
                else
                    ++it;
            }
        }
    }
};

FrameRuntime &FrameRuntime::instance()
{
    static FrameRuntime runtime;
    return runtime;
}

FrameRuntime::FrameRuntime() : QObject(nullptr), impl_(new Impl(this)) {}
FrameRuntime::~FrameRuntime() { delete impl_; }

VideoFrame FrameRuntime::frame_for_layer(const Layer &layer, double title_time,
                                             double project_frame_rate)
{
    VideoFrame result;
    if (!impl_ || layer.id.empty() || layer.video_source.empty())
        return result;
    const FrameRequest request = frame_request_for_layer(layer, title_time, project_frame_rate);
    if (!request.valid)
        return result;

    const qint64 requested_frame = request.media_frame_number;
    const double quantized_media_time = request.media_time;

    std::shared_ptr<Impl::Entry> entry;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto &slot = impl_->entries[layer.id];
        if (!slot) slot = std::make_shared<Impl::Entry>();
        entry = slot;
    }
    bool enqueue = false;
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        const bool identity_changed = entry->path != layer.video_source ||
                                      entry->stream_index != layer.video_stream_index;
        const bool moving_backward_request = !identity_changed &&
            entry->last_requested_frame_number >= 0 &&
            requested_frame < entry->last_requested_frame_number;
        const bool frame_changed = identity_changed ||
            entry->requested_frame_number != requested_frame;
        entry->last_requested_frame_number = requested_frame;
        if (identity_changed) {
            entry->frame = QImage();
            entry->decoded_time = -1.0;
            entry->decoded_frame_number = -1;
            entry->last_requested_frame_number = -1;
            entry->decoded_generation = 0;
            entry->requested_media_frame_rate = request.media_frame_rate;
            entry->decoded_frame_cache.clear();
            entry->decoded_frame_lru.clear();
        }
        entry->path = layer.video_source;
        entry->stream_index = layer.video_stream_index;
        entry->last_request = std::chrono::steady_clock::now();
        if (!identity_changed && Impl::cached_decoded_frame_locked(*entry, requested_frame, result)) {
            /* Editor scrubbing/frame stepping and duplicate/drop mapping
             * frequently revisit nearby frames. Serve exact hits from the LRU
             * instead of waking FFmpeg or forcing a new upload generation. */
            return result;
        }
        if (frame_changed) {
            entry->requested_time = quantized_media_time;
            entry->requested_frame_number = requested_frame;
            entry->requested_media_frame_rate = request.media_frame_rate;
            ++entry->requested_generation;
            enqueue = true;
        }
        if (moving_backward_request && !identity_changed &&
            Impl::nearest_cached_frame_at_or_before_locked(*entry, requested_frame, result)) {
            /* Backward scrubs/steps may not show a future decoded frame. Use the nearest
             * decoded source frame at or before the requested frame when an
             * exact hit is not ready yet. */
            return result;
        }
        if (!entry->frame.isNull() && entry->decoded_time >= 0.0) {
            if (!moving_backward_request || entry->decoded_frame_number <= requested_frame) {
                /* During realtime forward playback, keep presenting the latest
                 * decoded frame while the async decoder catches up. Version 234
                 * only allowed <= requested frames and returned old LRU entries
                 * too aggressively, which made video appear stuck on the first
                 * decoded frames while audio continued. */
                result.image = entry->frame;
                result.media_time = entry->decoded_time;
                result.frame_number = entry->decoded_frame_number >= 0
                    ? entry->decoded_frame_number : requested_frame;
            }
        }
    }
    if (enqueue) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->enqueue_locked(layer.id);
    }
    return result;
}

std::string FrameRuntime::frame_cache_key_for_layer(const Layer &layer, double title_time,
                                                       double project_frame_rate)
{
    const FrameRequest request = frame_request_for_layer(layer, title_time, project_frame_rate);
    if (request.valid) {
        /* Cache keys must describe the requested frame mapping only. Calling
         * frame_for_layer() here used to enqueue decode work from cache-key
         * calculation, doubling queue churn during playback and making the
         * asynchronous decoder chase stale generations. */
        return std::string("video-frame-map=timeline:") +
            std::to_string(request.timeline_frame_number) +
            ";media-requested:" + std::to_string(request.media_frame_number);
    }
    return "video-out-of-range";
}

void FrameRuntime::forget_layer(const std::string &layer_id)
{
    if (!impl_ || layer_id.empty()) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.erase(layer_id);
    impl_->queued.erase(layer_id);
    impl_->queue.erase(std::remove(impl_->queue.begin(), impl_->queue.end(), layer_id),
                       impl_->queue.end());
}

void FrameRuntime::clear()
{
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.clear();
    impl_->queued.clear();
    impl_->queue.clear();
}

} // namespace bgl::video
