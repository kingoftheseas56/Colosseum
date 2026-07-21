#include "DemuxSession.h"

#include "player2/video/D3D11VideoPipeline.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

#include <QtCore/QMetaObject>

#include <algorithm>
#include <chrono>
#include <memory>

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

} // namespace

DemuxSession::DemuxSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DemuxEndReason>();
    qRegisterMetaType<DemuxStreamInfo>();
    qRegisterMetaType<DemuxMetadata>();
    qRegisterMetaType<DemuxPacketInfo>();
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

void DemuxSession::open(const PlaybackRequest &request, quint64 generation)
{
    cancel();
    m_activeGeneration.store(generation, std::memory_order_release);
    m_cancelled.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    std::scoped_lock lock(m_workerMutex);
    m_worker = std::thread(&DemuxSession::run, this, request, generation);
}

void DemuxSession::cancel()
{
    m_cancelled.store(true, std::memory_order_release);
    joinWorker();
}

void DemuxSession::setVideoPipeline(D3D11VideoPipeline *pipeline) noexcept
{
    m_videoPipeline.store(pipeline, std::memory_order_release);
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
    const QByteArray location = request.source.isLocalFile()
        ? request.source.toLocalFile().toUtf8() : request.source.toString().toUtf8();
    int result = avformat_open_input(&rawFormat, location.constData(), nullptr, nullptr);
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
        metadata.streams.append(DemuxStreamInfo{
            static_cast<int>(i), mediaTypeName(stream->codecpar->codec_type),
            QString::fromUtf8(avcodec_get_name(stream->codecpar->codec_id)),
            dictionaryValue(stream->metadata, "language"),
            dictionaryValue(stream->metadata, "title")});
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

    using Clock = std::chrono::steady_clock;
    Clock::time_point wallStart{};
    qint64 firstPtsUs = 0;
    bool haveFirstPts = false;
    quint64 videoSequence = 0;
    QString decodeFailure;
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
            if (!haveFirstPts) {
                firstPtsUs = ptsUs;
                wallStart = Clock::now();
                haveFirstPts = true;
            }
            const auto target = wallStart + std::chrono::microseconds(
                std::max<qint64>(0, ptsUs - firstPtsUs));
            std::this_thread::sleep_until(target);
            QString submitError;
            const VideoFrameToken token{generation, ++videoSequence, ptsUs};
            if (!pipeline->submitDecodedFrame(frame.get(), token, &submitError) &&
                !submitError.isEmpty()) {
                decodeFailure = submitError;
                return false;
            }
            av_frame_unref(frame.get());
        }
        return true;
    };

    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    bool decodeFailed = false;
    while (!m_cancelled.load(std::memory_order_acquire) &&
           (result = av_read_frame(format.get(), packet.get())) >= 0) {
        AVStream *stream = format->streams[packet->stream_index];
        const auto toUs = [stream](qint64 value) {
            return value == AV_NOPTS_VALUE ? qint64{0}
                                           : av_rescale_q(value, stream->time_base,
                                                          AVRational{1, 1'000'000});
        };
        postPacket(generation, DemuxPacketInfo{packet->stream_index, toUs(packet->pts),
                                               toUs(packet->duration), packet->size,
                                               (packet->flags & AV_PKT_FLAG_KEY) != 0});
        if (decoder && packet->stream_index == videoStreamIndex) {
            const int sendResult = avcodec_send_packet(decoder.get(), packet.get());
            if (sendResult < 0 || !receiveVideoFrames()) {
                if (decodeFailure.isEmpty())
                    decodeFailure = QStringLiteral("Video packet submission failed: %1")
                                        .arg(avError(sendResult));
                decodeFailed = true;
                av_packet_unref(packet.get());
                break;
            }
        }
        av_packet_unref(packet.get());
    }

    if (!decodeFailed && decoder && !m_cancelled.load(std::memory_order_acquire) &&
        result == AVERROR_EOF) {
        avcodec_send_packet(decoder.get(), nullptr);
        decodeFailed = !receiveVideoFrames();
    }

    const bool cancelled = m_cancelled.load(std::memory_order_acquire);
    const bool eof = result == AVERROR_EOF;
    const DemuxEndReason reason = cancelled ? DemuxEndReason::Cancelled
                                            : (!decodeFailed && eof ? DemuxEndReason::EndOfFile
                                                   : DemuxEndReason::Failed);
    const Player2Error error = reason == DemuxEndReason::Failed
        ? Player2Error{Player2ErrorCode::DecodeFailed,
                       decodeFailure.isEmpty()
                           ? QStringLiteral("Demux read failed: %1").arg(avError(result))
                           : decodeFailure, false}
        : Player2Error{reason == DemuxEndReason::Cancelled ? Player2ErrorCode::Cancelled
                                                           : Player2ErrorCode::None,
                       cancelled ? QStringLiteral("Demux cancelled") : QString(), cancelled};
    postEnded(generation, reason, error);
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

} // namespace Colosseum::Player2
