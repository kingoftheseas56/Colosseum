#pragma once

#include "WASAPIAudioSink.h"
#include "player2/core/Player2Types.h"

#include <QtCore/QString>

#include <vector>

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace Colosseum::Player2 {

// Pitch-preserving playback-speed stage — an isolated FFmpeg `atempo` graph between the resampler/
// normalizer and the endpoint. This is what makes 1.5× actually play faster without turning voices into
// chipmunks (the same family as mpv's scaletempo). At speed 1.0 it is a bit-transparent passthrough
// (no graph). Each emitted buffer carries:
//   - ptsUs : the INPUT media time it represents. The abuffer runs on a 1/1'000'000 time_base, so
//             atempo's compressed output pts (µs) × speed recovers the original media time.
//   - speed : the rate, so the sink advances the audio-master clock at the true media rate.
// Speed range is clamped to a single atempo instance's safe window [0.5, 2.0]; the menu presets fit.
class AudioTempo
{
public:
    AudioTempo();
    ~AudioTempo();

    // (Re)build the graph for the endpoint format and speed. Safe to call to change speed; the caller
    // discards stale filter output around the change (flush()).
    bool configure(const AudioFormat &format, double speed, QString *error);
    double speed() const noexcept { return m_speed; }

    // Push one packed-float32 buffer (its ptsUs is the input media time); append zero or more
    // time-stretched buffers (atempo buffers internally, so a push may emit nothing yet).
    bool process(const AudioBuffer &input, std::vector<AudioBuffer> *outputs, QString *error);
    // Drain remaining buffered output at end of stream.
    bool drain(std::vector<AudioBuffer> *outputs, QString *error);
    // Reset filter state on seek/track change without changing the speed.
    void flush();

private:
    void teardown();
    bool pullOutputs(std::vector<AudioBuffer> *outputs, QString *error);

    double m_speed = 1.0;
    AudioFormat m_format;
    AVFilterGraph *m_graph = nullptr;
    AVFilterContext *m_source = nullptr;
    AVFilterContext *m_sink = nullptr;
    AVFrame *m_frame = nullptr;
};

} // namespace Colosseum::Player2
