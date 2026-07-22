// Task 9 - typed loudness normalization stage. Pure CPU (FFmpeg avfilter); no device needed.

#include "player2/audio/AudioNormalizer.h"

#include <QtCore/QtGlobal>

#include <cmath>
#include <cstdlib>
#include <iostream>
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

AudioBuffer makeSine(const AudioFormat &format, int frames, qint64 ptsUs, double freq = 440.0)
{
    AudioBuffer buffer;
    buffer.format = format;
    buffer.frameCount = frames;
    buffer.ptsUs = ptsUs;
    buffer.bytes.resize(frames * format.channels * static_cast<int>(sizeof(float)));
    auto *samples = reinterpret_cast<float *>(buffer.bytes.data());
    for (int i = 0; i < frames; ++i) {
        const float value = static_cast<float>(
            0.25 * std::sin(2.0 * 3.14159265358979 * freq * i / format.sampleRate));
        for (int c = 0; c < format.channels; ++c)
            samples[i * format.channels + c] = value;
    }
    return buffer;
}

bool allFinite(const AudioBuffer &buffer)
{
    const auto *samples = reinterpret_cast<const float *>(buffer.bytes.constData());
    const int count = buffer.frameCount * buffer.format.channels;
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(samples[i]))
            return false;
    }
    return true;
}

void smoothIsBitTransparent(const AudioFormat &format)
{
    AudioNormalizer normalizer;
    QString error;
    require(normalizer.configure(format, NormalizationMode::Smooth, &error),
            "smooth configure failed: " + error.toStdString());
    const AudioBuffer input = makeSine(format, 1024, 12345);
    std::vector<AudioBuffer> outputs;
    require(normalizer.process(input, &outputs, &error), "smooth process failed");
    require(outputs.size() == 1, "smooth must emit exactly one buffer per input");
    require(outputs[0].bytes == input.bytes, "smooth must be bit-transparent");
    require(outputs[0].ptsUs == input.ptsUs, "smooth must preserve pts");
    require(normalizer.reportedLatencyUs() == 0, "smooth must report zero latency");
}

void modesPreserveEndpointFormat(const AudioFormat &format, NormalizationMode mode,
                                 const std::string &label)
{
    AudioNormalizer normalizer;
    QString error;
    require(normalizer.configure(format, mode, &error),
            label + " configure failed: " + error.toStdString());
    // Push enough audio that a lookahead filter emits output.
    std::vector<AudioBuffer> outputs;
    qint64 pts = 0;
    for (int i = 0; i < 200; ++i) {
        const AudioBuffer input = makeSine(format, 1024, pts);
        pts += static_cast<qint64>(1024) * 1'000'000 / format.sampleRate;
        require(normalizer.process(input, &outputs, &error),
                label + " process failed: " + error.toStdString());
    }
    require(normalizer.drain(&outputs, &error), label + " drain failed");
    require(!outputs.empty(), label + " emitted no audio");
    for (const AudioBuffer &buffer : outputs) {
        require(buffer.format.sampleRate == format.sampleRate, label + " changed sample rate");
        require(buffer.format.channels == format.channels, label + " changed channel count");
        require(allFinite(buffer), label + " produced a non-finite sample");
    }
    require(normalizer.reportedLatencyUs() >= 0, label + " reported negative latency");
}

void liveModeChangeIsClean(const AudioFormat &format)
{
    AudioNormalizer normalizer;
    QString error;
    require(normalizer.configure(format, NormalizationMode::Full, &error), "full configure failed");
    std::vector<AudioBuffer> outputs;
    for (int i = 0; i < 32; ++i)
        require(normalizer.process(makeSine(format, 1024, i * 1000), &outputs, &error),
                "full process failed");
    // Switch to Smooth on the fly: passthrough must return exactly.
    require(normalizer.configure(format, NormalizationMode::Smooth, &error),
            "switch to smooth failed");
    outputs.clear();
    const AudioBuffer input = makeSine(format, 1024, 999);
    require(normalizer.process(input, &outputs, &error), "smooth-after-full process failed");
    require(outputs.size() == 1 && outputs[0].bytes == input.bytes,
            "switch back to Smooth must be bit-transparent");
    // And back to Light without a crash.
    require(normalizer.configure(format, NormalizationMode::Light, &error),
            "switch to light failed");
    outputs.clear();
    require(normalizer.process(makeSine(format, 1024, 1), &outputs, &error),
            "light-after-smooth process failed");
}

} // namespace

int main()
{
    const AudioFormat format{48'000, 2};
    try {
        smoothIsBitTransparent(format);
        modesPreserveEndpointFormat(format, NormalizationMode::Light, "light");
        modesPreserveEndpointFormat(format, NormalizationMode::Full, "full");
        liveModeChangeIsClean(format);
    } catch (const std::exception &error) {
        std::cerr << "player2_audio_normalizer_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_audio_normalizer_test: PASS\n";
    return EXIT_SUCCESS;
}
