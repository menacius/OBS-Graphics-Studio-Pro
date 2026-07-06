#pragma once

#include "layer-model.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>

namespace bgl::video {

struct AudioStreamInfo {
    int stream_index = -1;
    int sample_rate = 0;
    int channels = 0;
    std::string language;
    std::string title;
};

struct MediaInfo {
    bool valid = false;
    int video_stream_index = -1;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    double frame_rate = 0.0;
    bool has_alpha = false;
    std::vector<AudioStreamInfo> audio_streams;
    std::string error;
};

/* Reads container/stream headers only. It never decodes the media body. */
MediaInfo probe_media(const std::string &path);

struct VideoFrame {
    QImage image;
    double media_time = -1.0;
    qint64 frame_number = -1;
};

/* Shared, asynchronous video-frame decoder. Every request is expressed in the
 * title clock used by SourceAudioRuntime. The decoder may drop obsolete frames
 * while catching up, but it never advances its own clock, preventing A/V drift. */
class FrameRuntime : public QObject {
    Q_OBJECT
public:
    static FrameRuntime &instance();

    VideoFrame frame_for_layer(const Layer &layer, double title_time,
                               double project_frame_rate = 0.0);
    std::string frame_cache_key_for_layer(const Layer &layer, double title_time,
                                          double project_frame_rate = 0.0);
    void forget_layer(const std::string &layer_id);
    void clear();

signals:
    void frameReady(const QString &layer_id);

private:
    FrameRuntime();
    ~FrameRuntime();
    FrameRuntime(const FrameRuntime &) = delete;
    FrameRuntime &operator=(const FrameRuntime &) = delete;

    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace bgl::video
