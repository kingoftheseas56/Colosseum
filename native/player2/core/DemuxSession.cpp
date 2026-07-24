#include "DemuxSession.h"

#include "player2/video/D3D11VideoPipeline.h"
#include "player2/audio/AudioPipeline.h"
#include "player2/network/QtHttpTransport.h"
#include "AudioWorker.h"
#include "FrameScheduler.h"
#include "PacketQueue.h"
#include "PlaybackClock.h"
#include "SubtitlePipeline.h"

#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}

#include <QtCore/QMetaObject>

#include <algorithm>
#include <chrono>
#include <memory>
#include <windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {
namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

QString mediaTypeName(AVMediaType type)
{
    switch (type) {
    case AVMEDIA_TYPE_VIDEO: return QStringLiteral("video");
    case AVMEDIA_TYPE_AUDIO: return QStringLiteral("audio");
    case AVMEDIA_TYPE_SUBTITLE: return QStringLiteral("subtitle");
    case AVMEDIA_TYPE_DATA: return QStringLiteral("data");
    case AVMEDIA_TYPE_ATTACHMENT: return QStringLiteral("attachment");
    default: return QStringLiteral("unknown");
    }
}

QString dictionaryValue(AVDictionary *dictionary, const char *key)
{
    const AVDictionaryEntry *entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value) : QString();
}

struct FormatCloser
{
    void operator()(AVFormatContext *context) const
    {
        if (context)
            avformat_close_input(&context);
    }
};

struct PacketCloser
{
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};

struct CodecCloser
{
    void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
};

struct FrameCloser
{
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};

struct BufferCloser
{
    void operator()(AVBufferRef *buffer) const { av_buffer_unref(&buffer); }
};

AVPixelFormat selectD3d11Format(AVCodecContext *, const AVPixelFormat *formats)
{
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == AV_PIX_FMT_D3D11)
            return *format;
    }
    return AV_PIX_FMT_NONE;
}

qint64 qpcNow()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

} // namespace

DemuxSession::DemuxSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DemuxEndReason>();
    qRegisterMetaType<DemuxStreamInfo>();
    qRegisterMetaType<DemuxMetadata>();
    qRegisterMetaType<DemuxPacketInfo>();
    qRegisterMetaType<SubtitleCue>();
}

DemuxSession::~DemuxSession()
{
    cancel();
}

int DemuxSession::interrupt(void *opaque)
{
    const auto *session = static_cast<const DemuxSession *>(opaque);
    return session && session->m_cancelled.load(std::memory_order_acquire) ? 1 : 0;
}

int DemuxSession::avioRead(void *opaque, uint8_t *buffer, int size)
{
    auto *source = static_cast<HttpMediaSource *>(opaque);
    const int read = source->read(reinterpret_cast<char *>(buffer), size);
    if (read > 0)
        return read;
    if (read == 0)
        return AVERROR_EOF;
    return AVERROR_EXIT; // cancelled or a terminal network failure
}

int64_t DemuxSession::avioSeek(void *opaque, int64_t offset, int whence)
{
    auto *source = static_cast<HttpMediaSource *>(opaque);
    const qint64 result = source->seek(offset, static_cast<int>(whence));
    return result < 0 ? qint64{AVERROR(EINVAL)} : result;
}

void DemuxSession::open(const PlaybackRequest &request, quint64 generation)
{
    cancel();
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
    }
    m_commandPending.store(false, std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
    m_activeGeneration.store(generation, std::memory_order_release);
    m_cancelled.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    std::scoped_lock lock(m_workerMutex);
    m_worker = std::thread(&DemuxSession::run, this, request, generation);
}

void DemuxSession::cancel()
{
    m_cancelled.store(true, std::memory_order_release);
    m_commandCv.notify_all();
    {
        // Unblock a demux blocked pushing into a full audio queue so it observes the cancel and exits.
        std::scoped_lock queueLock(m_audioQueueMutex);
        if (m_audioQueueForInterrupt)
            m_audioQueueForInterrupt->interrupt();
    }
    {
        std::scoped_lock queueLock(m_videoQueueMutex);
        if (m_videoQueueForInterrupt)
            m_videoQueueForInterrupt->interrupt();
    }
    // NOTE: do NOT flush the audio sink here. cancel() is also called at the top of open() as
    // pre-run cleanup, and flushing would stamp the sink with the previous generation right before
    // the new run's first write, which the sink then rejects ("Audio generation rejected"). The
    // enqueueBlocking-teardown wake is handled by the sink's own flush on close()/reopen instead.
    // Unblock a blocked AVIO read so the worker can observe the cancel and exit; the shared_ptr copy
    // keeps the source alive across this call even as the worker tears it down.
    std::shared_ptr<HttpMediaSource> source;
    {
        std::scoped_lock lock(m_httpMutex);
        source = m_httpSource;
    }
    if (source)
        source->cancel();
    joinWorker();
}

void DemuxSession::enqueueCommand(const Command &command)
{
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.push_back(command);
    }
    m_commandPending.store(true, std::memory_order_release);
    m_commandCv.notify_all();
    // Wake a demux blocked on either worker queue so it breaks out and services this command.
    {
        std::scoped_lock queueLock(m_audioQueueMutex);
        if (m_audioQueueForInterrupt)
            m_audioQueueForInterrupt->interrupt();
    }
    {
        std::scoped_lock queueLock(m_videoQueueMutex);
        if (m_videoQueueForInterrupt)
            m_videoQueueForInterrupt->interrupt();
    }
}

void DemuxSession::requestSeek(qint64 targetUs, quint64 generation, bool resumePlaying)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::Seek;
    command.targetUs = targetUs;
    command.generation = generation;
    command.resumePlaying = resumePlaying;
    enqueueCommand(command);
}

void DemuxSession::requestFrameStep(int frames, quint64 generation)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::FrameStep;
    command.frames = frames;
    command.generation = generation;
    command.resumePlaying = false;
    enqueueCommand(command);
}

void DemuxSession::requestSelectAudioTrack(int streamIndex, quint64 generation)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::SelectAudioTrack;
    command.streamIndex = streamIndex;
    command.generation = generation;
    enqueueCommand(command);
}

void DemuxSession::requestSelectSubtitleTrack(int streamIndex)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::SelectSubtitleTrack;
    command.streamIndex = streamIndex;
    enqueueCommand(command);
}

void DemuxSession::requestNormalizationMode(int mode)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::Normalization;
    command.normalizationMode = mode;
    enqueueCommand(command);
}

void DemuxSession::requestSpeed(double speed)
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::Speed;
    command.speed = speed;
    enqueueCommand(command);
}

void DemuxSession::requestPause()
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::Pause;
    enqueueCommand(command);
}

void DemuxSession::requestResume()
{
    if (!running())
        return;
    Command command;
    command.type = CommandType::Resume;
    enqueueCommand(command);
}

void DemuxSession::setAudioDelay(qint64 delayUs) noexcept
{
    m_audioDelayUs.store(delayUs, std::memory_order_release);
}

void DemuxSession::setVideoPipeline(D3D11VideoPipeline *pipeline) noexcept
{
    m_videoPipeline.store(pipeline, std::memory_order_release);
}

void DemuxSession::setAudioPipeline(AudioPipeline *pipeline) noexcept
{
    m_audioPipeline.store(pipeline, std::memory_order_release);
}

void DemuxSession::setTiming(PlaybackClock *clock, FrameScheduler *scheduler) noexcept
{
    m_playbackClock.store(clock, std::memory_order_release);
    m_frameScheduler.store(scheduler, std::memory_order_release);
}

void DemuxSession::joinWorker()
{
    std::thread worker;
    {
        std::scoped_lock lock(m_workerMutex);
        if (m_worker.joinable())
            worker = std::move(m_worker);
    }
    if (worker.joinable())
        worker.join();
    m_running.store(false, std::memory_order_release);
}

bool DemuxSession::running() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

void DemuxSession::run(PlaybackRequest request, quint64 generation)
{
    if (m_cancelled.load(std::memory_order_acquire)) {
        postEnded(generation, DemuxEndReason::Cancelled,
                  Player2Error{Player2ErrorCode::Cancelled, QStringLiteral("Open cancelled"), true});
        m_running.store(false, std::memory_order_release);
        return;
    }

    AVFormatContext *rawFormat = avformat_alloc_context();
    if (!rawFormat) {
        postEnded(generation, DemuxEndReason::Failed,
                  Player2Error{Player2ErrorCode::OpenFailed,
                               QStringLiteral("Could not allocate demux context"), false});
        m_running.store(false, std::memory_order_release);
        return;
    }
    rawFormat->interrupt_callback = AVIOInterruptCB{&DemuxSession::interrupt, this};

    // Streamed HTTP(S) sources route through our own transport (HttpMediaSource) so WE own buffering
    // truth, ranged seeks, reconnect and header redaction, surfaced to FFmpeg via a custom AVIO.
    std::shared_ptr<HttpMediaSource> httpSource;
    AVIOContext *avio = nullptr;
    const bool useHttpSource =
        !request.source.isLocalFile() && HttpMediaSource::isHttpUrl(request.source);
    if (useHttpSource) {
        httpSource = std::make_shared<HttpMediaSource>(std::make_unique<QtHttpTransport>(), request);
        httpSource->setStateCallback([this](NetworkState state) { postNetworkState(state); });
        QString openError;
        if (!httpSource->open(&openError)) {
            avformat_free_context(rawFormat);
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::NetworkFailed,
                                   QStringLiteral("Could not open stream: %1").arg(openError), true});
            m_running.store(false, std::memory_order_release);
            return;
        }
        {
            std::scoped_lock lock(m_httpMutex);
            m_httpSource = httpSource;
        }
        constexpr int kAvioBufferSize = 64 * 1024;
        auto *buffer = static_cast<unsigned char *>(av_malloc(kAvioBufferSize));
        avio = avio_alloc_context(buffer, kAvioBufferSize, 0, httpSource.get(),
                                  &DemuxSession::avioRead, nullptr,
                                  httpSource->capabilities().seekable ? &DemuxSession::avioSeek
                                                                      : nullptr);
        rawFormat->pb = avio;
        rawFormat->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
    // Free the custom AVIO and release the streaming source on every exit path. It is declared before
    // `format` so it tears down AFTER avformat_close_input, and after the shared source it references.
    struct HttpTeardown
    {
        DemuxSession *self;
        AVIOContext **avio;
        ~HttpTeardown()
        {
            if (*avio) {
                av_freep(&(*avio)->buffer);
                avio_context_free(avio);
            }
            std::scoped_lock lock(self->m_httpMutex);
            self->m_httpSource.reset();
        }
    } httpTeardown{this, &avio};

    const QByteArray location = request.source.isLocalFile()
        ? request.source.toLocalFile().toUtf8() : request.source.toString().toUtf8();
    int result = avformat_open_input(&rawFormat, useHttpSource ? nullptr : location.constData(),
                                     nullptr, nullptr);
    std::unique_ptr<AVFormatContext, FormatCloser> format(rawFormat);
    if (result < 0) {
        const bool cancelled = m_cancelled.load(std::memory_order_acquire);
        postEnded(generation, cancelled ? DemuxEndReason::Cancelled : DemuxEndReason::Failed,
                  Player2Error{cancelled ? Player2ErrorCode::Cancelled
                                         : Player2ErrorCode::OpenFailed,
                               cancelled ? QStringLiteral("Open cancelled")
                                         : QStringLiteral("Could not open media: %1").arg(avError(result)),
                               cancelled});
        m_running.store(false, std::memory_order_release);
        return;
    }
    if ((result = avformat_find_stream_info(format.get(), nullptr)) < 0) {
        const bool cancelled = m_cancelled.load(std::memory_order_acquire);
        postEnded(generation, cancelled ? DemuxEndReason::Cancelled : DemuxEndReason::Failed,
                  Player2Error{cancelled ? Player2ErrorCode::Cancelled
                                         : Player2ErrorCode::OpenFailed,
                               cancelled ? QStringLiteral("Open cancelled")
                                         : QStringLiteral("Stream discovery failed: %1")
                                               .arg(avError(result)),
                               cancelled});
        m_running.store(false, std::memory_order_release);
        return;
    }

    DemuxMetadata metadata;
    metadata.durationUs = format->duration == AV_NOPTS_VALUE ? 0 : format->duration;
    metadata.chapterCount = static_cast<int>(format->nb_chapters);
    AVDictionaryEntry *tag = nullptr;
    while ((tag = av_dict_get(format->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        metadata.tags.insert(QString::fromUtf8(tag->key), QString::fromUtf8(tag->value));
    for (unsigned int i = 0; i < format->nb_streams; ++i) {
        AVStream *stream = format->streams[i];
        DemuxStreamInfo info;
        info.index = static_cast<int>(i);
        info.type = mediaTypeName(stream->codecpar->codec_type);
        info.codec = QString::fromUtf8(avcodec_get_name(stream->codecpar->codec_id));
        info.language = dictionaryValue(stream->metadata, "language");
        info.title = dictionaryValue(stream->metadata, "title");
        info.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
        info.isForced = (stream->disposition & AV_DISPOSITION_FORCED) != 0;
        metadata.streams.append(info);
    }
    // Typed, immutable chapter rows (parity with the current player's mpv.chapters).
    for (unsigned int i = 0; i < format->nb_chapters; ++i) {
        const AVChapter *chapter = format->chapters[i];
        DemuxChapter row;
        row.index = static_cast<int>(i);
        row.startUs = av_rescale_q(chapter->start, chapter->time_base, AVRational{1, 1'000'000});
        row.endUs = av_rescale_q(chapter->end, chapter->time_base, AVRational{1, 1'000'000});
        row.title = dictionaryValue(chapter->metadata, "title");
        metadata.chapters.append(row);
    }
    postOpened(generation, metadata);

    // Video is DISCOVERED here (it needs the format context) but DECODED on its own thread. The
    // video decoder, its D3D11 pipeline and the frame scheduler live entirely on a dedicated thread
    // fed a bounded queue of video packets. Audio has its own worker too, so a full-but-on-time video
    // queue may safely backpressure demux; only measured lateness authorizes dropping forward.
    D3D11VideoPipeline *pipeline = m_videoPipeline.load(std::memory_order_acquire);
    int videoStreamIndex = -1;
    AVStream *videoStream = nullptr;
    const AVCodec *videoCodec = nullptr;
    double framesPerSecond = 24.0;
    if (pipeline) {
        videoStreamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                               &videoCodec, 0);
        if (videoStreamIndex < 0 || !videoCodec) {
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::DecodeFailed,
                                   QStringLiteral("No decodable video stream"), false});
            m_running.store(false, std::memory_order_release);
            return;
        }
        videoStream = format->streams[videoStreamIndex];
        const AVRational guessedRate = av_guess_frame_rate(format.get(), videoStream, nullptr);
        if (guessedRate.num > 0 && guessedRate.den > 0)
            framesPerSecond = av_q2d(guessedRate);
    }

    AudioPipeline *audioPipeline = m_audioPipeline.load(std::memory_order_acquire);
    int audioStreamIndex = -1;
    AVStream *audioStream = nullptr;
    const AVCodecParameters *audioCodecpar = nullptr;
    if (audioPipeline) {
        const AVCodec *audioCodec = nullptr;
        audioStreamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1,
                                               &audioCodec, 0);
        if (audioStreamIndex >= 0 && audioCodec) {
            audioStream = format->streams[audioStreamIndex];
            audioCodecpar = audioStream->codecpar;
        } else {
            audioStreamIndex = -1;
        }
    }
    const bool hasAudio = audioStream != nullptr;

    // The audio decode WORKER (the third thread) owns the audio decoder, frame, and every mutable
    // pipeline decode op end-to-end. The demux only PUSHES packets into this queue; blocking on the
    // small queue (pushInterruptible) paces the demux to real-time audio and the demux command loop
    // is never trapped. Created below — after landSeek is
    // defined, since the worker's Host reports the seek landing through it.
    // Sizing is NOT the video-stutter fix (measured): 1.5 s → decode ~6 fps, avDrift ~+40 ms mean;
    // 4 s → decode ~0.3 fps, avDrift ~+2534 ms mean. Enlarging the read-ahead pushed video FURTHER
    // ahead of the audio clock — so video runs systematically ahead (a clock-mastering regression),
    // not packet starvation. 2 s is a neutral middle pending that clock fix (handed to Codex).
    PacketQueue audioQueue(PacketQueue::Bounds{2'000'000, 48 * 1024 * 1024, 768, /*dropOldest*/ false});
    std::unique_ptr<AudioWorker> audioWorker;

    PlaybackClock *playbackClock = m_playbackClock.load(std::memory_order_acquire);
    FrameScheduler *frameScheduler = m_frameScheduler.load(std::memory_order_acquire);

    // Read by BOTH threads (the video thread consults it for clock mastering) and flipped by an
    // audio-track add, so it must be atomic. Fixed in practice: true whenever the file has audio.
    std::atomic<bool> audioIsMaster{hasAudio};
    const bool videoPresent = pipeline && videoStreamIndex >= 0;

    // ---- State shared between the demux thread (below) and the video decode thread ----
    // The single `gen` barrier still rules: it rides each queued packet, so the video thread adopts
    // it for tagging. A SEEK additionally bumps seekEpoch — that, not a bare generation change, is
    // what tells the video thread to flush its decoder + pipeline + scheduler and discard to the
    // target. So a plain generation bump from an audio/subtitle track swap never hitches video.
    std::atomic<quint64> seekEpoch{0};
    // Bumped whenever the audio path is flushed (seek OR audio-track change): it re-arms the video
    // thread's audio-master readiness barrier so video holds until the NEW audio becomes audible.
    std::atomic<quint64> audioPathEpoch{0};
    std::atomic<qint64> seekTargetUs{0};
    std::atomic<bool> seekResumePlaying{true};
    std::atomic<bool> decodingToTarget{false};
    std::atomic<qint64> lastMasterPtsUs{0};
    std::atomic<bool> audioMasterResync{false};
    std::atomic<bool> videoThreadFailed{false};
    std::atomic<bool> videoReachedEnd{false};
    QString videoFailureMessage; // published before videoThreadFailed is set (release/acquire)

    // Bounded video read-ahead with ADAPTIVE admission. A full queue is normal now that audio has an
    // independent worker: when its oldest packet is on-time/ahead of the active sink clock, demux
    // backpressures interruptibly and preserves presentation order. If that oldest packet is truly
    // late, admission drops a whole leading GOP to a keyframe and signals decoder discontinuity.
    // This keeps the historical overload escape hatch without the future-GOP feedback loop caused
    // by unconditional drop-oldest. The existing 8 s horizon is unchanged.
    PacketQueue videoQueue(
        PacketQueue::Bounds{8'000'000, 128 * 1024 * 1024, 1200, /*dropOldestWhenFull*/ true});
    if (videoPresent) {
        std::scoped_lock queueLock(m_videoQueueMutex);
        m_videoQueueForInterrupt = &videoQueue;
    }

    // The video decode thread: builds its own hardware decoder, then pops packets and paces frames
    // against the master clock. Joined at the end of run() — its captured references outlive it.
    std::thread videoThread;
    if (videoPresent) {
        videoThread = std::thread([&] {
            QString hardwareError;
            std::unique_ptr<AVBufferRef, BufferCloser> hardware(
                pipeline->createDecoderDeviceContext(&hardwareError));
            std::unique_ptr<AVCodecContext, CodecCloser> decoder(avcodec_alloc_context3(videoCodec));
            int rc = 0;
            if (!hardware || !decoder ||
                (rc = avcodec_parameters_to_context(decoder.get(), videoStream->codecpar)) < 0) {
                videoFailureMessage = hardwareError.isEmpty()
                    ? QStringLiteral("Video decoder setup failed: %1").arg(avError(rc))
                    : hardwareError;
                videoThreadFailed.store(true, std::memory_order_release);
                videoQueue.cancel();
                return;
            }
            decoder->hw_device_ctx = av_buffer_ref(hardware.get());
            decoder->get_format = selectD3d11Format;
            if ((rc = avcodec_open2(decoder.get(), videoCodec, nullptr)) < 0) {
                videoFailureMessage =
                    QStringLiteral("Hardware decoder open failed: %1").arg(avError(rc));
                videoThreadFailed.store(true, std::memory_order_release);
                videoQueue.cancel();
                return;
            }
            std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
            const AVRational videoTimeBase = videoStream->time_base;

            quint64 gen = generation;
            quint64 lastSeekEpoch = seekEpoch.load(std::memory_order_acquire);
            quint64 lastAudioPathEpoch = audioPathEpoch.load(std::memory_order_acquire);
            quint64 videoSequence = 0;
            bool discarding = false;
            bool landingPresented = false;
            bool audioMasterActive = false;
            // Tracks the audio clock's validity across frames so a valid->invalid->valid transition
            // (an underrun that emptied the sink, then refilled) is recognised as a RECOVERY: the
            // frozen master clock is stale, so we snap to the fresh audio position instead of crawling.
            bool audioWasValid = false;
            // Audio-master readiness barrier state. `audioReady` latches true once the sink produces a
            // real clock for the current audio-path epoch; until then video holds so it cannot lead
            // the silent loudnorm preroll. `barrierPresented` remembers we already showed one held
            // frame this epoch (the cold-start first frame or the seek landing frame).
            bool audioReady = false;
            bool barrierPresented = false;
            bool failed = false;

            // Drain and present every frame the decoder can emit for the packet just fed. Returns
            // false only on a real, non-benign decode failure.
            auto drainDecodedFrames = [&]() -> bool {
                while (!m_cancelled.load(std::memory_order_acquire)) {
                    const int r = avcodec_receive_frame(decoder.get(), frame.get());
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                        return true;
                    if (r < 0) {
                        videoFailureMessage =
                            QStringLiteral("Video decode failed: %1").arg(avError(r));
                        return false;
                    }
                    if (frame->format != AV_PIX_FMT_D3D11) {
                        videoFailureMessage =
                            QStringLiteral("Hardware decoder returned %1 instead of D3D11")
                                .arg(QString::fromLatin1(av_get_pix_fmt_name(
                                    static_cast<AVPixelFormat>(frame->format))));
                        return false;
                    }
                    pipeline->noteDecoded();
                    qint64 ptsUs =
                        static_cast<qint64>((videoSequence * 1'000'000.0) / framesPerSecond);
                    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                        ptsUs = av_rescale_q(frame->best_effort_timestamp, videoTimeBase,
                                             AVRational{1, 1'000'000});
                    }

                    // Re-arm the readiness barrier when the audio path was flushed (seek/track swap):
                    // the new audio must become audible before video may advance again.
                    const quint64 currentAudioPathEpoch =
                        audioPathEpoch.load(std::memory_order_acquire);
                    if (currentAudioPathEpoch != lastAudioPathEpoch) {
                        lastAudioPathEpoch = currentAudioPathEpoch;
                        audioReady = false;
                        barrierPresented = false;
                        audioMasterActive = false;
                    }

                    if (discarding) {
                        // Skip everything before the target; present only the first landing frame,
                        // then hold until the master (audio, if present) lands.
                        const qint64 target = seekTargetUs.load(std::memory_order_acquire);
                        if (ptsUs + 1'000 < target || landingPresented) {
                            ++videoSequence;
                            av_frame_unref(frame.get());
                            continue;
                        }
                        QString submitError;
                        const VideoFrameToken token{gen, ++videoSequence, ptsUs};
                        if (!pipeline->submitDecodedFrame(frame.get(), token, &submitError) &&
                            !submitError.isEmpty()) {
                            av_frame_unref(frame.get());
                            return true; // benign: a newer generation will re-drive presentation
                        }
                        av_frame_unref(frame.get());
                        landingPresented = true;
                        barrierPresented = true; // the landing frame is the held image for the barrier
                        if (!audioIsMaster) {
                            // Video-only: this frame is the landing point. Reset the clock, restore
                            // the requested pause state, publish completion.
                            decodingToTarget.store(false, std::memory_order_release);
                            lastMasterPtsUs.store(ptsUs, std::memory_order_release);
                            const bool pausing = !seekResumePlaying.load(std::memory_order_acquire);
                            m_paused.store(pausing, std::memory_order_release);
                            if (playbackClock) {
                                playbackClock->reset(ptsUs, qpcNow());
                                if (pausing)
                                    playbackClock->pause(qpcNow());
                            }
                            audioMasterActive = false;
                            postSeekCompleted(gen, ptsUs / 1'000'000.0);
                        }
                        discarding = false;
                        continue;
                    }

                    if (playbackClock && frameScheduler) {
                        qint64 now = qpcNow();
                        if (audioMasterResync.exchange(false, std::memory_order_acq_rel))
                            audioMasterActive = false;
                        const AudioClockSnapshot audio = (audioIsMaster && audioPipeline)
                            ? audioPipeline->clock() : AudioClockSnapshot{};
                        const quint64 expectedAudioGeneration =
                            m_activeGeneration.load(std::memory_order_acquire);
                        const bool audioClockCurrent =
                            audio.isValidForGeneration(expectedAudioGeneration);

                        // === Audio-master readiness barrier ===
                        // Until the sink produces a real clock for this audio-path epoch, HOLD video
                        // rather than let it lead the silent loudnorm/dynaudnorm preroll. Show one
                        // held frame, then wait interruptibly for the first valid sink clock and
                        // hard-reset to it — the truly-audible event. Video-only playback (below)
                        // skips the barrier and stays its own master.
                        if (audioIsMaster && !audioReady) {
                            if (!audioClockCurrent) {
                                if (!barrierPresented) {
                                    QString e;
                                    const VideoFrameToken t{gen, ++videoSequence, ptsUs};
                                    pipeline->submitDecodedFrame(frame.get(), t, &e);
                                    barrierPresented = true;
                                }
                                av_frame_unref(frame.get());
                                // Hold in place — do NOT decode/consume further frames — until the
                                // sink clock is real or an interruption arrives. The 8 s video queue
                                // is the preroll runway that absorbs this ~3 s wait while audio fills.
                                AudioClockSnapshot ready{};
                                while (!m_cancelled.load(std::memory_order_acquire) &&
                                       seekEpoch.load(std::memory_order_acquire) == lastSeekEpoch &&
                                       audioPathEpoch.load(std::memory_order_acquire) ==
                                           lastAudioPathEpoch &&
                                       audioIsMaster) {
                                    ready = audioPipeline ? audioPipeline->clock()
                                                          : AudioClockSnapshot{};
                                    if (ready.isValidForGeneration(expectedAudioGeneration))
                                        break;
                                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                                }
                                if (!ready.isValidForGeneration(expectedAudioGeneration))
                                    return true; // interrupted (cancel/seek/track) — re-drive fresh
                                const qint64 hnow = qpcNow();
                                const qint64 audioNow = ready.mediaPositionUs + static_cast<qint64>(
                                    static_cast<long double>(hnow - ready.qpcTimestamp) *
                                    1'000'000.0L / playbackClock->qpcFrequency());
                                playbackClock->reset(audioNow, hnow);
                                audioMasterActive = true;
                                audioReady = true;
                                continue; // the held frame is shown; advance to the next frame
                            }
                            // Audio already audible on the first check → hard-reset and authorize.
                            const qint64 audioNow = audio.mediaPositionUs + static_cast<qint64>(
                                static_cast<long double>(now - audio.qpcTimestamp) * 1'000'000.0L /
                                playbackClock->qpcFrequency());
                            playbackClock->reset(audioNow, now);
                            audioMasterActive = true;
                            audioReady = true;
                        } else if (audioClockCurrent) {
                            const qint64 audioNow = audio.mediaPositionUs + static_cast<qint64>(
                                static_cast<long double>(now - audio.qpcTimestamp) * 1'000'000.0L /
                                playbackClock->qpcFrequency());
                            if (!audioMasterActive) {
                                if (playbackClock->valid())
                                    playbackClock->correctToward(audioNow, now, 20'000);
                                else
                                    playbackClock->reset(audioNow, now);
                                audioMasterActive = true;
                            } else if (decideClockResync(audioWasValid,
                                                         audioNow - playbackClock->positionAt(now),
                                                         50'000) == ClockResync::HardReset) {
                                // Recovering from an underrun: snap across the gap so the picture does
                                // not lag audio while a 5 ms/frame slew crawls a multi-100 ms jump.
                                playbackClock->reset(audioNow, now);
                            } else {
                                playbackClock->correctToward(audioNow, now, 5'000);
                            }
                        } else if (!audioIsMaster && !playbackClock->valid()) {
                            playbackClock->reset(ptsUs, now); // video-only: video is its own master
                        }
                        // Current-path audio that is invalid means pause/underrun: keep frozen clock.
                        // A valid older-generation snapshot is ignored while a pre-flush callback
                        // finishes publishing; it cannot seed or resync the new audio epoch.
                        audioWasValid = audioClockCurrent;

                        // Real pause: freeze on this frame and HOLD. Without this the scheduler keeps
                        // firing wall-clock deadlines against the paused (frozen) clock, so the picture
                        // creeps forward through the buffered read-ahead instead of stopping. Present
                        // the current frame once, then wait interruptibly — resume, seek and cancel all
                        // break the hold so the transport stays responsive.
                        if (m_paused.load(std::memory_order_acquire) &&
                            !decodingToTarget.load(std::memory_order_acquire)) {
                            QString pauseError;
                            const VideoFrameToken held{gen, ++videoSequence, ptsUs};
                            pipeline->submitDecodedFrame(frame.get(), held, &pauseError);
                            av_frame_unref(frame.get());
                            while (m_paused.load(std::memory_order_acquire) &&
                                   !m_cancelled.load(std::memory_order_acquire) &&
                                   seekEpoch.load(std::memory_order_acquire) == lastSeekEpoch &&
                                   !decodingToTarget.load(std::memory_order_acquire)) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            }
                            continue;
                        }

                        const FrameScheduleDecision decision = frameScheduler->choose(
                            playbackClock->positionAt(now), now,
                            std::vector<FrameCandidate>{{videoSequence + 1, ptsUs}});
                        if (decision.action == FrameScheduleAction::WaitUntilQpc) {
                            while (!m_cancelled.load(std::memory_order_acquire)) {
                                if (seekEpoch.load(std::memory_order_acquire) != lastSeekEpoch)
                                    break; // a seek is in flight; stop pacing and abandon this frame
                                now = qpcNow();
                                if (now >= decision.deadlineQpc)
                                    break;
                                const qint64 remainingUs = static_cast<qint64>(
                                    static_cast<long double>(decision.deadlineQpc - now) *
                                    1'000'000.0L / playbackClock->qpcFrequency());
                                std::this_thread::sleep_for(std::chrono::microseconds(
                                    std::clamp<qint64>(remainingUs, 100, 5'000)));
                            }
                            if (seekEpoch.load(std::memory_order_acquire) != lastSeekEpoch) {
                                av_frame_unref(frame.get());
                                return true; // abandon; the seek path will re-drive presentation
                            }
                        } else if (decision.action == FrameScheduleAction::DropLate) {
                            pipeline->noteSchedulingDecision(decision.timingErrorUs, true);
                            ++videoSequence;
                            av_frame_unref(frame.get());
                            continue;
                        }
                        now = qpcNow();
                        pipeline->noteSchedulingDecision(ptsUs - playbackClock->positionAt(now),
                                                         false);
                    }
                    QString submitError;
                    const VideoFrameToken token{gen, ++videoSequence, ptsUs};
                    if (!pipeline->submitDecodedFrame(frame.get(), token, &submitError) &&
                        !submitError.isEmpty()) {
                        av_frame_unref(frame.get());
                        return true; // benign stale/flush
                    }
                    if (!audioIsMaster)
                        lastMasterPtsUs.store(ptsUs, std::memory_order_release);
                    av_frame_unref(frame.get());
                }
                return true;
            };

            std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
            while (!m_cancelled.load(std::memory_order_acquire) && !failed) {
                quint64 pktGen = gen;
                bool videoDiscontinuity = false;
                const PacketQueue::PopResult pop =
                    videoQueue.pop(pkt.get(), &pktGen, &videoDiscontinuity);
                if (pop == PacketQueue::PopResult::Cancelled)
                    break;
                if (pop == PacketQueue::PopResult::EndOfStream) {
                    // Flush the decoder tail, present what remains, then mark the video end so the
                    // demux thread publishes EndOfFile only once video has actually drained.
                    avcodec_send_packet(decoder.get(), nullptr);
                    drainDecodedFrames();
                    avcodec_flush_buffers(decoder.get());
                    videoReachedEnd.store(true, std::memory_order_release);
                    m_commandCv.notify_all(); // nudge the demux thread if it is awaiting the tail
                    continue; // keep waiting: a later seek flushes+refills, or cancel exits
                }

                videoReachedEnd.store(false, std::memory_order_release);
                const quint64 currentSeekEpoch = seekEpoch.load(std::memory_order_acquire);
                if (currentSeekEpoch != lastSeekEpoch) {
                    lastSeekEpoch = currentSeekEpoch;
                    avcodec_flush_buffers(decoder.get());
                    if (pipeline)
                        pipeline->flush(pktGen);
                    if (frameScheduler)
                        frameScheduler->reset();
                    discarding = decodingToTarget.load(std::memory_order_acquire);
                    landingPresented = false;
                    audioMasterActive = false;
                }
                gen = pktGen; // adopt the generation (covers both seeks and plain track changes)

                // The queue dropped stale backlog to let a slow video skip ahead to a keyframe; the
                // decoder's reference chain is broken across that gap, so flush before decoding it.
                if (videoDiscontinuity) {
                    avcodec_flush_buffers(decoder.get());
                    if (frameScheduler)
                        frameScheduler->reset();
                }

                const int sr = avcodec_send_packet(decoder.get(), pkt.get());
                av_packet_unref(pkt.get());
                if (sr < 0) {
                    // A stale packet right after a flush can be rejected; tolerate unless it is a hard
                    // failure with no seek in flight.
                    if (seekEpoch.load(std::memory_order_acquire) == lastSeekEpoch &&
                        sr != AVERROR(EAGAIN) && sr != AVERROR_INVALIDDATA) {
                        videoFailureMessage =
                            QStringLiteral("Video packet submission failed: %1").arg(avError(sr));
                        failed = true;
                    }
                    continue;
                }
                if (!drainDecodedFrames())
                    failed = true;
            }
            if (failed)
                videoThreadFailed.store(true, std::memory_order_release);
            // On ANY exit (cancel, teardown, or failure) cancel the queue so a demux thread blocked
            // in push() unblocks — otherwise the teardown join would deadlock once we stop draining.
            videoQueue.cancel();
        });
    }

    // Mutable transport state owned by the DEMUX thread. `gen` is the single generation it tags every
    // product with; a seek/track command advances it (and, for seeks, bumps seekEpoch above).
    quint64 gen = generation;
    SubtitlePipeline subtitlePipeline;
    int subtitleStreamIndex = -1;
    bool eofReached = false;
    bool eofSignaled = false;
    QString decodeFailure;

    // Publishes seek/frame-step completion, restores the requested play/pause state and re-arms the
    // clock at the landed position. Called once per reposition, from whichever stream is master.
    auto landSeek = [&](qint64 ptsUs) {
        decodingToTarget.store(false, std::memory_order_release);
        lastMasterPtsUs.store(ptsUs, std::memory_order_release);
        const bool pausing = !seekResumePlaying.load(std::memory_order_acquire);
        m_paused.store(pausing, std::memory_order_release);
        if (audioPipeline)
            audioPipeline->setPaused(pausing);
        // Do NOT start the playback clock here. This fires when audio is DECODED to the target, but
        // loudnorm still has to refill (~3 s) before it is AUDIBLE; seeding the clock now would let
        // video run ahead of silence (the same lead as cold start). The video thread's readiness
        // barrier hard-resets the clock on the first valid SINK clock — the truly-audible event.
        // We only publish that the seek landed and restore the requested pause state.
        audioMasterResync.store(true, std::memory_order_release);
        postSeekCompleted(gen, ptsUs / 1'000'000.0);
    };

    // Mandatory seek order: adopt the new generation, flush every active pipeline, flush the codecs,
    // seek the container, then decode to the target frame before presenting.
    auto applySeek = [&](qint64 targetUs, quint64 newGeneration, bool resumePlaying) {
        gen = newGeneration;
        m_activeGeneration.store(newGeneration, std::memory_order_release);
        if (hasAudio)
            audioQueue.flush(); // drop stale audio packets; the worker flushes its OWN decoder +
                                // filters when it next sees the new generation (it owns the decode).
        if (audioPipeline)
            audioPipeline->flush(newGeneration); // flush the sink (generation-gated)
        subtitlePipeline.flush(); // drop any decoder-buffered cue state across the seek
        if (playbackClock)
            playbackClock->invalidate();
        qint64 clamped = std::max<qint64>(0, targetUs);
        if (metadata.durationUs > 0)
            clamped = std::min(clamped, metadata.durationUs);
        // Publish the target BEFORE bumping seekEpoch/flushing the queue, so the video thread sees a
        // consistent target the moment it observes the seek. The video thread flushes its own
        // decoder/pipeline/scheduler and discards to the target on the seekEpoch bump.
        seekTargetUs.store(clamped, std::memory_order_release);
        seekResumePlaying.store(resumePlaying, std::memory_order_release);
        decodingToTarget.store(true, std::memory_order_release);
        audioMasterResync.store(true, std::memory_order_release);
        videoReachedEnd.store(false, std::memory_order_release);
        seekEpoch.fetch_add(1, std::memory_order_acq_rel);      // tell the video thread a seek happened
        audioPathEpoch.fetch_add(1, std::memory_order_acq_rel); // re-arm the audio readiness barrier
        videoQueue.flush();                                     // drop stale video packets in flight
        av_seek_frame(format.get(), -1, clamped, AVSEEK_FLAG_BACKWARD);
        eofReached = false;
        eofSignaled = false;
        // Scan to the target with the endpoint running; landSeek applies the final pause state.
        m_paused.store(false, std::memory_order_release);
        if (audioPipeline)
            audioPipeline->setPaused(false);
    };

    // Swap the decoded audio stream in place. Flush old audio, adopt the new generation, and let the
    // master clock re-establish; video is untouched so playback does not hitch.
    auto applyAudioTrack = [&](int streamIndex, quint64 newGeneration) {
        gen = newGeneration;
        m_activeGeneration.store(newGeneration, std::memory_order_release);
        if (audioPipeline)
            audioPipeline->flush(newGeneration); // sink flush; the worker flushes its filters on the new gen
        // The audio worker owns the decoder: post it a reconfigure for the new codec (it rebuilds the
        // decoder itself at the generation boundary) and repoint routing at the new stream. The demux
        // never touches the decoder. Requires a worker (a video-only file has none, so the swap no-ops).
        if (audioWorker && streamIndex >= 0 && streamIndex < static_cast<int>(format->nb_streams) &&
            format->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStream = format->streams[streamIndex];
            audioStreamIndex = streamIndex;
            audioIsMaster = true;
            audioWorker->reconfigure(audioStream->codecpar, audioStream->time_base, newGeneration);
            audioQueue.flush(); // drop stale packets so the worker adopts the new codec on the new gen
        }
        audioMasterResync.store(true, std::memory_order_release); // video re-locks after the swap
        audioPathEpoch.fetch_add(1, std::memory_order_acq_rel);   // re-arm the readiness barrier: the
        // new track must refill loudnorm before it is audible, so video holds until it is.
        // Report the track actually decoding now.
        postAudioTrackChanged(newGeneration, audioStreamIndex);
    };

    // Turn a subtitle stream on/off. -1 disables. Audio/video are untouched, so this keeps the
    // current generation (advancing it would desync the un-flushed audio queue). Cues are tagged
    // with the current generation and dropped by the next real seek.
    auto applySubtitleTrack = [&](int streamIndex) {
        subtitlePipeline.close();
        subtitleStreamIndex = -1;
        if (streamIndex >= 0 && streamIndex < static_cast<int>(format->nb_streams) &&
            format->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            QString subtitleError;
            if (subtitlePipeline.open(format->streams[streamIndex]->codecpar,
                                      format->streams[streamIndex]->time_base, &subtitleError))
                subtitleStreamIndex = streamIndex;
        }
        postSubtitleTrackChanged(gen, subtitleStreamIndex);
    };

    auto processCommands = [&]() {
        std::deque<Command> pending;
        {
            std::scoped_lock lock(m_commandMutex);
            pending.swap(m_commands);
        }
        for (const Command &command : pending) {
            switch (command.type) {
            case CommandType::Seek:
                applySeek(command.targetUs, command.generation, command.resumePlaying);
                break;
            case CommandType::FrameStep: {
                const double fps = framesPerSecond > 0.0 ? framesPerSecond : 24.0;
                const qint64 frameUs = static_cast<qint64>(1'000'000.0 / fps);
                applySeek(lastMasterPtsUs + static_cast<qint64>(command.frames) * frameUs,
                          command.generation, false);
                break;
            }
            case CommandType::SelectAudioTrack:
                applyAudioTrack(command.streamIndex, command.generation);
                break;
            case CommandType::SelectSubtitleTrack:
                applySubtitleTrack(command.streamIndex);
                break;
            case CommandType::Normalization:
                // The worker owns the normalizer; post the mode so it applies on its own thread.
                if (audioWorker)
                    audioWorker->setNormalizationMode(command.normalizationMode);
                else if (audioPipeline)
                    audioPipeline->configureNormalization(
                        static_cast<NormalizationMode>(command.normalizationMode));
                postAudioNormalizationChanged(gen, command.normalizationMode);
                break;
            case CommandType::Speed:
                // The worker owns the tempo stage; the clock rate must move with it so the master
                // prediction matches the now-faster audio between corrections. Resync so the tempo
                // discontinuity snaps cleanly instead of slewing.
                if (audioWorker)
                    audioWorker->setSpeed(command.speed);
                else if (audioPipeline)
                    audioPipeline->configureTempo(command.speed);
                if (playbackClock)
                    playbackClock->setRate(command.speed, qpcNow());
                audioMasterResync.store(true, std::memory_order_release);
                break;
            case CommandType::Pause:
                m_paused.store(true, std::memory_order_release);
                if (audioPipeline)
                    audioPipeline->setPaused(true);
                if (playbackClock)
                    playbackClock->pause(qpcNow());
                break;
            case CommandType::Resume:
                m_paused.store(false, std::memory_order_release);
                if (audioPipeline)
                    audioPipeline->setPaused(false);
                if (playbackClock)
                    playbackClock->resume(qpcNow());
                audioMasterResync.store(true, std::memory_order_release);
                break;
            }
        }
    };

    auto videoBacklogIsLate = [&]() {
        if (!audioPipeline || !playbackClock ||
            !audioIsMaster.load(std::memory_order_acquire)) {
            return false;
        }
        const std::optional<qint64> oldestVideoPtsUs = videoQueue.oldestPtsUs();
        if (!oldestVideoPtsUs)
            return false;
        return shouldDropVideoBacklog(
            audioPipeline->clock(),
            m_activeGeneration.load(std::memory_order_acquire),
            *oldestVideoPtsUs,
            qpcNow(),
            playbackClock->qpcFrequency(),
            FrameSchedulerConfig{}.lateDropThresholdUs);
    };

    // Adapts the demux's shared playback state to the audio worker's narrow, thread-safe seam. Holds
    // POINTERS to the atomics (never reaches into private members), and forwards the seek landing
    // through landSeek — the exact couplings the old inline audio decode had, now across a thread.
    struct RunAudioHost final : AudioWorker::Host {
        std::atomic<bool> *pCancelled = nullptr;
        std::atomic<bool> *pPaused = nullptr;
        std::atomic<bool> *pCommandPending = nullptr;
        std::atomic<quint64> *pActiveGeneration = nullptr;
        std::atomic<qint64> *pAudioDelayUs = nullptr;
        std::atomic<bool> *pDecodingToTarget = nullptr;
        std::atomic<qint64> *pSeekTargetUs = nullptr;
        std::atomic<bool> *pAudioIsMaster = nullptr;
        std::atomic<qint64> *pLastMasterPtsUs = nullptr;
        std::function<void(qint64)> seekLanded;
        bool cancelled() const override { return pCancelled->load(std::memory_order_acquire); }
        bool paused() const override { return pPaused->load(std::memory_order_acquire); }
        bool commandPending() const override { return pCommandPending->load(std::memory_order_acquire); }
        quint64 activeGeneration() const override
        {
            return pActiveGeneration->load(std::memory_order_acquire);
        }
        qint64 audioDelayUs() const override { return pAudioDelayUs->load(std::memory_order_acquire); }
        bool audioIsMaster() const override { return pAudioIsMaster->load(std::memory_order_acquire); }
        bool decodingToTarget() const override { return pDecodingToTarget->load(std::memory_order_acquire); }
        qint64 seekTargetUs() const override { return pSeekTargetUs->load(std::memory_order_acquire); }
        void onAudioPositionAdvanced(qint64 ptsUs) override
        {
            pLastMasterPtsUs->store(ptsUs, std::memory_order_release);
        }
        void onSeekLanded(qint64 ptsUs) override { if (seekLanded) seekLanded(ptsUs); }
    };

    // Start the audio worker now that landSeek exists (its Host reports the seek landing through it).
    // The worker is explicitly stopped at the cleanup below, so its thread is joined before any of
    // these locals (audioHost included) are destroyed.
    RunAudioHost audioHost;
    if (hasAudio) {
        audioHost.pCancelled = &m_cancelled;
        audioHost.pPaused = &m_paused;
        audioHost.pCommandPending = &m_commandPending;
        audioHost.pActiveGeneration = &m_activeGeneration;
        audioHost.pAudioDelayUs = &m_audioDelayUs;
        audioHost.pDecodingToTarget = &decodingToTarget;
        audioHost.pSeekTargetUs = &seekTargetUs;
        audioHost.pAudioIsMaster = &audioIsMaster;
        audioHost.pLastMasterPtsUs = &lastMasterPtsUs;
        audioHost.seekLanded = landSeek;
        audioWorker = std::make_unique<AudioWorker>(audioPipeline, audioCodecpar,
                                                    audioStream->time_base, &audioQueue, &audioHost,
                                                    generation);
        {
            std::scoped_lock queueLock(m_audioQueueMutex);
            m_audioQueueForInterrupt = &audioQueue; // cancel()/enqueueCommand() can now wake a blocked push
        }
        QString audioStartError;
        if (!audioWorker->start(&audioStartError)) {
            {
                std::scoped_lock queueLock(m_audioQueueMutex);
                m_audioQueueForInterrupt = nullptr;
            }
            {
                std::scoped_lock queueLock(m_videoQueueMutex);
                m_videoQueueForInterrupt = nullptr;
            }
            videoQueue.cancel(); // the video thread is already spawned; tear it down before returning
            if (videoThread.joinable())
                videoThread.join();
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::DecodeFailed, audioStartError, false});
            m_running.store(false, std::memory_order_release);
            return;
        }
        postAudioTrackChanged(generation, audioStreamIndex);
    }

    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    bool decodeFailed = false;
    result = 0;
    while (!m_cancelled.load(std::memory_order_acquire)) {
        if (m_commandPending.exchange(false, std::memory_order_acq_rel))
            processCommands();
        if (m_cancelled.load(std::memory_order_acquire))
            break;

        // The video thread failed (decoder setup or a hard decode error) — surface it and stop.
        if (videoThreadFailed.load(std::memory_order_acquire)) {
            if (decodeFailure.isEmpty())
                decodeFailure = videoFailureMessage;
            decodeFailed = true;
            break;
        }

        // Paused (and not scanning to a seek target) or fully ended: sleep until a command.
        if ((m_paused.load(std::memory_order_acquire) &&
             !decodingToTarget.load(std::memory_order_acquire)) ||
            (eofReached && !decodingToTarget.load(std::memory_order_acquire))) {
            std::unique_lock lock(m_commandMutex);
            m_commandCv.wait(lock, [this] {
                return m_commandPending.load(std::memory_order_acquire) ||
                       m_cancelled.load(std::memory_order_acquire) || !m_commands.empty();
            });
            continue;
        }

        // End of file was reached; audio has drained and the video queue was closed. Publish
        // EndOfFile only once the video thread has actually drained its tail (up to ~2 s buffered),
        // so the UI is never told "done" while video is still presenting.
        if (eofSignaled && !eofReached) {
            const bool videoDrained = !videoPresent || videoReachedEnd.load(std::memory_order_acquire);
            const bool audioDrained = !hasAudio || (audioWorker && audioWorker->reachedEnd());
            if (videoDrained && audioDrained) {
                postEnded(gen, DemuxEndReason::EndOfFile,
                          Player2Error{Player2ErrorCode::None, QString(), false});
                eofReached = true;
            } else {
                std::unique_lock lock(m_commandMutex);
                m_commandCv.wait_for(lock, std::chrono::milliseconds(20), [this] {
                    return m_commandPending.load(std::memory_order_acquire) ||
                           m_cancelled.load(std::memory_order_acquire) || !m_commands.empty();
                });
            }
            continue;
        }

        result = av_read_frame(format.get(), packet.get());
        if (result < 0) {
            if (result == AVERROR_EOF) {
                // Signal end-of-stream to both workers; each drains its OWN tail (audio: decoder +
                // loudnorm lookahead to the sink, then lands at the edge if it was scanning; video:
                // its presentation queue). EndOfFile is published by the eofSignaled branch above,
                // once BOTH tails have drained.
                if (hasAudio)
                    audioQueue.setEndOfStream();
                if (videoPresent)
                    videoQueue.setEndOfStream();
                eofSignaled = true;
                continue;
            }
            decodeFailure = QStringLiteral("Demux read failed: %1").arg(avError(result));
            decodeFailed = true;
            break;
        }

        const int streamIndex = packet->stream_index;
        AVStream *stream = format->streams[streamIndex];
        const auto toUs = [stream](qint64 value) {
            return value == AV_NOPTS_VALUE ? qint64{0}
                                           : av_rescale_q(value, stream->time_base,
                                                          AVRational{1, 1'000'000});
        };
        postPacket(gen, DemuxPacketInfo{streamIndex, toUs(packet->pts),
                                        toUs(packet->duration), packet->size,
                                        (packet->flags & AV_PKT_FLAG_KEY) != 0});

        // No stream is producing frames to land on (no audio master and no video pipeline): land the
        // seek on the first packet at or past the target so completion is still observable.
        if (decodingToTarget.load(std::memory_order_acquire) && !audioIsMaster && !videoPresent) {
            const qint64 packetUs = toUs(packet->pts);
            if (packetUs + 1'000 >= seekTargetUs.load(std::memory_order_acquire))
                landSeek(packetUs);
        }

        // Route the packet by stream. `streamIndex` is captured above because a video push moves out
        // of the packet (blanking stream_index); else-if keeps exactly one branch consuming it.
        if (videoPresent && streamIndex == videoStreamIndex) {
            // Hand off to the video worker. A full-but-on-time queue backpressures without
            // discarding presentation order; only the current sink clock proving that its oldest
            // packet is late authorizes whole-GOP recovery. Commands interrupt either path so seek,
            // pause and cancel stay responsive. After a generation-changing command the same packet
            // is stale and must not be retried.
            const qint64 packetPtsUs = toUs(packet->pts);
            const bool keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            PacketQueue::Admit admit = videoQueue.pushInterruptible(
                packet.get(), packetPtsUs, gen, keyframe, videoBacklogIsLate(),
                /*recheckAfterMs*/ 10);
            while (admit == PacketQueue::Admit::Interrupted) {
                const quint64 genBefore = gen;
                if (m_commandPending.exchange(false, std::memory_order_acq_rel))
                    processCommands();
                if (m_cancelled.load(std::memory_order_acquire) || gen != genBefore)
                    break;
                admit = videoQueue.pushInterruptible(
                    packet.get(), packetPtsUs, gen, keyframe, videoBacklogIsLate(),
                    /*recheckAfterMs*/ 10);
            }
            if (admit == PacketQueue::Admit::Cancelled &&
                !m_cancelled.load(std::memory_order_acquire)) {
                if (decodeFailure.isEmpty())
                    decodeFailure = videoFailureMessage;
                decodeFailed = true;
                av_packet_unref(packet.get());
                break;
            }
        } else if (hasAudio && streamIndex == audioStreamIndex) {
            // Hand off to the audio worker. Blocking here paces the demux to real-time audio, but a
            // command interrupts a blocked push so the demux command loop is never trapped. On an
            // interrupt: service commands and retry the SAME packet — UNLESS a seek/track-switch moved
            // the generation, in which case this packet is stale and is dropped (the read resumes
            // fresh after the container seek).
            PacketQueue::Admit admit = audioQueue.pushInterruptible(packet.get(), toUs(packet->pts), gen);
            while (admit == PacketQueue::Admit::Interrupted) {
                const quint64 genBefore = gen;
                if (m_commandPending.exchange(false, std::memory_order_acq_rel))
                    processCommands();
                if (m_cancelled.load(std::memory_order_acquire) || gen != genBefore)
                    break; // cancelled, or a seek changed the generation → drop this stale packet
                admit = audioQueue.pushInterruptible(packet.get(), toUs(packet->pts), gen);
            }
            if (admit == PacketQueue::Admit::Cancelled &&
                !m_cancelled.load(std::memory_order_acquire)) {
                // The audio worker cancelled the queue (a hard decode failure) — surface and stop.
                if (decodeFailure.isEmpty() && audioWorker && audioWorker->failed())
                    decodeFailure = audioWorker->failureMessage();
                decodeFailed = true;
                av_packet_unref(packet.get());
                break;
            }
        } else if (subtitleStreamIndex >= 0 && streamIndex == subtitleStreamIndex &&
                   !decodingToTarget.load(std::memory_order_acquire)) {
            std::vector<SubtitleCue> cues;
            QString subtitleError;
            if (subtitlePipeline.decode(packet.get(), gen, subtitleStreamIndex, &cues,
                                        &subtitleError)) {
                for (SubtitleCue &cue : cues) {
                    // Bitmap cues are positioned against the video frame — carry its size so the QML
                    // layer can scale the region onto the displayed video.
                    if (cue.bitmap && videoStream && videoStream->codecpar) {
                        cue.canvasWidth = videoStream->codecpar->width;
                        cue.canvasHeight = videoStream->codecpar->height;
                    }
                    postSubtitleCue(gen, std::move(cue));
                }
            }
        }
        av_packet_unref(packet.get());
    }

    // Tear down the video thread: unblock any queue wait, then join before the codecs are released.
    videoQueue.cancel();
    if (videoThread.joinable())
        videoThread.join();
    if (!decodeFailed && videoThreadFailed.load(std::memory_order_acquire)) {
        decodeFailed = true;
        if (decodeFailure.isEmpty())
            decodeFailure = videoFailureMessage;
    }
    {
        std::scoped_lock queueLock(m_videoQueueMutex);
        m_videoQueueForInterrupt = nullptr;
    }

    // Tear down the audio worker: stop() cancels its queue (unblocking its pop) and joins its thread
    // BEFORE any run-local it references (audioHost, the atomics, audioQueue) is destroyed. Then clear
    // the off-thread interrupt handle so a late cancel()/command can't touch the dying audioQueue.
    if (audioWorker) {
        audioWorker->stop();
        if (!decodeFailed && audioWorker->failed()) {
            decodeFailed = true;
            if (decodeFailure.isEmpty())
                decodeFailure = audioWorker->failureMessage();
        }
    }
    {
        std::scoped_lock queueLock(m_audioQueueMutex);
        m_audioQueueForInterrupt = nullptr;
    }

    const bool cancelled = m_cancelled.load(std::memory_order_acquire);
    if (cancelled) {
        postEnded(gen, DemuxEndReason::Cancelled,
                  Player2Error{Player2ErrorCode::Cancelled, QStringLiteral("Demux cancelled"), true});
    } else if (decodeFailed) {
        // A GPU device-removed surfaces here as a decode failure; report it as a typed, recoverable
        // DeviceLost so the session's recovery coordinator can act, not a generic decode error.
        const bool deviceLost = pipeline && pipeline->deviceLost();
        postEnded(gen, DemuxEndReason::Failed,
                  Player2Error{deviceLost ? Player2ErrorCode::DeviceLost
                                          : Player2ErrorCode::DecodeFailed,
                               deviceLost ? QStringLiteral("Video device lost")
                                          : (decodeFailure.isEmpty()
                                                 ? QStringLiteral("Demux read failed")
                                                 : decodeFailure),
                               deviceLost});
    }
    // A normal end of file was already published inside the loop before parking.
    m_running.store(false, std::memory_order_release);
}

void DemuxSession::postOpened(quint64 generation, DemuxMetadata metadata)
{
    QMetaObject::invokeMethod(this, [this, generation, metadata = std::move(metadata)] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit opened(generation, metadata);
    }, Qt::QueuedConnection);
}

void DemuxSession::postPacket(quint64 generation, DemuxPacketInfo packet)
{
    QMetaObject::invokeMethod(this, [this, generation, packet] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit packetObserved(generation, packet);
    }, Qt::QueuedConnection);
}

void DemuxSession::postEnded(quint64 generation, DemuxEndReason reason, Player2Error error)
{
    QMetaObject::invokeMethod(this, [this, generation, reason, error = std::move(error)] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit ended(generation, reason, error);
    }, Qt::QueuedConnection);
}

void DemuxSession::postSeekCompleted(quint64 generation, double actualSeconds)
{
    QMetaObject::invokeMethod(this, [this, generation, actualSeconds] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit seekCompleted(generation, actualSeconds);
    }, Qt::QueuedConnection);
}

void DemuxSession::postAudioTrackChanged(quint64 generation, int streamIndex)
{
    QMetaObject::invokeMethod(this, [this, generation, streamIndex] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit audioTrackChanged(generation, streamIndex);
    }, Qt::QueuedConnection);
}

void DemuxSession::postAudioNormalizationChanged(quint64 generation, int mode)
{
    QMetaObject::invokeMethod(this, [this, generation, mode] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit audioNormalizationChanged(generation, mode);
    }, Qt::QueuedConnection);
}

void DemuxSession::postNetworkState(NetworkState state)
{
    const quint64 generation = m_activeGeneration.load(std::memory_order_acquire);
    const int value = static_cast<int>(state);
    QMetaObject::invokeMethod(this, [this, generation, value] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit networkStateChanged(generation, value);
    }, Qt::QueuedConnection);
}

void DemuxSession::postSubtitleTrackChanged(quint64 generation, int streamIndex)
{
    QMetaObject::invokeMethod(this, [this, generation, streamIndex] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit subtitleTrackChanged(generation, streamIndex);
    }, Qt::QueuedConnection);
}

void DemuxSession::postSubtitleCue(quint64 generation, SubtitleCue cue)
{
    QMetaObject::invokeMethod(this, [this, generation, cue = std::move(cue)] {
        if (m_activeGeneration.load(std::memory_order_acquire) == generation)
            emit subtitleCue(generation, cue);
    }, Qt::QueuedConnection);
}

} // namespace Colosseum::Player2
