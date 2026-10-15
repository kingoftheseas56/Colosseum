#pragma once

#include "WASAPIAudioSink.h"
#include "player2/core/Player2Types.h"

#include <QtCore/QString>

#include <atomic>
#include <vector>

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace Colosseum::Player2 {

// Explicit loudness stage between the resampler and the endpoint. Modes are typed, never raw filter
// strings in the public contract:
//   Smooth - bit-transparent; the input buffer is passed through untouched.
//   Light  - the intent of the previous dynaudnorm path (gentle dynamic gain).
//   Full   - EBU R128 loudness normalization (loudnorm), which may cost more CPU/latency.
// The stage owns an isolated FFmpeg filter graph for Light/Full; the packed float32 endpoint format
// is preserved on both sides so the sink and audio-master clock are unaffected.
class AudioNormalizer
{
public:
    AudioNormalizer();
    ~AudioNormalizer();

    // (Re)build the graph for the given endpoint format and mode. Safe to call to switch modes; the
    // caller flushes filter latency by discarding stale output around the change.
    bool configure(const AudioFormat &format, NormalizationMode mode, QString *error);
    NormalizationMode mode() const noexcept { return m_mode; }

    // Push one packed-float32 buffer; append zero or more normalized buffers (a filter may buffer).
    bool process(const AudioBuffer &input, std::vector<AudioBuffer> *outputs, QString *error);
    // Drain remaining buffered output (end of stream).
    bool drain(std::vector<AudioBuffer> *outputs, QString *error);
    // Reset filter state on seek/track change without changing the mode.
    void flush();

    // Honest buffered latency: samples pushed but not yet emitted, expressed in microseconds.
    qint64 reportedLatencyUs() const noexcept;

private:
    void teardown();
    bool pullOutputs(std::vector<AudioBuffer> *outputs, QString *error);

    NormalizationMode m_mode = NormalizationMode::Smooth;
    AudioFormat m_format;
    AVFilterGraph *m_graph = nullptr;
    AVFilterContext *m_source = nullptr;
    AVFilterContext *m_sink = nullptr;
    AVFrame *m_frame = nullptr;
    // Written by the decode thread, read by the GUI via reportedLatencyUs(); atomic to avoid a
    // data race on the diagnostic.
    std::atomic<qint64> m_pushedSamples{0};
    std::atomic<qint64> m_pulledSamples{0};
};

} // namespace Colosseum::Player2
