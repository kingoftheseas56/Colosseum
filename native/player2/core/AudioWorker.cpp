#include "AudioWorker.h"

#include "player2/audio/AudioPipeline.h"
#include "player2/audio/WASAPIAudioSink.h" // AudioFormat
#include "Player2Types.h"                   // NormalizationMode

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

#include <chrono>
#include <thread>

namespace Colosseum::Player2 {
namespace {

QString avErr(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

bool benignGenerationTransition(const AudioWorker::Host *host, quint64 submittedGeneration)
{
    return host->cancelled() || host->commandPending() ||
        submittedGeneration != host->activeGeneration();
}

} // namespace

AudioWorker::AudioWorker(AudioPipeline *pipeline, const AVCodecParameters *codecpar,
                         AVRational timeBase, PacketQueue *queue, Host *host, quint64 generation)
    : m_pipeline(pipeline)
    , m_codecpar(codecpar)
    , m_timeBaseNum(timeBase.num)
    , m_timeBaseDen(timeBase.den)
    , m_queue(queue)
    , m_host(host)
    , m_generation(generation)
{
}

AudioWorker::~AudioWorker()
{
    stop();
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_decoder)
        avcodec_free_context(&m_decoder);
}

bool AudioWorker::start(QString *error)
{
    const AVCodec *codec = avcodec_find_decoder(m_codecpar->codec_id);
    if (!codec) {
        if (error)
            *error = QStringLiteral("No audio decoder for the selected stream");
        return false;
    }
    m_decoder = avcodec_alloc_context3(codec);
    int rc = 0;
    if (!m_decoder || (rc = avcodec_parameters_to_context(m_decoder, m_codecpar)) < 0 ||
        (rc = avcodec_open2(m_decoder, codec, nullptr)) < 0) {
        if (error)
            *error = QStringLiteral("Audio decoder setup failed: %1").arg(avErr(rc));
        return false;
    }
    m_frame = av_frame_alloc();
    if (!m_frame) {
        if (error)
            *error = QStringLiteral("Could not allocate an audio decode frame");
        return false;
    }
    QString openError;
    if (!m_pipeline->open(AudioFormat{48'000, 2}, &openError)) {
        if (error)
            *error = openError;
        return false;
    }
    m_thread = std::thread(&AudioWorker::run, this);
    return true;
}

void AudioWorker::stop()
{
    if (m_queue)
        m_queue->cancel();
    if (m_thread.joinable())
        m_thread.join();
}

void AudioWorker::setNormalizationMode(int mode)
{
    m_pendingNormalization.store(mode, std::memory_order_release);
    if (m_queue)
        m_queue->interrupt(); // in case the worker is blocked in pop with an empty queue mid-pause
}

void AudioWorker::setSpeed(double speed)
{
    m_pendingSpeed.store(speed, std::memory_order_release);
    if (m_queue)
        m_queue->interrupt(); // in case the worker is blocked in pop with an empty queue mid-pause
}

void AudioWorker::reconfigure(const AVCodecParameters *codecpar, AVRational timeBase,
                              quint64 generation)
{
    m_pendingReconfigTimeBase.store((static_cast<qint64>(timeBase.num) << 32) |
                                        (static_cast<qint64>(timeBase.den) & 0xffffffff),
                                    std::memory_order_release);
    m_pendingReconfigGeneration.store(generation, std::memory_order_release);
    m_pendingReconfig.store(codecpar, std::memory_order_release); // publish last (arms the swap)
}

bool AudioWorker::receiveFrames(quint64 generation)
{
    const AVRational timeBase{static_cast<int>(m_timeBaseNum), static_cast<int>(m_timeBaseDen)};
    while (!m_host->cancelled()) {
        const int rc = avcodec_receive_frame(m_decoder, m_frame);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            m_failureMessage = QStringLiteral("Audio decode failed: %1").arg(avErr(rc));
            return false;
        }
        qint64 ptsUs = 0;
        if (m_frame->best_effort_timestamp != AV_NOPTS_VALUE)
            ptsUs = av_rescale_q(m_frame->best_effort_timestamp, timeBase, AVRational{1, 1'000'000});

        if (m_host->decodingToTarget() && m_host->audioIsMaster()) {
            if (ptsUs + 1'000 < m_host->seekTargetUs()) {
                av_frame_unref(m_frame); // discard audio before the seek target
                continue;
            }
            m_host->onSeekLanded(ptsUs); // audio is master: this frame is the landing point
        }
        if (!m_host->paused() && !m_host->decodingToTarget()) {
            QString submitError;
            // The pts reported to the master clock carries the A/V offset; the true position
            // (host's lastMasterPts, seek landing) stays unshifted.
            const qint64 clockPtsUs = ptsUs + m_host->audioDelayUs();
            if (!m_pipeline->submitDecodedFrame(m_frame, clockPtsUs, generation, &submitError)) {
                if (benignGenerationTransition(m_host, generation)) {
                    av_frame_unref(m_frame);
                    return true; // benign: a cancel, or a flush/seek/track-switch, is in flight
                }
                m_failureMessage = submitError;
                return false;
            }
            if (m_host->audioIsMaster())
                m_host->onAudioPositionAdvanced(ptsUs);
        }
        av_frame_unref(m_frame);
    }
    return true;
}

void AudioWorker::run()
{
    quint64 activeGen = m_generation;
    bool drained = false;
    AVPacket *packet = av_packet_alloc();

    while (!m_host->cancelled()) {
        // The worker owns the normalizer: apply a posted mode change here, never from the demux.
        const int normalization = m_pendingNormalization.exchange(-1, std::memory_order_acq_rel);
        if (normalization >= 0)
            m_pipeline->configureNormalization(static_cast<NormalizationMode>(normalization));

        // The worker owns the tempo stage: apply a posted speed change here, never from the demux.
        const double pendingSpeed = m_pendingSpeed.exchange(-1.0, std::memory_order_acq_rel);
        if (pendingSpeed > 0.0)
            m_pipeline->configureTempo(pendingSpeed);

        quint64 packetGen = 0;
        bool discontinuity = false;
        const PacketQueue::PopResult result = m_queue->pop(packet, &packetGen, &discontinuity);
        if (result == PacketQueue::PopResult::Cancelled)
            break;
        if (result == PacketQueue::PopResult::EndOfStream) {
            if (!drained) {
                avcodec_send_packet(m_decoder, nullptr); // flush the decoder tail
                receiveFrames(activeGen);
                QString drainError;
                m_pipeline->drain(activeGen, &drainError); // drain loudnorm's lookahead to the sink
                if (m_host->decodingToTarget() && m_host->audioIsMaster())
                    m_host->onSeekLanded(m_host->seekTargetUs()); // landed at the edge
                m_reachedEnd.store(true, std::memory_order_release);
                drained = true;
            }
            // Wait quietly at the end until a seek refills the queue (flush clears end-of-stream) or
            // cancel arrives — do not busy-spin re-draining.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        drained = false;
        m_reachedEnd.store(false, std::memory_order_release);

        // A generation change means a seek (flush the decoder) or a track switch (rebuild it for the
        // new codec). Applying the rebuild HERE, at the boundary, guarantees the new decoder is in
        // place before any new-generation packet is decoded — the demux never touches the decoder.
        if (packetGen != activeGen || discontinuity) {
            const AVCodecParameters *reconfig = m_pendingReconfig.load(std::memory_order_acquire);
            if (reconfig &&
                m_pendingReconfigGeneration.load(std::memory_order_acquire) == packetGen) {
                m_pendingReconfig.store(nullptr, std::memory_order_release);
                const qint64 packed = m_pendingReconfigTimeBase.load(std::memory_order_acquire);
                m_timeBaseNum = packed >> 32;
                m_timeBaseDen = static_cast<qint32>(packed & 0xffffffff);
                const AVCodec *codec = avcodec_find_decoder(reconfig->codec_id);
                AVCodecContext *rebuilt = codec ? avcodec_alloc_context3(codec) : nullptr;
                if (rebuilt && avcodec_parameters_to_context(rebuilt, reconfig) >= 0 &&
                    avcodec_open2(rebuilt, codec, nullptr) >= 0) {
                    avcodec_free_context(&m_decoder);
                    m_decoder = rebuilt;
                } else if (rebuilt) {
                    avcodec_free_context(&rebuilt); // keep the old decoder if the swap failed
                    avcodec_flush_buffers(m_decoder);
                }
            } else {
                avcodec_flush_buffers(m_decoder);
            }
            m_pipeline->flushFilters();
            activeGen = packetGen;
        }

        const int sendResult = avcodec_send_packet(m_decoder, packet);
        av_packet_unref(packet);
        if (sendResult < 0 || !receiveFrames(activeGen)) {
            // The command flag may already be cleared by Demux before an in-flight stale write
            // returns. The active generation is the durable transition authority.
            if (!benignGenerationTransition(m_host, activeGen)) {
                if (m_failureMessage.isEmpty())
                    m_failureMessage =
                        QStringLiteral("Audio packet submission failed: %1").arg(avErr(sendResult));
                m_failed.store(true, std::memory_order_release);
                m_queue->cancel();
                break;
            }
        }
    }

    av_packet_free(&packet);
}

} // namespace Colosseum::Player2
