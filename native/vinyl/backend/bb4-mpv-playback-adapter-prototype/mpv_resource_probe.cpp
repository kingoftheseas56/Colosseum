#include "ResolvedPlaybackSource.h"

#include <mpv/client.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>

#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (condition)
        return;
    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustSource(const vinyl::SourceCandidateResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustBb4(const vinyl::MpvPlaybackAdapterResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "BB4 mpv probe must execute against Qt 6.11.1");
    require(argc == 2, "BB4 mpv probe requires one local media fixture");

    const QString fixture =
        QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    require(QFileInfo::exists(fixture), "mpv probe fixture must exist");

    const RecordingMbid recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "probe RecordingMbid should parse");
    const SourceCandidateKey key(
        mustSource(
            SourceProviderId::parse(QStringLiteral("bb4.probe")),
            "probe provider should parse"),
        mustSource(
            SourceCandidateId::parse(QStringLiteral("local-fixture")),
            "probe candidate should parse"));

    const ResolvedTrackSource source = mustBb4(
        ResolvedTrackSource::create(
            recording,
            key,
            SourceKind::LocalFile,
            SourceResolutionPath::Direct,
            ResolvedPlaybackResourceKind::LocalPath,
            fixture),
        "local probe source should construct");
    const PlaybackLoadInput input = mustBb4(
        MpvPlaybackSourceAdapter::toPlaybackLoadInput(source, recording),
        "probe source should map to PlaybackLoadInput");

    mpv_handle *handle = mpv_create();
    require(handle != nullptr, "mpv_create should succeed");
    mpv_set_option_string(handle, "terminal", "no");
    mpv_set_option_string(handle, "vo", "null");
    mpv_set_option_string(handle, "ao", "null");
    require(mpv_initialize(handle) >= 0, "mpv_initialize should succeed");

    const QByteArray resourceUtf8 = input.resource().toUtf8();
    const char *command[] = {
        "loadfile",
        resourceUtf8.constData(),
        "replace",
        nullptr,
    };
    require(mpv_command(handle, command) >= 0,
            "libmpv should accept BB4 mapped local resource");

    bool loaded = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !loaded) {
        mpv_event *event = mpv_wait_event(handle, 0.10);
        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            loaded = true;
            break;
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end && end->error < 0) {
                qCritical().noquote()
                    << "FAIL: mpv ended before FILE_LOADED:"
                    << mpv_error_string(end->error);
                mpv_destroy(handle);
                return 1;
            }
        }
    }

    require(loaded, "libmpv should emit FILE_LOADED for BB4 mapped local resource");
    const char *stop[] = {"stop", nullptr};
    mpv_command(handle, stop);
    mpv_destroy(handle);

    qInfo().noquote()
        << "VINYL_BB4_MPV_RESOURCE_PROBE_OK"
        << "qt=" << qVersion()
        << "resource=" << input.resource();
    return 0;
}