#include "ffmpeg_hevc_source.h"

#include "shared_bridge.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

namespace {

QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

AVPixelFormat selectD3d11Format(AVCodecContext *, const AVPixelFormat *formats)
{
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == AV_PIX_FMT_D3D11)
            return *format;
    }
    return AV_PIX_FMT_NONE;
}

QString dxgiFormatName(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_NV12: return QStringLiteral("NV12");
    case DXGI_FORMAT_P010: return QStringLiteral("P010");
    default: return QStringLiteral("DXGI_%1").arg(static_cast<int>(format));
    }
}

struct FormatCloser { void operator()(AVFormatContext *p) const { avformat_close_input(&p); } };
struct CodecCloser { void operator()(AVCodecContext *p) const { avcodec_free_context(&p); } };
struct FrameCloser { void operator()(AVFrame *p) const { av_frame_free(&p); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct BufferCloser { void operator()(AVBufferRef *p) const { av_buffer_unref(&p); } };

} // namespace

FfmpegHevcSource::FfmpegHevcSource(SharedBridge *bridge)
    : m_bridge(bridge)
{
}

bool FfmpegHevcSource::run(const QString &filePath, const std::atomic_bool &stop,
                           const std::function<void()> &wakeConsumer)
{
    m_bridge->setSourceInfo(QStringLiteral("hevc"), QString(), QString(), QString(), 0, 0, false);
    auto fail = [this, &wakeConsumer](const QString &message) {
        m_bridge->setSourceError(message);
        wakeConsumer();
        return false;
    };

    AVFormatContext *rawFormat = nullptr;
    QByteArray nativePath = filePath.toUtf8();
    int rc = avformat_open_input(&rawFormat, nativePath.constData(), nullptr, nullptr);
    if (rc < 0)
        return fail(QStringLiteral("avformat_open_input: %1").arg(avError(rc)));
    std::unique_ptr<AVFormatContext, FormatCloser> format(rawFormat);
    if ((rc = avformat_find_stream_info(format.get(), nullptr)) < 0)
        return fail(QStringLiteral("avformat_find_stream_info: %1").arg(avError(rc)));

    const AVCodec *codec = nullptr;
    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                                &codec, 0);
    if (streamIndex < 0 || !codec)
        return fail(QStringLiteral("No playable video stream: %1").arg(avError(streamIndex)));
    AVStream *stream = format->streams[streamIndex];

    AVBufferRef *rawHw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!rawHw)
        return fail(QStringLiteral("Could not allocate D3D11VA hardware context"));
    std::unique_ptr<AVBufferRef, BufferCloser> hardware(rawHw);
    auto *hwContext = reinterpret_cast<AVHWDeviceContext *>(hardware->data);
    auto *d3dContext = reinterpret_cast<AVD3D11VADeviceContext *>(hwContext->hwctx);
    d3dContext->device = m_bridge->producerDevice();
    d3dContext->device->AddRef();
    if ((rc = av_hwdevice_ctx_init(hardware.get())) < 0)
        return fail(QStringLiteral("D3D11VA device init: %1").arg(avError(rc)));

    AVCodecContext *rawCodec = avcodec_alloc_context3(codec);
    if (!rawCodec)
        return fail(QStringLiteral("Could not allocate codec context"));
    std::unique_ptr<AVCodecContext, CodecCloser> decoder(rawCodec);
    if ((rc = avcodec_parameters_to_context(decoder.get(), stream->codecpar)) < 0)
        return fail(QStringLiteral("Codec parameters: %1").arg(avError(rc)));
    decoder->hw_device_ctx = av_buffer_ref(hardware.get());
    decoder->get_format = selectD3d11Format;
    if ((rc = avcodec_open2(decoder.get(), codec, nullptr)) < 0)
        return fail(QStringLiteral("Hardware decoder open: %1").arg(avError(rc)));

    const AVRational guessedRate = av_guess_frame_rate(format.get(), stream, nullptr);
    const double framesPerSecond = guessedRate.num > 0 && guessedRate.den > 0
        ? av_q2d(guessedRate) : 24.0;
    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    if (!frame || !packet)
        return fail(QStringLiteral("Could not allocate FFmpeg frame/packet"));

    using Clock = std::chrono::steady_clock;
    Clock::time_point wallStart{};
    double firstPts = 0.0;
    bool haveFirstPts = false;
    std::uint64_t sequence = 0;
    std::uint64_t fallbackIndex = 0;

    auto deliverFrames = [&]() -> bool {
        while (!stop.load()) {
            rc = avcodec_receive_frame(decoder.get(), frame.get());
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail(QStringLiteral("Decode frame: %1").arg(avError(rc)));
            m_bridge->noteDecoded();
            if (frame->format != AV_PIX_FMT_D3D11) {
                return fail(QStringLiteral("Decoder fell back from AV_PIX_FMT_D3D11 to %1")
                                .arg(QString::fromLatin1(av_get_pix_fmt_name(
                                    static_cast<AVPixelFormat>(frame->format)))));
            }

            auto *texture = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
            const UINT arraySlice = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            m_bridge->setSourceInfo(QStringLiteral("hevc"), QString::fromUtf8(codec->name),
                                    QStringLiteral("d3d11va"), dxgiFormatName(desc.Format),
                                    frame->width, frame->height, false);

            double pts = static_cast<double>(fallbackIndex++) / framesPerSecond;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                pts = frame->best_effort_timestamp * av_q2d(stream->time_base);
            if (!haveFirstPts) {
                firstPts = pts;
                wallStart = Clock::now();
                haveFirstPts = true;
            }
            const auto target = wallStart + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(std::max(0.0, pts - firstPts)));
            const auto framePeriod = std::chrono::duration<double>(1.0 / framesPerSecond);
            if (Clock::now() > target + framePeriod)
                m_bridge->noteLate();
            else
                std::this_thread::sleep_until(target);

            if (const auto slot = m_bridge->claimProducerSlot()) {
                QString error;
                ++sequence;
                if (!m_bridge->convertVideoFrame(*slot, texture, arraySlice, sequence,
                                                  frame->width, frame->height, desc.Format,
                                                  framesPerSecond,
                                                  frame->colorspace == AVCOL_SPC_BT709,
                                                  frame->color_range == AVCOL_RANGE_JPEG, error))
                    return fail(error);
                wakeConsumer();
            } else {
                m_bridge->noteProducerStarved();
                m_bridge->noteDropped();
            }
            av_frame_unref(frame.get());
        }
        return true;
    };

    while (!stop.load() && av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == streamIndex) {
            if ((rc = avcodec_send_packet(decoder.get(), packet.get())) < 0)
                return fail(QStringLiteral("Send packet: %1").arg(avError(rc)));
            if (!deliverFrames())
                return false;
        }
        av_packet_unref(packet.get());
    }
    if (!stop.load()) {
        avcodec_send_packet(decoder.get(), nullptr);
        if (!deliverFrames())
            return false;
    }
    return true;
}
