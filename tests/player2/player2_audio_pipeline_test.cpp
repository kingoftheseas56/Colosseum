#include "player2/audio/AudioPipeline.h"
#include "player2/audio/WASAPIAudioSink.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class FakeAudioSink final : public IAudioSink
{
public:
    bool open(const AudioFormat &format, QString *) override
    {
        openedFormat = format;
        return true;
    }
    int write(const AudioBuffer &buffer, quint64 generation, QString *error) override
    {
        if (generation != acceptedGeneration) {
            if (error)
                *error = QStringLiteral("generation rejected");
            return 0;
        }
        buffers.push_back(buffer);
        return buffer.frameCount;
    }
    AudioClockSnapshot clock() const override { return {}; }
    void flush(quint64 generation) override
    {
        acceptedGeneration = generation;
        buffers.clear();
    }
    void setVolume(float) override {}
    void setMuted(bool) override {}
    int queueDepthFrames() const override { return 0; }
    QString deviceName() const override { return QStringLiteral("fake"); }
    quint64 underrunCount() const override { return 0; }

    AudioFormat openedFormat;
    quint64 acceptedGeneration = 1;
    std::vector<AudioBuffer> buffers;
};

struct FrameDeleter
{
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};

std::unique_ptr<AVFrame, FrameDeleter> makeStereoFloatFrame(int sampleRate, int frames,
                                                            qint64 pts)
{
    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    frame->format = AV_SAMPLE_FMT_FLT;
    frame->sample_rate = sampleRate;
    frame->nb_samples = frames;
    frame->pts = pts;
    av_channel_layout_default(&frame->ch_layout, 2);
    require(av_frame_get_buffer(frame.get(), 0) >= 0, "audio frame allocation failed");
    auto *samples = reinterpret_cast<float *>(frame->data[0]);
    for (int i = 0; i < frames; ++i) {
        const float value = std::sin(static_cast<float>(i) * 0.05f) * 0.25f;
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    return frame;
}

void exactPassthroughAndTimestamp()
{
    FakeAudioSink sink;
    AudioPipeline pipeline(&sink);
    QString error;
    require(pipeline.open(AudioFormat{48'000, 2}, &error), error.toStdString());
    auto frame = makeStereoFloatFrame(48'000, 480, 0);
    require(pipeline.submitDecodedFrame(frame.get(), 125'000, 1, &error), error.toStdString());
    require(sink.buffers.size() == 1, "passthrough produced the wrong buffer count");
    require(sink.buffers[0].frameCount == 480, "passthrough changed sample count");
    require(sink.buffers[0].ptsUs == 125'000, "passthrough changed timestamp");
    require(sink.buffers[0].bytes.size() == 480 * 2 * static_cast<int>(sizeof(float)),
            "passthrough byte count is wrong");
}

void resampleHasExactLongRunCount()
{
    FakeAudioSink sink;
    AudioPipeline pipeline(&sink);
    QString error;
    require(pipeline.open(AudioFormat{48'000, 2}, &error), error.toStdString());
    auto frame = makeStereoFloatFrame(44'100, 44'100, 0);
    require(pipeline.submitDecodedFrame(frame.get(), 0, 1, &error), error.toStdString());
    require(pipeline.drain(1, &error), error.toStdString());
    int frames = 0;
    for (const AudioBuffer &buffer : sink.buffers)
        frames += buffer.frameCount;
    require(frames == 48'000, "44.1 kHz to 48 kHz did not produce exactly one second");
}

void queueFlushUnderrunAndGenerationRules()
{
    AudioBufferQueue queue(16);
    queue.flush(7);
    AudioBuffer buffer;
    buffer.format = AudioFormat{48'000, 2};
    buffer.frameCount = 8;
    buffer.ptsUs = 10'000;
    buffer.bytes.resize(buffer.frameCount * buffer.format.channels * sizeof(float));
    require(queue.enqueue(buffer, 7), "current generation enqueue failed");

    std::vector<float> output(12 * 2, 1.0f);
    const int copied = queue.read(output.data(), 12, 2);
    require(copied == 8, "queue copied the wrong number of frames");
    require(queue.underrunCount() == 1, "short read did not count an underrun");
    require(output.back() == 0.0f, "underrun tail was not silenced");

    queue.flush(8);
    require(queue.depthFrames() == 0, "flush did not clear queued audio");
    require(queue.underrunCount() == 0, "flush did not reset per-generation underruns");
    require(!queue.enqueue(buffer, 7), "stale generation audio was accepted");
    require(queue.enqueue(buffer, 8), "current generation audio was rejected");
}

void sinkRejectionPropagates()
{
    FakeAudioSink sink;
    AudioPipeline pipeline(&sink);
    QString error;
    require(pipeline.open(AudioFormat{48'000, 2}, &error), error.toStdString());
    sink.flush(2);
    auto frame = makeStereoFloatFrame(48'000, 48, 0);
    require(!pipeline.submitDecodedFrame(frame.get(), 0, 1, &error),
            "stale audio submission unexpectedly succeeded");
    require(error.contains(QStringLiteral("generation")),
            "generation rejection did not propagate a useful error");
}

void boundedQueueAppliesBackpressureAndFlushCancelsIt()
{
    using namespace std::chrono_literals;
    AudioBufferQueue queue(8);
    queue.flush(7);
    AudioBuffer buffer;
    buffer.format = AudioFormat{48'000, 2};
    buffer.frameCount = 8;
    buffer.bytes.resize(buffer.frameCount * buffer.format.channels * sizeof(float));
    require(queue.enqueue(buffer, 7), "initial queue fill failed");

    auto blockedWrite = std::async(std::launch::async, [&] {
        return queue.enqueueBlocking(buffer, 7);
    });
    require(blockedWrite.wait_for(30ms) == std::future_status::timeout,
            "full queue did not backpressure its producer");
    std::vector<float> output(8 * 2);
    require(queue.read(output.data(), 8, 2) == 8, "queue did not release capacity");
    require(blockedWrite.wait_for(250ms) == std::future_status::ready && blockedWrite.get(),
            "consumer progress did not release the blocked producer");

    auto staleWrite = std::async(std::launch::async, [&] {
        return queue.enqueueBlocking(buffer, 7);
    });
    require(staleWrite.wait_for(30ms) == std::future_status::timeout,
            "second full queue did not backpressure its producer");
    queue.flush(8);
    require(staleWrite.wait_for(250ms) == std::future_status::ready && !staleWrite.get(),
            "generation flush did not cancel the blocked stale producer");
}

} // namespace

int main()
{
    try {
        exactPassthroughAndTimestamp();
        resampleHasExactLongRunCount();
        queueFlushUnderrunAndGenerationRules();
        boundedQueueAppliesBackpressureAndFlushCancelsIt();
        sinkRejectionPropagates();
    } catch (const std::exception &error) {
        std::cerr << "player2_audio_pipeline_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_audio_pipeline_test: PASS\n";
    return EXIT_SUCCESS;
}
