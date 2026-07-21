#include "player2/video/D3D11VideoPipeline.h"
#include "player2/video/Player2VideoItem.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaObject>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

using namespace Colosseum::Player2;

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

struct FormatCloser { void operator()(AVFormatContext *p) const { avformat_close_input(&p); } };
struct CodecCloser { void operator()(AVCodecContext *p) const { avcodec_free_context(&p); } };
struct FrameCloser { void operator()(AVFrame *p) const { av_frame_free(&p); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct BufferCloser { void operator()(AVBufferRef *p) const { av_buffer_unref(&p); } };

bool decodeFixture(const QString &path, D3D11VideoPipeline *pipeline, Player2VideoItem *item,
                   const std::atomic_bool *stop, QString *failure)
{
    AVFormatContext *rawFormat = nullptr;
    int result = avformat_open_input(&rawFormat, path.toUtf8().constData(), nullptr, nullptr);
    if (result < 0) {
        *failure = QStringLiteral("avformat_open_input: %1").arg(avError(result));
        return false;
    }
    std::unique_ptr<AVFormatContext, FormatCloser> format(rawFormat);
    if ((result = avformat_find_stream_info(format.get(), nullptr)) < 0) {
        *failure = QStringLiteral("avformat_find_stream_info: %1").arg(avError(result));
        return false;
    }
    const AVCodec *codec = nullptr;
    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                                &codec, 0);
    if (streamIndex < 0 || !codec) {
        *failure = QStringLiteral("No video stream: %1").arg(avError(streamIndex));
        return false;
    }
    QString hardwareError;
    std::unique_ptr<AVBufferRef, BufferCloser> hardware(
        pipeline->createDecoderDeviceContext(&hardwareError));
    if (!hardware) {
        *failure = hardwareError;
        return false;
    }
    std::unique_ptr<AVCodecContext, CodecCloser> decoder(avcodec_alloc_context3(codec));
    if (!decoder ||
        (result = avcodec_parameters_to_context(decoder.get(),
                                                format->streams[streamIndex]->codecpar)) < 0) {
        *failure = QStringLiteral("Codec setup failed: %1").arg(avError(result));
        return false;
    }
    decoder->hw_device_ctx = av_buffer_ref(hardware.get());
    decoder->get_format = selectD3d11Format;
    if ((result = avcodec_open2(decoder.get(), codec, nullptr)) < 0) {
        *failure = QStringLiteral("Hardware decoder open: %1").arg(avError(result));
        return false;
    }
    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    if (!frame || !packet) {
        *failure = QStringLiteral("FFmpeg frame allocation failed");
        return false;
    }

    const AVRational rate = av_guess_frame_rate(format.get(), format->streams[streamIndex], nullptr);
    const double fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 24.0;
    const auto framePeriod = std::chrono::duration<double>(1.0 / fps);
    quint64 sequence = 0;
    quint64 decodedAttempts = 0;
    auto nextFrame = std::chrono::steady_clock::now();

    auto receive = [&]() -> bool {
        while (sequence < 120 && !stop->load()) {
            result = avcodec_receive_frame(decoder.get(), frame.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                return true;
            if (result < 0) {
                *failure = QStringLiteral("Decode frame: %1").arg(avError(result));
                return false;
            }
            pipeline->noteDecoded();
            ++decodedAttempts;
            if (frame->format != AV_PIX_FMT_D3D11) {
                *failure = QStringLiteral("Decoder returned %1 instead of AV_PIX_FMT_D3D11")
                               .arg(QString::fromLatin1(av_get_pix_fmt_name(
                                   static_cast<AVPixelFormat>(frame->format))));
                return false;
            }
            nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                framePeriod);
            std::this_thread::sleep_until(nextFrame);
            QString submitError;
            const VideoFrameToken token{1, sequence + 1,
                                        static_cast<qint64>((sequence * 1'000'000.0) / fps)};
            if (pipeline->submitDecodedFrame(frame.get(), token, &submitError)) {
                ++sequence;
                QMetaObject::invokeMethod(item, &QQuickItem::update, Qt::QueuedConnection);
            } else if (!submitError.isEmpty()) {
                *failure = submitError;
                return false;
            }
            av_frame_unref(frame.get());
            if (decodedAttempts >= 600 && sequence < 60) {
                *failure = QStringLiteral("Render consumer starved the shared texture ring");
                return false;
            }
        }
        return true;
    };

    while (sequence < 120 && !stop->load() &&
           av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == streamIndex) {
            if ((result = avcodec_send_packet(decoder.get(), packet.get())) < 0 || !receive()) {
                if (failure->isEmpty())
                    *failure = QStringLiteral("Send packet: %1").arg(avError(result));
                return false;
            }
        }
        av_packet_unref(packet.get());
    }
    if (stop->load()) {
        *failure = QStringLiteral("Fixture cancelled by watchdog");
        return false;
    }
    return sequence >= 60;
}

bool writeReport(const QString &path, const D3D11VideoPipeline::Diagnostics &diagnostics,
                 bool decodePassed, const QString &decodeError)
{
    const QJsonObject report{
        {QStringLiteral("adapterMatch"), diagnostics.adapterMatch},
        {QStringLiteral("sharedFences"), diagnostics.sharedFences},
        {QStringLiteral("qtAdapter"), diagnostics.qtAdapter},
        {QStringLiteral("producerAdapter"), diagnostics.producerAdapter},
        {QStringLiteral("decoded"), static_cast<qint64>(diagnostics.decoded)},
        {QStringLiteral("submitted"), static_cast<qint64>(diagnostics.submitted)},
        {QStringLiteral("presented"), static_cast<qint64>(diagnostics.presented)},
        {QStringLiteral("producerStarved"), static_cast<qint64>(diagnostics.producerStarved)},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(diagnostics.cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(diagnostics.deviceErrors)},
        {QStringLiteral("hardwareFormat"), diagnostics.hardwareFormat},
        {QStringLiteral("inputFormat"), diagnostics.inputFormat},
        {QStringLiteral("pipelineError"), diagnostics.error},
        {QStringLiteral("decodePassed"), decodePassed},
        {QStringLiteral("decodeError"), decodeError}
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) >= 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const int fileOption = arguments.indexOf(QStringLiteral("--file"));
    const int reportOption = arguments.indexOf(QStringLiteral("--report"));
    if (fileOption < 0 || fileOption + 1 >= arguments.size() || reportOption < 0 ||
        reportOption + 1 >= arguments.size()) {
        std::cerr << "usage: player2_video_fixture_test --file PATH --report PATH\n";
        return EXIT_FAILURE;
    }
    const QString mediaPath = arguments.at(fileOption + 1);
    const QString reportPath = arguments.at(reportOption + 1);
    if (!QFileInfo::exists(mediaPath)) {
        std::cerr << "fixture media does not exist\n";
        return EXIT_FAILURE;
    }

    D3D11VideoPipeline pipeline;
    QQuickWindow window;
    window.setPersistentGraphics(true);
    window.setPersistentSceneGraph(true);
    window.setFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    window.resize(960, 540);
    auto *item = new Player2VideoItem(window.contentItem());
    item->setSize(QSizeF(960, 540));
    item->setVideoPipeline(&pipeline);
    window.show();

    std::atomic_bool workerStarted{false};
    std::atomic_bool workerFinished{false};
    std::atomic_bool stop{false};
    bool decodePassed = false;
    QString decodeError;
    bool timedOut = false;
    std::thread worker;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &application, [&] {
        const auto diagnostics = pipeline.diagnostics();
        if (!workerStarted && diagnostics.adapterMatch) {
            workerStarted = true;
            worker = std::thread([&] {
                decodePassed = decodeFixture(mediaPath, &pipeline, item, &stop, &decodeError);
                workerFinished = true;
            });
        }
        if (workerFinished) {
            poll.stop();
            application.quit();
        }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &application, [&] {
        timedOut = true;
        stop = true;
        application.quit();
    });
    poll.start();
    watchdog.start(20'000);
    application.exec();
    stop = true;
    if (worker.joinable())
        worker.join();
    if (timedOut && decodeError.isEmpty())
        decodeError = QStringLiteral("Fixture timed out");

    const auto diagnostics = pipeline.diagnostics();
    const bool gatePassed = decodePassed && diagnostics.adapterMatch && diagnostics.sharedFences &&
        diagnostics.cpuTransfers == 0 && diagnostics.deviceErrors == 0 &&
        diagnostics.hardwareFormat == QStringLiteral("d3d11va") &&
        diagnostics.submitted >= 60 && diagnostics.presented > 0;
    if (!writeReport(reportPath, diagnostics, decodePassed, decodeError)) {
        std::cerr << "could not write diagnostics report\n";
        return EXIT_FAILURE;
    }
    if (!gatePassed) {
        std::cerr << "player2_video_fixture_test: FAIL: "
                  << decodeError.toStdString() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_video_fixture_test: PASS\n";
    return EXIT_SUCCESS;
}
