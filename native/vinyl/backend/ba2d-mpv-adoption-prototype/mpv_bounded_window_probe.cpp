#include "ColosseumMpvBridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QString>

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

mpv_handle *makeClient(bool paused)
{
    mpv_handle *client = mpv_create();
    require(client != nullptr, "mpv_create should succeed");
    require(mpv_set_option_string(client, "vo", "null") >= 0,
            "vo=null should configure");
    require(mpv_set_option_string(client, "ao", "null") >= 0,
            "ao=null should configure");
    require(mpv_set_option_string(client, "idle", "yes") >= 0,
            "idle=yes should configure");
    require(mpv_initialize(client) >= 0,
            "mpv client should initialize");

    int flag = paused ? 1 : 0;
    require(mpv_set_property(
                client,
                "pause",
                MPV_FORMAT_FLAG,
                &flag) >= 0,
            "pause property should configure");
    return client;
}

void loadFile(mpv_handle *client,
              const QString &fixture,
              const QString &perFileOptions = QString())
{
    const QByteArray resource =
        QFileInfo(fixture).absoluteFilePath().toUtf8();
    const QByteArray options = perFileOptions.toUtf8();

    if (perFileOptions.isEmpty()) {
        const char *args[] = {
            "loadfile",
            resource.constData(),
            "replace",
            nullptr,
        };
        require(mpv_command(client, args) >= 0,
                "plain loadfile command should succeed");
        return;
    }

    const char *args[] = {
        "loadfile",
        resource.constData(),
        "replace",
        "-1",
        options.constData(),
        nullptr,
    };
    require(mpv_command(client, args) >= 0,
            "bounded loadfile command should accept per-file start/end options");
}

void waitForLoaded(mpv_handle *client, double timeoutSeconds)
{
    double remaining = timeoutSeconds;
    while (remaining > 0.0) {
        const double slice = std::min(remaining, 0.1);
        remaining -= slice;
        mpv_event *event = mpv_wait_event(client, slice);
        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;
        if (event->event_id == MPV_EVENT_FILE_LOADED)
            return;
        if (event->event_id == MPV_EVENT_END_FILE && event->data) {
            const auto *end =
                static_cast<const mpv_event_end_file *>(event->data);
            if (end->error < 0) {
                std::fprintf(
                    stderr,
                    "load failed before FILE_LOADED: %s\n",
                    mpv_error_string(end->error));
                std::exit(1);
            }
        }
    }
    require(false, "FILE_LOADED should arrive before timeout");
}

std::optional<double> propertyDouble(
    mpv_handle *client,
    const char *name)
{
    double value = 0.0;
    if (mpv_get_property(
            client,
            name,
            MPV_FORMAT_DOUBLE,
            &value) < 0) {
        return std::nullopt;
    }
    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return value;
}

qint64 fixtureDurationMs(const QString &fixture)
{
    mpv_handle *client = makeClient(true);
    loadFile(client, fixture);
    waitForLoaded(client, 5.0);

    const auto duration = propertyDouble(client, "duration");
    require(duration.has_value() && *duration > 0.25,
            "fixture should expose a useful positive duration");
    const auto durationMs =
        vinyl::millisecondsFromMpvSeconds(*duration);
    require(durationMs.has_value() && *durationMs > 250,
            "fixture duration should convert to integer milliseconds");

    mpv_terminate_destroy(client);
    return *durationMs;
}

bool waitForPlaybackRestart(mpv_handle *client, double timeoutSeconds)
{
    double remaining = timeoutSeconds;
    while (remaining > 0.0) {
        const double slice = std::min(remaining, 0.1);
        remaining -= slice;
        mpv_event *event = mpv_wait_event(client, slice);
        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;
        if (event->event_id == MPV_EVENT_PLAYBACK_RESTART)
            return true;
        if (event->event_id == MPV_EVENT_END_FILE)
            return false;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    require(QString::fromLatin1(qVersion()) == QStringLiteral("6.11.1"),
            "W4-A bounded mpv probe must run against Qt 6.11.1");
    require(argc == 2,
            "W4-A bounded mpv probe requires one seekable local fixture");

    const QString fixture =
        QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    require(QFileInfo(fixture).isFile(),
            "bounded probe fixture should exist");

    const qint64 totalMs = fixtureDurationMs(fixture);

    qint64 startMs = std::max<qint64>(50, totalMs / 5);
    qint64 endMs = std::min<qint64>(totalMs - 20, startMs + 1000);
    if (endMs - startMs < 250) {
        startMs = totalMs / 10;
        endMs = totalMs * 9 / 10;
    }
    require(startMs >= 0
                && endMs > startMs
                && endMs <= totalMs,
            "fixture should provide a usable bounded playback window");

    const auto windowResult =
        PlaybackWindow::create(startMs, endMs);
    require(windowResult.ok(),
            "runtime playback window should validate");
    const PlaybackWindow window = *windowResult.value;
    const PlaybackTimeTransform transform(window);

    const QString options =
        mpvPerFilePlaybackWindowOptions(window);
    require(options.contains(QStringLiteral("start="))
                && options.contains(QStringLiteral("end=")),
            "bounded window must become concrete mpv start/end options");

    mpv_handle *client = makeClient(true);
    loadFile(client, fixture, options);
    waitForLoaded(client, 5.0);

    const auto loadedPhysicalSeconds =
        propertyDouble(client, "time-pos");
    require(loadedPhysicalSeconds.has_value(),
            "bounded FILE_LOADED should expose time-pos");
    const auto loadedPhysicalMs =
        millisecondsFromMpvSeconds(*loadedPhysicalSeconds);
    require(loadedPhysicalMs.has_value(),
            "loaded physical position should convert to ms");
    const auto loadedTrackMs =
        transform.trackPositionForPhysical(*loadedPhysicalMs);
    require(loadedTrackMs.has_value()
                && *loadedTrackMs <= 250,
            "bounded load should begin at logical track-relative zero");

    const qint64 logicalSeekMs =
        *window.durationMs() / 2;
    const auto physicalSeekMs =
        transform.physicalPositionForTrack(logicalSeekMs);
    require(physicalSeekMs.has_value(),
            "logical seek should map to physical source time");

    const QByteArray seekTarget =
        QByteArray::number(
            mpvSecondsFromMilliseconds(*physicalSeekMs),
            'f',
            3);
    const char *seekArgs[] = {
        "seek",
        seekTarget.constData(),
        "absolute",
        "exact",
        nullptr,
    };
    require(mpv_command(client, seekArgs) >= 0,
            "physical exact seek should succeed");
    require(waitForPlaybackRestart(client, 3.0),
            "bounded exact seek should complete");

    const auto observedSeekSeconds =
        propertyDouble(client, "time-pos");
    require(observedSeekSeconds.has_value(),
            "seek completion should expose physical time-pos");
    const auto observedSeekPhysicalMs =
        millisecondsFromMpvSeconds(*observedSeekSeconds);
    require(observedSeekPhysicalMs.has_value(),
            "seek completion should convert to ms");
    const auto observedSeekTrackMs =
        transform.trackPositionForPhysical(
            *observedSeekPhysicalMs);
    require(observedSeekTrackMs.has_value()
                && std::llabs(
                       *observedSeekTrackMs - logicalSeekMs)
                    <= 250,
            "physical seek completion should map back to logical track time");

    int paused = 0;
    require(mpv_set_property(
                client,
                "pause",
                MPV_FORMAT_FLAG,
                &paused) >= 0,
            "bounded playback should unpause");

    bool ended = false;
    int endReason = -1;
    std::optional<qint64> lastTrackMs;
    double remaining = 5.0;
    while (remaining > 0.0 && !ended) {
        const auto currentSeconds =
            propertyDouble(client, "time-pos");
        if (currentSeconds.has_value()) {
            const auto currentPhysicalMs =
                millisecondsFromMpvSeconds(*currentSeconds);
            if (currentPhysicalMs.has_value()) {
                lastTrackMs =
                    transform.trackPositionForPhysical(
                        *currentPhysicalMs);
            }
        }

        const double slice = std::min(remaining, 0.05);
        remaining -= slice;
        mpv_event *event = mpv_wait_event(client, slice);
        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;

        if (event->event_id == MPV_EVENT_END_FILE && event->data) {
            const auto *end =
                static_cast<const mpv_event_end_file *>(event->data);
            ended = true;
            endReason = end->reason;
            require(end->error >= 0,
                    "logical bound should not end as an mpv load error");
        }
    }

    require(ended,
            "bounded playback should emit END_FILE at logical end");
    require(endReason == MPV_END_FILE_REASON_EOF,
            "per-file end bound should be observed as logical EOF");
    require(lastTrackMs.has_value(),
            "bounded playback should expose track-relative progress");
    require(*lastTrackMs <= *window.durationMs(),
            "track-relative progress must never exceed logical window duration");
    require(*window.durationMs() - *lastTrackMs <= 300,
            "last observed track-relative position should approach logical end");

    mpv_terminate_destroy(client);

    qInfo().noquote()
        << "VINYL_W4A_BOUNDED_MPV_OK checks=" << g_checks
        << "fixtureDurationMs=" << totalMs
        << "startMs=" << startMs
        << "endMs=" << endMs
        << "logicalSeekMs=" << logicalSeekMs
        << "lastTrackMs=" << *lastTrackMs;

    return 0;
}
