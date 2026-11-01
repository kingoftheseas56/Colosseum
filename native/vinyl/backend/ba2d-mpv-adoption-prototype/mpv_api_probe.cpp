#include <MpvController>

#include <QCoreApplication>
#include <QDebug>

#include <mpv/client.h>

#include <cstdlib>
#include <type_traits>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    static_assert(
        std::is_same<
            decltype(&MpvController::mpv),
            mpv_handle *(MpvController::*)() const>::value,
        "Installed MpvQt must expose the shared libmpv core handle.");

    const auto createWeakClient = &mpv_create_weak_client;
    const auto requestEvent = &mpv_request_event;
    const auto waitEvent = &mpv_wait_event;
    const auto setWakeup = &mpv_set_wakeup_callback;
    const auto destroyClient = &mpv_destroy;

    require(createWeakClient != nullptr,
            "libmpv must export mpv_create_weak_client");
    require(requestEvent != nullptr,
            "libmpv must export mpv_request_event");
    require(waitEvent != nullptr,
            "libmpv must export mpv_wait_event");
    require(setWakeup != nullptr,
            "libmpv must export mpv_set_wakeup_callback");
    require(destroyClient != nullptr,
            "libmpv must export mpv_destroy");

    require(MPV_EVENT_START_FILE != MPV_EVENT_NONE,
            "raw START_FILE event must exist");
    require(MPV_EVENT_FILE_LOADED != MPV_EVENT_NONE,
            "raw FILE_LOADED event must exist");
    require(MPV_EVENT_END_FILE != MPV_EVENT_NONE,
            "raw END_FILE event must exist");
    require(MPV_EVENT_SEEK != MPV_EVENT_NONE,
            "raw SEEK event must exist");
    require(MPV_EVENT_PLAYBACK_RESTART != MPV_EVENT_NONE,
            "raw PLAYBACK_RESTART event must exist");

    mpv_event_start_file start{};
    start.playlist_entry_id = 42;
    require(start.playlist_entry_id == 42,
            "START_FILE payload must expose playlist entry identity");

    mpv_event_end_file end{};
    end.playlist_entry_id = 42;
    end.reason = MPV_END_FILE_REASON_ERROR;
    end.error = MPV_ERROR_LOADING_FAILED;
    require(end.playlist_entry_id == start.playlist_entry_id,
            "END_FILE payload must carry matching playlist entry identity");
    require(end.error == MPV_ERROR_LOADING_FAILED,
            "END_FILE payload must preserve coarse libmpv error evidence");

    qInfo().noquote()
        << "VINYL_BA2D_MPV_API_OK checks=" << g_checks
        << "qt=" << qVersion()
        << "mpvClientApi=" << MPV_CLIENT_API_VERSION;
    return 0;
}
