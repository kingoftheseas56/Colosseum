#include "DemuxSession.h"

#include "player2/video/D3D11VideoPipeline.h"
#include "player2/audio/AudioPipeline.h"
#include "player2/network/QtHttpTransport.h"
#include "FrameScheduler.h"
#include "PlaybackClock.h"
#include "SubtitlePipeline.h"

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

    D3D11VideoPipeline *pipeline = m_videoPipeline.load(std::memory_order_acquire);
    int videoStreamIndex = -1;
    AVStream *videoStream = nullptr;
    std::unique_ptr<AVBufferRef, BufferCloser> hardware;
    std::unique_ptr<AVCodecContext, CodecCloser> decoder;
    std::unique_ptr<AVFrame, FrameCloser> frame;
    double framesPerSecond = 24.0;
    if (pipeline) {
        const AVCodec *codec = nullptr;
        videoStreamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                               &codec, 0);
        if (videoStreamIndex < 0 || !codec) {
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::DecodeFailed,
                                   QStringLiteral("No decodable video stream"), false});
            m_running.store(false, std::memory_order_release);
            return;
        }
        QString hardwareError;
        hardware.reset(pipeline->createDecoderDeviceContext(&hardwareError));
        decoder.reset(avcodec_alloc_context3(codec));
        videoStream = format->streams[videoStreamIndex];
        if (!hardware || !decoder ||
            (result = avcodec_parameters_to_context(decoder.get(), videoStream->codecpar)) < 0) {
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::DecodeFailed,
                                   hardwareError.isEmpty()
                                       ? QStringLiteral("Video decoder setup failed: %1")
                                             .arg(avError(result))
                                       : hardwareError,
                                   false});
            m_running.store(false, std::memory_order_release);
            return;
        }
        decoder->hw_device_ctx = av_buffer_ref(hardware.get());
        decoder->get_format = selectD3d11Format;
        if ((result = avcodec_open2(decoder.get(), codec, nullptr)) < 0) {
            postEnded(generation, DemuxEndReason::Failed,
                      Player2Error{Player2ErrorCode::DecodeFailed,
                                   QStringLiteral("Hardware decoder open failed: %1")
                                       .arg(avError(result)), false});
            m_running.store(false, std::memory_order_release);
            return;
        }
        frame.reset(av_frame_alloc());
        const AVRational guessedRate = av_guess_frame_rate(format.get(), videoStream, nullptr);
        if (guessedRate.num > 0 && guessedRate.den > 0)
            framesPerSecond = av_q2d(guessedRate);
    }

    AudioPipeline *audioPipeline = m_audioPipeline.load(std::memory_order_acquire);
    int audioStreamIndex = -1;
    AVStream *audioStream = nullptr;
    std::unique_ptr<AVCodecContext, CodecCloser> audioDecoder;
    std::unique_ptr<AVFrame, FrameCloser> audioFrame;
    if (audioPipeline) {
        const AVCodec *audioCodec = nullptr;
        audioStreamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1,
                                               &audioCodec, 0);
        if (audioStreamIndex >= 0 && audioCodec) {
            audioStream = format->streams[audioStreamIndex];
            audioDecoder.reset(avcodec_alloc_context3(audioCodec));
            QString audioError;
            if (!audioDecoder) {
                postEnded(generation, DemuxEndReason::Failed,
                          Player2Error{Player2ErrorCode::DecodeFailed,
                                       QStringLiteral("Could not allocate an audio decoder"), false});
                m_running.store(false, std::memory_order_release);
                return;
            }
            if ((result = avcodec_parameters_to_context(audioDecoder.get(),
                                                        audioStream->codecpar)) < 0 ||
                (result = avcodec_open2(audioDecoder.get(), audioCodec, nullptr)) < 0) {
                postEnded(generation, DemuxEndReason::Failed,
                          Player2Error{Player2ErrorCode::DecodeFailed,
                                       QStringLiteral("Audio decoder setup failed: %1")
                                           .arg(avError(result)), false});
                m_running.store(false, std::memory_order_release);
                return;
            }
            if (!audioPipeline->open(AudioFormat{48'000, 2}, &audioError)) {
                postEnded(generation, DemuxEndReason::Failed,
                          Player2Error{Player2ErrorCode::AudioDeviceLost, audioError, true});
                m_running.store(false, std::memory_order_release);
                return;
            }
            audioFrame.reset(av_frame_alloc());
            if (!audioFrame) {
                postEnded(generation, DemuxEndReason::Failed,
                          Player2Error{Player2ErrorCode::DecodeFailed,
                                       QStringLiteral("Could not allocate an audio decode frame"),
                                       false});
                m_running.store(false, std::memory_order_release);
                return;
            }
        }
    }

    PlaybackClock *playbackClock = m_playbackClock.load(std::memory_order_acquire);
    FrameScheduler *frameScheduler = m_frameScheduler.load(std::memory_order_acquire);

    // Mutable transport state owned by this worker. `gen` is the single generation the worker tags
    // every product with; a seek/track command advances it, so no component keeps a private epoch.
    quint64 gen = generation;
    bool audioMasterActive = false;
    quint64 videoSequence = 0;
    qint64 lastMasterPtsUs = 0;
    bool audioIsMaster = audioDecoder != nullptr;
    SubtitlePipeline subtitlePipeline;
    int subtitleStreamIndex = -1;
    bool decodingToTarget = false;
    bool videoLandingPresented = false;
    qint64 seekTargetUs = 0;
    bool seekResumePlaying = true;
    bool eofReached = false;
    QString decodeFailure;

    // Publishes seek/frame-step completion, restores the requested play/pause state and re-arms the
    // clock at the landed position. Called once per reposition, from whichever stream is master.
    auto landSeek = [&](qint64 ptsUs) {
        decodingToTarget = false;
        lastMasterPtsUs = ptsUs;
        const bool pausing = !seekResumePlaying;
        m_paused.store(pausing, std::memory_order_release);
        if (audioPipeline)
            audioPipeline->setPaused(pausing);
        if (playbackClock) {
            playbackClock->reset(ptsUs, qpcNow());
            if (pausing)
                playbackClock->pause(qpcNow());
        }
        audioMasterActive = false;
        postSeekCompleted(gen, ptsUs / 1'000'000.0);
    };

    // Mandatory seek order: adopt the new generation, flush every active pipeline, flush the codecs,
    // seek the container, then decode to the target frame before presenting.
    auto applySeek = [&](qint64 targetUs, quint64 newGeneration, bool resumePlaying) {
        gen = newGeneration;
        m_activeGeneration.store(newGeneration, std::memory_order_release);
        if (decoder)
            avcodec_flush_buffers(decoder.get());
        if (audioDecoder)
            avcodec_flush_buffers(audioDecoder.get());
        if (audioPipeline) {
            audioPipeline->flush(newGeneration);
            audioPipeline->flushFilters(); // drop stale normalization-buffered audio (worker thread)
        }
        subtitlePipeline.flush(); // drop any decoder-buffered cue state across the seek
        if (pipeline)
            pipeline->flush(newGeneration);
        if (playbackClock)
            playbackClock->invalidate();
        if (frameScheduler)
            frameScheduler->reset();
        audioMasterActive = false;
        qint64 clamped = std::max<qint64>(0, targetUs);
        if (metadata.durationUs > 0)
            clamped = std::min(clamped, metadata.durationUs);
        av_seek_frame(format.get(), -1, clamped, AVSEEK_FLAG_BACKWARD);
        seekTargetUs = clamped;
        seekResumePlaying = resumePlaying;
        decodingToTarget = true;
        videoLandingPresented = false;
        eofReached = false;
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
        if (audioPipeline) {
            audioPipeline->flush(newGeneration);
            audioPipeline->flushFilters();
        }
        if (streamIndex >= 0 && streamIndex < static_cast<int>(format->nb_streams) &&
            format->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            const AVCodec *codec =
                avcodec_find_decoder(format->streams[streamIndex]->codecpar->codec_id);
            if (codec) {
                AVCodecContext *context = avcodec_alloc_context3(codec);
                if (context &&
                    avcodec_parameters_to_context(
                        context, format->streams[streamIndex]->codecpar) >= 0 &&
                    avcodec_open2(context, codec, nullptr) >= 0) {
                    audioDecoder.reset(context);
                    audioStream = format->streams[streamIndex];
                    audioStreamIndex = streamIndex;
                    audioIsMaster = true;
                } else {
                    avcodec_free_context(&context);
                }
            }
        }
        audioMasterActive = false;
        // Report the track actually decoding now: if the requested decoder failed to open,
        // audioStreamIndex still points at the previous track that keeps playing.
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
                if (audioPipeline)
                    audioPipeline->configureNormalization(
                        static_cast<NormalizationMode>(command.normalizationMode));
                postAudioNormalizationChanged(gen, command.normalizationMode);
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
                audioMasterActive = false;
                break;
            }
        }
    };

    auto receiveVideoFrames = [&]() -> bool {
        while (!m_cancelled.load(std::memory_order_acquire)) {
            const int receiveResult = avcodec_receive_frame(decoder.get(), frame.get());
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                return true;
            if (receiveResult < 0) {
                decodeFailure = QStringLiteral("Video decode failed: %1").arg(avError(receiveResult));
                return false;
            }
            if (frame->format != AV_PIX_FMT_D3D11) {
                decodeFailure = QStringLiteral("Hardware decoder returned %1 instead of D3D11")
                    .arg(QString::fromLatin1(av_get_pix_fmt_name(
                        static_cast<AVPixelFormat>(frame->format))));
                return false;
            }
            pipeline->noteDecoded();
            qint64 ptsUs = static_cast<qint64>((videoSequence * 1'000'000.0) / framesPerSecond);
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                ptsUs = av_rescale_q(frame->best_effort_timestamp, videoStream->time_base,
                                     AVRational{1, 1'000'000});
            }
            if (decodingToTarget) {
                // Skip everything before the target; present only the first landing frame so the
                // destination shows, then hold until the master stream lands.
                if (ptsUs + 1'000 < seekTargetUs || videoLandingPresented) {
                    ++videoSequence;
                    av_frame_unref(frame.get());
                    continue;
                }
                QString submitError;
                const VideoFrameToken token{gen, ++videoSequence, ptsUs};
                if (!pipeline->submitDecodedFrame(frame.get(), token, &submitError) &&
                    !submitError.isEmpty()) {
                    if (m_commandPending.load(std::memory_order_acquire)) {
                        av_frame_unref(frame.get());
                        return true;
                    }
                    decodeFailure = submitError;
                    return false;
                }
                av_frame_unref(frame.get());
                videoLandingPresented = true;
                if (!audioIsMaster)
                    landSeek(ptsUs);
                continue;
            }
            if (playbackClock && frameScheduler) {
                qint64 now = qpcNow();
                const AudioClockSnapshot audio = audioPipeline ? audioPipeline->clock()
                                                                : AudioClockSnapshot{};
                if (audio.valid) {
                    const qint64 audioNow = audio.mediaPositionUs + static_cast<qint64>(
                        static_cast<long double>(now - audio.qpcTimestamp) * 1'000'000.0L /
                        playbackClock->qpcFrequency());
                    if (!audioMasterActive) {
                        // First audio lock. If the clock was already QPC-seeded (video ran ahead
                        // during audio/normalization warmup), converge toward audio instead of
                        // snapping backward, which would spike A/V error for many frames.
                        if (playbackClock->valid())
                            playbackClock->correctToward(audioNow, now, 20'000);
                        else
                            playbackClock->reset(audioNow, now);
                        audioMasterActive = true;
                    } else {
                        playbackClock->correctToward(audioNow, now, 5'000);
                    }
                } else if (!playbackClock->valid()) {
                    playbackClock->reset(ptsUs, now);
                }

                const FrameScheduleDecision decision = frameScheduler->choose(
                    playbackClock->positionAt(now), now,
                    std::vector<FrameCandidate>{{videoSequence + 1, ptsUs}});
                if (decision.action == FrameScheduleAction::WaitUntilQpc) {
                    while (!m_cancelled.load(std::memory_order_acquire)) {
                        if (m_commandPending.load(std::memory_order_acquire))
                            break; // a transport command is waiting; stop pacing and process it
                        now = qpcNow();
                        if (now >= decision.deadlineQpc)
                            break;
                        const qint64 remainingUs = static_cast<qint64>(
                            static_cast<long double>(decision.deadlineQpc - now) *
                            1'000'000.0L / playbackClock->qpcFrequency());
                        std::this_thread::sleep_for(std::chrono::microseconds(
                            std::clamp<qint64>(remainingUs, 100, 5'000)));
                    }
                    if (m_commandPending.load(std::memory_order_acquire)) {
                        av_frame_unref(frame.get());
                        return true; // abandon this frame; the command loop will flush anyway
                    }
                } else if (decision.action == FrameScheduleAction::DropLate) {
                    pipeline->noteSchedulingDecision(decision.timingErrorUs, true);
                    ++videoSequence;
                    av_frame_unref(frame.get());
                    continue;
                }
                now = qpcNow();
                pipeline->noteSchedulingDecision(
                    ptsUs - playbackClock->positionAt(now), false);
            }
            QString submitError;
            const VideoFrameToken token{gen, ++videoSequence, ptsUs};
            if (!pipeline->submitDecodedFrame(frame.get(), token, &submitError) &&
                !submitError.isEmpty()) {
                if (m_commandPending.load(std::memory_order_acquire)) {
                    av_frame_unref(frame.get());
                    return true;
                }
                decodeFailure = submitError;
                return false;
            }
            if (!audioIsMaster)
                lastMasterPtsUs = ptsUs;
            av_frame_unref(frame.get());
        }
        return true;
    };

    auto receiveAudioFrames = [&]() -> bool {
        while (!m_cancelled.load(std::memory_order_acquire)) {
            const int receiveResult = avcodec_receive_frame(audioDecoder.get(), audioFrame.get());
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                return true;
            if (receiveResult < 0) {
                decodeFailure = QStringLiteral("Audio decode failed: %1").arg(avError(receiveResult));
                return false;
            }
            qint64 ptsUs = 0;
            if (audioFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                ptsUs = av_rescale_q(audioFrame->best_effort_timestamp, audioStream->time_base,
                                     AVRational{1, 1'000'000});
            }
            if (decodingToTarget && audioIsMaster) {
                if (ptsUs + 1'000 < seekTargetUs) {
                    av_frame_unref(audioFrame.get()); // discard audio before the target
                    continue;
                }
                landSeek(ptsUs); // audio is master: this frame is the landing point
            }
            if (!m_paused.load(std::memory_order_acquire) && !decodingToTarget) {
                QString audioError;
                // The pts reported to the master clock carries the A/V offset; the true position
                // (lastMasterPtsUs, seek landing) stays unshifted.
                const qint64 clockPtsUs = ptsUs + m_audioDelayUs.load(std::memory_order_acquire);
                if (!audioPipeline->submitDecodedFrame(audioFrame.get(), clockPtsUs, gen,
                                                       &audioError)) {
                    if (m_commandPending.load(std::memory_order_acquire) ||
                        gen != m_activeGeneration.load(std::memory_order_acquire)) {
                        av_frame_unref(audioFrame.get());
                        return true; // benign: a flush/seek is in flight
                    }
                    decodeFailure = audioError;
                    return false;
                }
                if (audioIsMaster)
                    lastMasterPtsUs = ptsUs;
            }
            av_frame_unref(audioFrame.get());
        }
        return true;
    };

    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    bool decodeFailed = false;
    result = 0;
    while (!m_cancelled.load(std::memory_order_acquire)) {
        if (m_commandPending.exchange(false, std::memory_order_acq_rel))
            processCommands();
        if (m_cancelled.load(std::memory_order_acquire))
            break;

        // Paused (and not scanning to a seek target) or parked at EOF: sleep until a command.
        if ((m_paused.load(std::memory_order_acquire) && !decodingToTarget) ||
            (eofReached && !decodingToTarget)) {
            std::unique_lock lock(m_commandMutex);
            m_commandCv.wait(lock, [this] {
                return m_commandPending.load(std::memory_order_acquire) ||
                       m_cancelled.load(std::memory_order_acquire) || !m_commands.empty();
            });
            continue;
        }

        result = av_read_frame(format.get(), packet.get());
        if (result < 0) {
            if (result == AVERROR_EOF) {
                if (decoder) {
                    avcodec_send_packet(decoder.get(), nullptr);
                    receiveVideoFrames();
                }
                if (audioDecoder) {
                    avcodec_send_packet(audioDecoder.get(), nullptr);
                    receiveAudioFrames();
                    if (audioPipeline) {
                        QString audioError;
                        audioPipeline->drain(gen, &audioError);
                    }
                }
                if (decodingToTarget)
                    landSeek(seekTargetUs); // sought past the end: land at the requested edge
                postEnded(gen, DemuxEndReason::EndOfFile,
                          Player2Error{Player2ErrorCode::None, QString(), false});
                eofReached = true;
                continue;
            }
            decodeFailure = QStringLiteral("Demux read failed: %1").arg(avError(result));
            decodeFailed = true;
            break;
        }

        AVStream *stream = format->streams[packet->stream_index];
        const auto toUs = [stream](qint64 value) {
            return value == AV_NOPTS_VALUE ? qint64{0}
                                           : av_rescale_q(value, stream->time_base,
                                                          AVRational{1, 1'000'000});
        };
        postPacket(gen, DemuxPacketInfo{packet->stream_index, toUs(packet->pts),
                                        toUs(packet->duration), packet->size,
                                        (packet->flags & AV_PKT_FLAG_KEY) != 0});

        // No decoder is producing frames (e.g. video-only with no attached pipeline): land the seek
        // on the first packet at or past the target so completion is still observable.
        if (decodingToTarget && !audioIsMaster && !decoder) {
            const qint64 packetUs = toUs(packet->pts);
            if (packetUs + 1'000 >= seekTargetUs)
                landSeek(packetUs);
        }

        if (decoder && packet->stream_index == videoStreamIndex) {
            const int sendResult = avcodec_send_packet(decoder.get(), packet.get());
            if (sendResult < 0 || !receiveVideoFrames()) {
                if (!m_commandPending.load(std::memory_order_acquire)) {
                    if (decodeFailure.isEmpty())
                        decodeFailure = QStringLiteral("Video packet submission failed: %1")
                                            .arg(avError(sendResult));
                    decodeFailed = true;
                    av_packet_unref(packet.get());
                    break;
                }
            }
        }
        if (audioDecoder && packet->stream_index == audioStreamIndex) {
            const int sendResult = avcodec_send_packet(audioDecoder.get(), packet.get());
            if (sendResult < 0 || !receiveAudioFrames()) {
                if (!m_commandPending.load(std::memory_order_acquire)) {
                    if (decodeFailure.isEmpty())
                        decodeFailure = QStringLiteral("Audio packet submission failed: %1")
                                            .arg(avError(sendResult));
                    decodeFailed = true;
                    av_packet_unref(packet.get());
                    break;
                }
            }
        }
        if (subtitleStreamIndex >= 0 && packet->stream_index == subtitleStreamIndex &&
            !decodingToTarget) {
            std::vector<SubtitleCue> cues;
            QString subtitleError;
            if (subtitlePipeline.decode(packet.get(), gen, subtitleStreamIndex, &cues,
                                        &subtitleError)) {
                for (SubtitleCue &cue : cues)
                    postSubtitleCue(gen, std::move(cue));
            }
        }
        av_packet_unref(packet.get());
    }

    const bool cancelled = m_cancelled.load(std::memory_order_acquire);
    if (cancelled) {
        postEnded(gen, DemuxEndReason::Cancelled,
                  Player2Error{Player2ErrorCode::Cancelled, QStringLiteral("Demux cancelled"), true});
    } else if (decodeFailed) {
        postEnded(gen, DemuxEndReason::Failed,
                  Player2Error{Player2ErrorCode::DecodeFailed,
                               decodeFailure.isEmpty()
                                   ? QStringLiteral("Demux read failed") : decodeFailure,
                               false});
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
