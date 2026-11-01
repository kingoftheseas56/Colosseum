#include "MpvSidecarReference.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QString>

#include <mpv/client.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

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

int loadReplace(mpv_handle *owner, const QString &resource)
{
    const QByteArray encoded = QFileInfo(resource).absoluteFilePath().toUtf8();
    const char *args[] = {
        "loadfile",
        encoded.constData(),
        "replace",
        nullptr,
    };
    return mpv_command(owner, args);
}

void stopPlayback(mpv_handle *owner)
{
    const char *args[] = {"stop", nullptr};
    require(mpv_command(owner, args) >= 0,
            "stop command should succeed");
}

struct RawEvents final {
    std::vector<int64_t> starts;
    std::vector<int64_t> ends;
};

RawEvents drainUntilEnd(
    mpv_handle *sidecar,
    int64_t expectedEnd,
    double timeoutSeconds)
{
    RawEvents out;
    double remaining = timeoutSeconds;

    while (remaining > 0.0) {
        const double slice = remaining > 0.25 ? 0.25 : remaining;
        mpv_event *event = mpv_wait_event(sidecar, slice);
        remaining -= slice;

        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;

        if (event->event_id == MPV_EVENT_START_FILE && event->data) {
            const auto *data =
                static_cast<const mpv_event_start_file *>(event->data);
            out.starts.push_back(data->playlist_entry_id);
        } else if (event->event_id == MPV_EVENT_END_FILE && event->data) {
            const auto *data =
                static_cast<const mpv_event_end_file *>(event->data);
            out.ends.push_back(data->playlist_entry_id);
            if (data->playlist_entry_id == expectedEnd)
                break;
        }
    }

    return out;
}


RawEvents drainUntilStart(
    mpv_handle *sidecar,
    int64_t expectedStart,
    double timeoutSeconds)
{
    RawEvents out;
    double remaining = timeoutSeconds;

    while (remaining > 0.0) {
        const double slice = remaining > 0.25 ? 0.25 : remaining;
        mpv_event *event = mpv_wait_event(sidecar, slice);
        remaining -= slice;

        if (!event || event->event_id == MPV_EVENT_NONE)
            continue;

        if (event->event_id == MPV_EVENT_START_FILE && event->data) {
            const auto *data =
                static_cast<const mpv_event_start_file *>(event->data);
            out.starts.push_back(data->playlist_entry_id);
            if (data->playlist_entry_id == expectedStart)
                break;
        } else if (event->event_id == MPV_EVENT_END_FILE && event->data) {
            const auto *data =
                static_cast<const mpv_event_end_file *>(event->data);
            out.ends.push_back(data->playlist_entry_id);
        }
    }

    return out;
}

void appendEvents(RawEvents &target, RawEvents source)
{
    target.starts.insert(
        target.starts.end(),
        source.starts.begin(),
        source.starts.end());
    target.ends.insert(
        target.ends.end(),
        source.ends.begin(),
        source.ends.end());
}

bool contains(const std::vector<int64_t> &values, int64_t value)
{
    for (const int64_t candidate : values) {
        if (candidate == value)
            return true;
    }
    return false;
}

void requestRawEvents(mpv_handle *sidecar)
{
    require(mpv_request_event(
                sidecar, MPV_EVENT_START_FILE, 1) >= 0,
            "weak client should request START_FILE");
    require(mpv_request_event(
                sidecar, MPV_EVENT_FILE_LOADED, 1) >= 0,
            "weak client should request FILE_LOADED");
    require(mpv_request_event(
                sidecar, MPV_EVENT_END_FILE, 1) >= 0,
            "weak client should request END_FILE");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    require(argc >= 3,
            "probe requires two existing media fixture paths");

    const QString resourceA =
        QString::fromLocal8Bit(argv[1]);
    const QString resourceB =
        QString::fromLocal8Bit(argv[2]);

    require(QFileInfo(resourceA).isFile(),
            "resource A fixture should exist");
    require(QFileInfo(resourceB).isFile(),
            "resource B fixture should exist");

    mpv_handle *owner = mpv_create();
    require(owner != nullptr,
            "owner mpv handle should be created");

    require(mpv_set_option_string(owner, "vo", "null") >= 0,
            "vo=null should configure");
    require(mpv_set_option_string(owner, "ao", "null") >= 0,
            "ao=null should configure");
    require(mpv_set_option_string(owner, "idle", "yes") >= 0,
            "idle=yes should configure");
    require(mpv_initialize(owner) >= 0,
            "owner mpv handle should initialize");

    mpv_handle *sidecar =
        mpv_create_weak_client(owner, "vinyl_ba2db2");
    require(sidecar != nullptr,
            "weak sidecar should attach to same mpv core");
    requestRawEvents(sidecar);

    require(loadReplace(owner, resourceA) >= 0,
            "first load A should succeed");
    const std::optional<int64_t> firstA =
        vinyl::currentMpvPlaylistEntryId(sidecar);
    require(firstA.has_value(),
            "entry ID A1 should be synchronously queryable");

    require(loadReplace(owner, resourceA) >= 0,
            "same-resource replacement A2 should succeed");
    const std::optional<int64_t> secondA =
        vinyl::currentMpvPlaylistEntryId(sidecar);
    require(secondA.has_value(),
            "entry ID A2 should be synchronously queryable");
    require(*firstA != *secondA,
            "same-resource reload must receive a distinct playlist entry ID");

    const RawEvents sameResourceStarted =
        drainUntilStart(sidecar, *secondA, 4.0);
    require(contains(sameResourceStarted.starts, *secondA),
            "START_FILE should identify the captured A2 entry");

    stopPlayback(owner);
    const RawEvents sameResourceEnded =
        drainUntilEnd(sidecar, *secondA, 4.0);
    require(contains(sameResourceEnded.ends, *secondA),
            "END_FILE should preserve the captured A2 entry identity");

    for (const int64_t startId : sameResourceStarted.starts) {
        require(startId == *firstA || startId == *secondA,
                "same-resource START_FILE must resolve only to captured load IDs");
    }
    for (const int64_t endId : sameResourceStarted.ends) {
        require(endId == *firstA || endId == *secondA,
                "same-resource pre-start END_FILE must preserve captured load identity");
    }
    for (const int64_t endId : sameResourceEnded.ends) {
        require(endId == *firstA || endId == *secondA,
                "same-resource END_FILE must resolve only to captured load IDs");
    }

    require(loadReplace(owner, resourceA) >= 0,
            "rapid replacement A should succeed");
    const std::optional<int64_t> rapidA =
        vinyl::currentMpvPlaylistEntryId(sidecar);
    require(rapidA.has_value(),
            "rapid A entry ID should be synchronously queryable");

    require(loadReplace(owner, resourceB) >= 0,
            "rapid replacement B should succeed");
    const std::optional<int64_t> rapidB =
        vinyl::currentMpvPlaylistEntryId(sidecar);
    require(rapidB.has_value(),
            "rapid B entry ID should be synchronously queryable");
    require(*rapidA != *rapidB,
            "rapid replacement loads must have distinct concrete IDs");

    RawEvents rapidEvents =
        drainUntilStart(sidecar, *rapidB, 4.0);
    require(contains(rapidEvents.starts, *rapidB),
            "winning rapid START_FILE must match captured B entry ID");
    stopPlayback(owner);
    appendEvents(
        rapidEvents,
        drainUntilEnd(sidecar, *rapidB, 4.0));
    require(contains(rapidEvents.ends, *rapidB),
            "winning rapid END_FILE must match captured B entry ID");

    for (const int64_t startId : rapidEvents.starts) {
        require(startId == *rapidA || startId == *rapidB,
                "raw START_FILE must resolve only to captured concrete load IDs");
    }
    for (const int64_t endId : rapidEvents.ends) {
        require(endId == *rapidA || endId == *rapidB,
                "raw END_FILE must resolve only to captured concrete load IDs");
    }

    mpv_destroy(sidecar);
    mpv_terminate_destroy(owner);

    qInfo().noquote()
        << "VINYL_BA2DB2_PLAYLIST_ID_OK checks=" << g_checks
        << "sameResource=" << *firstA << "->" << *secondA
        << "rapid=" << *rapidA << "->" << *rapidB;

    return 0;
}
