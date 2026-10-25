// Focused tests for the opt-in Android Nuvio playback backend.
//
// These tests drive the PRODUCTION NuvioStreamBackend (and the production
// C-API table plumbing) against a scripted provider that implements the
// pinned nuvio_engine C API in-process. No engine library is loaded, no
// torrent/network activity happens; the provider's scripted events exercise
// the real correlation, validation and lifecycle logic in the adapter.
//
// The provider mirrors the real engine's request-id semantics: add_torrent
// and prepare_stream hand out distinct incrementing request ids and stamp
// them on the events that complete those requests, so the adapter's exact
// correlation is exercised with nonzero ids on every path.
#include <QtTest>

#include "player/nuvioengineapi.h"
#include "player/nuviostreambackend.h"

#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND
#include "player/streamserver.h"
#endif

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>

using Colosseum::Nuvio::EngineApi;
using Colosseum::Nuvio::NuvioStreamBackend;

namespace {

constexpr auto kInfoHashA = "0123456789abcdef0123456789abcdef01234567";
constexpr auto kInfoHashB = "fedcba9876543210fedcba9876543210fedcba98";
constexpr auto kUrlA = "http://127.0.0.1:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
constexpr auto kUrlB = "http://127.0.0.1:49151/stream/bbbb2222bbbb2222bbbb2222bbbb2222";
constexpr auto kBadUrl = "http://10.13.37.5:1234/stream/deadbeefdeadbeefdeadbeefdeadbeef";

// Malformed-URL shapes the engine must never be allowed to publish. Each must
// be rejected before any signal surfaces and the created route must retire.
enum class BadUrlKind {
    NonLoopbackHost,
    PortZero,
    PortTooLarge,
    PortSixtySixK,
    MissingPort,
    WithQuery,
    WithFragment,
    WithCredentials,
    EmptyToken,
    NonHexToken,
    HttpsScheme,
    LocalhostName,
};

const char *badUrlText(const BadUrlKind kind) {
    switch (kind) {
    case BadUrlKind::NonLoopbackHost:
        return kBadUrl;
    case BadUrlKind::PortZero:
        return "http://127.0.0.1:0/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::PortTooLarge:
        return "http://127.0.0.1:99999/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::PortSixtySixK:
        return "http://127.0.0.1:65536/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::MissingPort:
        return "http://127.0.0.1/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::WithQuery:
        return "http://127.0.0.1:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111?x=1";
    case BadUrlKind::WithFragment:
        return "http://127.0.0.1:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111#frag";
    case BadUrlKind::WithCredentials:
        return "http://user:pass@127.0.0.1:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::EmptyToken:
        return "http://127.0.0.1:49151/stream/";
    case BadUrlKind::NonHexToken:
        return "http://127.0.0.1:49151/stream/zzzz2222zzzz2222zzzz2222zzzz2222";
    case BadUrlKind::HttpsScheme:
        return "https://127.0.0.1:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    case BadUrlKind::LocalhostName:
        return "http://localhost:49151/stream/aaaa1111aaaa1111aaaa1111aaaa1111";
    }
    return kBadUrl;
}

// Scripted in-process provider for the pinned C API. Calls arrive on the
// backend's worker thread; scripted state is guarded so the test thread can
// inspect and steer it.
struct FakeEngine {
    std::mutex mutex;
    nuvio_engine_status createResult = NUVIO_ENGINE_STATUS_OK;
    bool badUrl = false;
    BadUrlKind badUrlKind = BadUrlKind::NonLoopbackHost;
    // When nonempty, prepared completions for that torrent id are withheld
    // from the event queue (held for the test to release manually).
    QString holdPreparedTorrent;
    int destroyDelayMs = 0;
    bool destroyEntered = false;
    int createDelayMs = 0;
    bool createEntered = false;
    size_t fileCount = 1;
    std::uint64_t fileSize = 10 * 1024 * 1024;
    std::uint64_t verifiedBytes = 3 * 1024 * 1024;
    std::uint32_t connectedPeers = 7;
    std::uint32_t unchokedPeers = 2;
    std::uint64_t downloadRate = 65536;

    int createCount = 0;
    std::uint32_t lastInactivityTimeoutMs = 0xffffffffu;
    int destroyCount = 0;
    int addTorrentCount = 0;
    // Per add_torrent request: source type and magnet text (empty for DATA).
    std::vector<int> receivedSourceTypes;
    QStringList receivedMagnets;
    int prepareCount = 0;
    std::uint64_t nextRequestId = 0;
    QStringList stopStreamIds;
    QStringList removedTorrentIds;
    // When set, TORRENT_REMOVED completions are held for the test to release.
    bool holdRemoved = false;
    std::vector<nuvio_engine_event> heldRemoved;

    std::deque<nuvio_engine_event> events;
    std::optional<nuvio_engine_event> heldPrepared;

    void pushEvent(const nuvio_engine_event &event) {
        events.push_back(event);
    }

    void reset() {
        std::lock_guard lock(mutex);
        createResult = NUVIO_ENGINE_STATUS_OK;
        badUrl = false;
        badUrlKind = BadUrlKind::NonLoopbackHost;
        holdPreparedTorrent.clear();
        destroyDelayMs = 0;
        destroyEntered = false;
        createDelayMs = 0;
        createEntered = false;
        fileCount = 1;
        fileSize = 10 * 1024 * 1024;
        verifiedBytes = 3 * 1024 * 1024;
        connectedPeers = 7;
        unchokedPeers = 2;
        downloadRate = 65536;
        createCount = 0;
        destroyCount = 0;
        addTorrentCount = 0;
        receivedSourceTypes.clear();
        receivedMagnets.clear();
        prepareCount = 0;
        nextRequestId = 0;
        stopStreamIds.clear();
        removedTorrentIds.clear();
        holdRemoved = false;
        heldRemoved.clear();
        events.clear();
        heldPrepared.reset();
    }

    nuvio_engine_event makeEvent(const nuvio_engine_event_type type, const char *torrentId) {
        nuvio_engine_event event{};
        event.struct_size = sizeof(event);
        event.type = type;
        event.file_index = 0;
        event.file_size = fileSize;
        std::snprintf(event.torrent_id, sizeof(event.torrent_id), "%s", torrentId);
        return event;
    }
};

FakeEngine &fake() {
    static FakeEngine engine;
    return engine;
}

nuvio_engine *g_engineHandle = reinterpret_cast<nuvio_engine *>(0xC0FFE);
std::atomic<int> g_liveFakeEngines{0};

void fake_config_init(nuvio_engine_config *config, uint32_t size) {
    std::memset(config, 0, size);
    config->struct_size = size;
    // Mirror the engine's defaulted idle route expiry (30 s).
    config->stream_inactivity_timeout_milliseconds = 30'000;
}

void fake_torrent_request_init(nuvio_engine_torrent_request *request, uint32_t size) {
    std::memset(request, 0, size);
    request->struct_size = size;
}

void fake_event_init(nuvio_engine_event *event, uint32_t size) {
    std::memset(event, 0, size);
    event->struct_size = size;
}

void fake_stream_request_init(nuvio_engine_stream_request *request, uint32_t size) {
    std::memset(request, 0, size);
    request->struct_size = size;
}

void fake_stats_init(nuvio_engine_stats *stats, uint32_t size) {
    std::memset(stats, 0, size);
    stats->struct_size = size;
}

void fake_stream_stats_init(nuvio_engine_stream_stats *stats, uint32_t size) {
    std::memset(stats, 0, size);
    stats->struct_size = size;
}

uint32_t fake_api_version() {
    return NUVIO_ENGINE_API_VERSION;
}

const char *fake_version_string() {
    return "fake-nuvio";
}

const char *fake_status_message(nuvio_engine_status) {
    return "fake status";
}

nuvio_engine_status fake_create(const nuvio_engine_config *config, nuvio_engine **engine) {
    int delay = 0;
    nuvio_engine_status result = NUVIO_ENGINE_STATUS_OK;
    {
        std::lock_guard lock(fake().mutex);
        ++fake().createCount;
        fake().lastInactivityTimeoutMs = config->stream_inactivity_timeout_milliseconds;
        fake().createEntered = true;
        delay = fake().createDelayMs;
        result = fake().createResult;
    }
    if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (result != NUVIO_ENGINE_STATUS_OK) {
        *engine = nullptr;
        return result;
    }
    *engine = g_engineHandle;
    ++g_liveFakeEngines;
    return NUVIO_ENGINE_STATUS_OK;
}

void fake_destroy(nuvio_engine *) {
    const int delay = [] {
        std::lock_guard lock(fake().mutex);
        fake().destroyEntered = true;
        return fake().destroyDelayMs;
    }();
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    std::lock_guard lock(fake().mutex);
    ++fake().destroyCount;
    --g_liveFakeEngines;
}

nuvio_engine_status fake_add_torrent(
    nuvio_engine *,
    const nuvio_engine_torrent_request *request,
    uint64_t *requestId
) {
    std::lock_guard lock(fake().mutex);
    ++fake().addTorrentCount;
    // The torrent id travels inside the magnet URI; explicit metadata is
    // always the task-owned synthetic torrent A.
    QString magnet = QString::fromUtf8(request->magnet_uri ? request->magnet_uri : "");
    fake().receivedSourceTypes.push_back(static_cast<int>(request->source_type));
    fake().receivedMagnets << magnet;
    const auto mark = magnet.lastIndexOf(QLatin1Char(':'));
    const auto id = request->source_type == NUVIO_ENGINE_TORRENT_SOURCE_DATA
        ? std::string(kInfoHashA) : magnet.mid(mark + 1).toStdString();
    auto event = fake().makeEvent(NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY, id.c_str());
    event.request_id = ++fake().nextRequestId;
    if (requestId != nullptr) {
        *requestId = event.request_id;
    }
    fake().pushEvent(event);
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_poll_event(nuvio_engine *, nuvio_engine_event *event) {
    std::lock_guard lock(fake().mutex);
    if (fake().events.empty()) {
        return NUVIO_ENGINE_STATUS_NO_EVENT;
    }
    *event = fake().events.front();
    fake().events.pop_front();
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_get_file_count(nuvio_engine *, const char *, size_t *count) {
    std::lock_guard lock(fake().mutex);
    *count = fake().fileCount;
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_get_file(
    nuvio_engine *,
    const char *,
    size_t index,
    nuvio_engine_file *file
) {
    std::lock_guard lock(fake().mutex);
    std::memset(file, 0, sizeof(*file));
    file->struct_size = sizeof(*file);
    file->index = static_cast<std::uint32_t>(index);
    file->offset = 0;
    file->size = fake().fileSize;
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_prepare_stream(
    nuvio_engine *,
    const nuvio_engine_stream_request *request,
    uint64_t *requestId
) {
    std::lock_guard lock(fake().mutex);
    ++fake().prepareCount;
    auto event = fake().makeEvent(
        NUVIO_ENGINE_EVENT_STREAM_PREPARED,
        request->torrent_id
    );
    event.request_id = ++fake().nextRequestId;
    event.file_index = request->file_index;
    const char *url = kUrlA;
    if (fake().badUrl) {
        url = badUrlText(fake().badUrlKind);
    } else if (QString::fromLatin1(request->torrent_id) == kInfoHashB) {
        url = kUrlB;
    }
    std::snprintf(
        event.stream_id,
        sizeof(event.stream_id),
        "stream-%d",
        fake().prepareCount
    );
    std::snprintf(event.stream_url, sizeof(event.stream_url), "%s", url);
    if (requestId != nullptr) {
        *requestId = event.request_id;
    }
    if (fake().holdPreparedTorrent == QString::fromLatin1(request->torrent_id)) {
        fake().heldPrepared = event;
    } else {
        fake().pushEvent(event);
    }
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_stop_stream(nuvio_engine *, const char *streamId, uint64_t *) {
    std::lock_guard lock(fake().mutex);
    fake().stopStreamIds << QString::fromUtf8(streamId);
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_remove_torrent(nuvio_engine *, const char *torrentId, uint64_t *) {
    std::lock_guard lock(fake().mutex);
    fake().removedTorrentIds << QString::fromUtf8(torrentId);
    auto event = fake().makeEvent(NUVIO_ENGINE_EVENT_TORRENT_REMOVED, torrentId);
    if (fake().holdRemoved)
        fake().heldRemoved.push_back(event);
    else
        fake().pushEvent(event);
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_get_stats(nuvio_engine *, nuvio_engine_stats *stats) {
    std::lock_guard lock(fake().mutex);
    fake_stats_init(stats, sizeof(*stats));
    stats->connected_peers = fake().connectedPeers;
    stats->unchoked_peers = fake().unchokedPeers;
    stats->download_rate_bytes_per_second = fake().downloadRate;
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status fake_get_stream_stats(
    nuvio_engine *,
    const char *,
    nuvio_engine_stream_stats *stats
) {
    std::lock_guard lock(fake().mutex);
    fake_stream_stats_init(stats, sizeof(*stats));
    stats->file_index = 0;
    stats->file_size = fake().fileSize;
    stats->verified_file_bytes = fake().verifiedBytes;
    return NUVIO_ENGINE_STATUS_OK;
}

EngineApi fakeApi() {
    EngineApi api;
    api.api_version = fake_api_version;
    api.version_string = fake_version_string;
    api.status_message = fake_status_message;
    api.config_init_sized = fake_config_init;
    api.torrent_request_init_sized = fake_torrent_request_init;
    api.event_init_sized = fake_event_init;
    api.stream_request_init_sized = fake_stream_request_init;
    api.stats_init_sized = fake_stats_init;
    api.stream_stats_init_sized = fake_stream_stats_init;
    api.create = fake_create;
    api.destroy = fake_destroy;
    api.add_torrent = fake_add_torrent;
    api.poll_event = fake_poll_event;
    api.get_file_count = fake_get_file_count;
    api.get_file = fake_get_file;
    api.prepare_stream = fake_prepare_stream;
    api.stop_stream = fake_stop_stream;
    api.remove_torrent = fake_remove_torrent;
    api.get_stats = fake_get_stats;
    api.get_stream_stats = fake_get_stream_stats;
    return api;
}

struct FakeEngineReset {
    FakeEngineReset() {
        // Prior QObject teardown is intentionally asynchronous. Drain the
        // scripted previous instance before resetting its shared fake state.
        for (int i = 0; i < 500 && g_liveFakeEngines.load() != 0; ++i)
            QTest::qWait(20);
        Q_ASSERT(g_liveFakeEngines.load() == 0);
        fake().reset();
    }
};

} // namespace

class TestNuvioStreamBackend : public QObject {
    Q_OBJECT

private slots:
    void backgroundPropertyChangesMustNotify();
    void backgroundStartingAndRapidTransitionsMustNotify();
    void startupFailureSurfacesAsUnavailable();
    void validMetadataAndPreparePublishLoopbackUrl();
    void staleRequestCompletionIsSuppressedAndRetired();
    void rapidSourceSwitchRetiresPriorRoute();
    void stopRetiresRouteAndRemovesTorrent();
    void stopDuringInflightPrepareSuppressesAndRemoves();
    void stopKeepsPrefetchOwnedTorrent();
    void replayAfterStopWaitsForPendingRemoval();
    // Task014: a demand deferred behind a pending removal keeps its input.
    void heldRemovalReplayKeepsExplicitMetadata();
    void heldRemovalReplayKeepsCustomMagnet();
    void heldRemovalPrefetchUsesFetchGeneration();
    void playerOwnedRoutesDoNotIdleExpire();
    void engineStoppedActiveRouteIsNotReused();
    void isolatedSessionStartsFromEmptyRoots();
    void prefetchDoesNotHijackActivePlayback();
    // Receiving-review probes: these two reproduced accepted P1 defects and
    // must never regress.
    void differentTorrentPrefetchPreservesPlayback();
    void sameTorrentOldPrepareIsNotAcceptedForNewFile();
    void prefetchOfDifferentFileInActiveTorrentIsDeferred();
    void prefetchRouteIsPromotedByPlayWithoutReprepare();
    void inFlightPrefetchIsPromotedByForegroundPlay();
    void malformedUrlIsRejected();
    void oldErrorEventsDoNotFailCurrentChain();
    void backgroundLifecycleInvalidatesAndReconstructs();
    void backgroundDownloadKeepsSharedRouteWithoutPlayback();
    void backgroundDownloadCanFinishPreparing();
    void backgroundDownloadLeaseLossQuiesces();
    void backgroundSharedPrepareKeepsDownload_data();
    void backgroundSharedPrepareKeepsDownload();
    void completedDownloadRetiresOnlyDownloadOwnership();
    void closingPlayerKeepsSameSourceDownload();
    void finalDownloadCompletionReleasesIdleEngine();
    void downloadCompletionBeforeHostStartReleasesRoute();
    void differentFilePlaybackCannotReplaceDownloadRoute();
    // Task014: Nokia T20 foreground return. QML re-demands before the C++
    // lifecycle slot runs; that slot's fence then drops the demand.
    void demandStampedBeforeForegroundRestoreIsFenced();
    void staleStatsCallbacksStopAfterRetirement();
    void statisticsCorrelateWithVerifiedBytes();
    void ownedTeardownDuringInflightPrepare();
    void teardownDoesNotBlockGuiAndDestroysExactlyOnce();
    void queuedReadyMustNotPublishAfterBackground();
    void destructionDuringBackgroundDestroyMustNotBlockGui();
    void olderDeferredFileMustNotOverrideLatestDifferentTorrent();
    void destructionDuringDelayedCreateMustNotBlockGui();
#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND
    void facadeReflectsEngineUnavailableState();
    void facadeSyncsForegroundBeforeDemand();
#endif
};

namespace {
NuvioStreamBackend::Configuration testConfiguration() {
    NuvioStreamBackend::Configuration configuration;
    configuration.dataDirectory = QStringLiteral("/tmp/nuvio-test/data");
    configuration.cacheDirectory = QStringLiteral("/tmp/nuvio-test/cache");
    configuration.tlsCaBundlePath = QStringLiteral("/tmp/nuvio-test/ca.pem");
    return configuration;
}
} // namespace

void TestNuvioStreamBackend::startupFailureSurfacesAsUnavailable() {
    FakeEngineReset reset;
    fake().createResult = NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QVERIFY(backend.available());
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("failed to start")));
    QVERIFY(backend.engineUnavailable());
    QVERIFY(!backend.ready());
}

void TestNuvioStreamBackend::validMetadataAndPreparePublishLoopbackUrl() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QSignalSpy starting(&backend, &NuvioStreamBackend::startingChanged);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto url = ready.first().at(0).toString();
    const auto hash = ready.first().at(1).toString();
    QCOMPARE(url, QString::fromLatin1(kUrlA));
    QCOMPARE(hash, QString::fromLatin1(kInfoHashA));
    QCOMPARE(fetch.count(), 0);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), url);
    QVERIFY(backend.ready());
    QVERIFY(!backend.starting());
    QCOMPARE(starting.count(), 2); // starting=true then false
    QCOMPARE(fake().addTorrentCount, 1);
    QCOMPARE(fake().prepareCount, 1);
}

void TestNuvioStreamBackend::staleRequestCompletionIsSuppressedAndRetired() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    // A newer demand supersedes A before its URL arrives.
    fake().holdPreparedTorrent.clear();
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 2, 10000);
    {
        std::lock_guard lock(fake().mutex);
        if (fake().heldPrepared.has_value()) {
            fake().pushEvent(*fake().heldPrepared);
            fake().heldPrepared.reset();
        }
    }
    // Only B's URL may surface; A's route must be retired instead.
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(ready.first().at(0).toString(), QString::fromLatin1(kUrlB));
    QTRY_COMPARE_WITH_TIMEOUT(fake().stopStreamIds.count(QString::fromLatin1("stream-1")), 1, 10000);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
}

void TestNuvioStreamBackend::rapidSourceSwitchRetiresPriorRoute() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(ready.at(1).at(0).toString(), QString::fromLatin1(kUrlB));
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    QCOMPARE(backend.streamUrl(kInfoHashB, 0), QString::fromLatin1(kUrlB));
    QTRY_VERIFY_WITH_TIMEOUT(fake().stopStreamIds.contains(QString::fromLatin1("stream-1")), 10000);
    // Replacing the source must also end the prior torrent's session demand.
    QTRY_VERIFY_WITH_TIMEOUT(fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashA)), 10000);
    QVERIFY(!fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashB)));
}

void TestNuvioStreamBackend::stopRetiresRouteAndRemovesTorrent() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy readyChanged(&backend, &NuvioStreamBackend::readyChanged);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 10000);
    backend.stop();
    QTRY_VERIFY_WITH_TIMEOUT(fake().stopStreamIds.contains(QString::fromLatin1("stream-1")), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.ready(), 10000);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    QTest::qWait(300);
    QCOMPARE(ready.count(), 1);
    // Exactly one removal; a later play reconstructs demand from scratch.
    QCOMPARE(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().addTorrentCount, 2);
}

void TestNuvioStreamBackend::stopDuringInflightPrepareSuppressesAndRemoves() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.starting(), 10000);
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.starting(), 10000);
    {
        std::lock_guard lock(fake().mutex);
        QVERIFY(fake().heldPrepared.has_value());
        fake().pushEvent(*fake().heldPrepared);
        fake().heldPrepared.reset();
    }
    // The last owner was removed, so engine teardown retires every pending
    // route. A completion from that destroyed session cannot publish anything.
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    QTest::qWait(300);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
}

void TestNuvioStreamBackend::stopKeepsPrefetchOwnedTorrent() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    QTest::qWait(300);
    // The download owner's torrent and route survive the player closing.
    QVERIFY(!fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashB)));
    QVERIFY(!fake().stopStreamIds.contains(QString::fromLatin1("stream-2")));
}

void TestNuvioStreamBackend::replayAfterStopWaitsForPendingRemoval() {
    FakeEngineReset reset;
    fake().holdRemoved = true;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    // Reopened before the engine finished removing the torrent.
    backend.play(kInfoHashA, 0);
    QTest::qWait(300);
    QCOMPARE(fake().addTorrentCount, 1);
    QCOMPARE(ready.count(), 1);
    QVERIFY(backend.starting());
    {
        std::lock_guard lock(fake().mutex);
        for (const auto &event : fake().heldRemoved) fake().pushEvent(event);
        fake().heldRemoved.clear();
    }
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().addTorrentCount, 2);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), ready.at(1).at(0).toString());
}

namespace {
void releaseHeldRemovals() {
    std::lock_guard lock(fake().mutex);
    for (const auto &event : fake().heldRemoved) fake().pushEvent(event);
    fake().heldRemoved.clear();
}
} // namespace

void TestNuvioStreamBackend::heldRemovalReplayKeepsExplicitMetadata() {
    FakeEngineReset reset;
    fake().holdRemoved = true;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    const QByteArray metadata("task-owned synthetic metadata for the injected provider");
    backend.playTorrentData(metadata, kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(fake().receivedSourceTypes.at(0), int(NUVIO_ENGINE_TORRENT_SOURCE_DATA));
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    // Reopened with the same metadata while removal is still pending.
    backend.playTorrentData(metadata, kInfoHashA, 0);
    QTest::qWait(300);
    QCOMPARE(fake().addTorrentCount, 1);
    QVERIFY(backend.starting());
    releaseHeldRemovals();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().receivedSourceTypes.size(), size_t(2));
    // The replayed demand must still carry explicit metadata, not a magnet:
    // isolated fixture admission rejects a metadata-less demand.
    QCOMPARE(fake().receivedSourceTypes.at(1), int(NUVIO_ENGINE_TORRENT_SOURCE_DATA));
}

void TestNuvioStreamBackend::heldRemovalReplayKeepsCustomMagnet() {
    FakeEngineReset reset;
    fake().holdRemoved = true;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    const auto custom = QStringLiteral("magnet:?dn=task014-custom&xt=urn:btih:") +
        QString::fromLatin1(kInfoHashA);
    backend.playMagnet(custom, kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(fake().receivedMagnets.at(0), custom);
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    backend.playMagnet(custom, kInfoHashA, 0);
    QTest::qWait(300);
    QCOMPARE(fake().addTorrentCount, 1);
    releaseHeldRemovals();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().receivedMagnets.size(), qsizetype(2));
    QCOMPARE(fake().receivedMagnets.at(1), custom);
}

void TestNuvioStreamBackend::heldRemovalPrefetchUsesFetchGeneration() {
    FakeEngineReset reset;
    fake().holdRemoved = true;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.stop();
    QTRY_COMPARE_WITH_TIMEOUT(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1, 10000);
    // Foreground generation is now 2 (play + stop); the prefetch is fetch
    // generation 1. It waits on the pending removal of the same torrent.
    backend.prefetch(kInfoHashA, 0);
    QTest::qWait(300);
    QCOMPARE(fake().addTorrentCount, 1);
    // Closing the player again must not drop the unrelated prefetch owner.
    backend.stop();
    QTest::qWait(300);
    QCOMPARE(fake().removedTorrentIds.count(QString::fromLatin1(kInfoHashA)), 1);
    releaseHeldRemovals();
    // The deferred prefetch is fenced by its own (fetch) generation.
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(fake().addTorrentCount, 2);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(fetch.first().at(1).toString(), QString::fromLatin1(kInfoHashA));
}

void TestNuvioStreamBackend::playerOwnedRoutesDoNotIdleExpire() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    // A fully buffered file leaves its route idle while playback continues;
    // the backend retires routes explicitly, so engine idle expiry is off.
    QCOMPARE(fake().lastInactivityTimeoutMs, 0u);
}

void TestNuvioStreamBackend::engineStoppedActiveRouteIsNotReused() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(fake().prepareCount, 1);
    {
        std::lock_guard lock(fake().mutex);
        auto event = fake().makeEvent(NUVIO_ENGINE_EVENT_STREAM_STOPPED, kInfoHashA);
        std::snprintf(event.stream_id, sizeof(event.stream_id), "%s", "stream-1");
        fake().pushEvent(event);
    }
    // The engine no longer serves that route: it must not be published again.
    QTRY_COMPARE_WITH_TIMEOUT(backend.streamUrl(kInfoHashA, 0), QString(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.ready(), 10000);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().prepareCount, 2);
}

void TestNuvioStreamBackend::isolatedSessionStartsFromEmptyRoots() {
    FakeEngineReset reset;
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto staleIn = [&](const QString &root) {
        QDir().mkpath(root + QStringLiteral("/nuvio-engine-state/resume"));
        QFile stale(root + QStringLiteral("/nuvio-engine-state/resume/stale.resume"));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("saved state from a previous session");
    };
    auto configuration = testConfiguration();
    configuration.dataDirectory = temporary.filePath(QStringLiteral("data"));
    configuration.cacheDirectory = temporary.filePath(QStringLiteral("cache"));
    staleIn(configuration.dataDirectory);
    staleIn(configuration.cacheDirectory);

    // Negative control: ordinary sessions keep their resume data.
    {
        NuvioStreamBackend backend(fakeApi(), configuration);
        QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
        backend.play(kInfoHashA, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QVERIFY(QFile::exists(configuration.dataDirectory +
                              QStringLiteral("/nuvio-engine-state/resume/stale.resume")));
    }
    FakeEngineReset secondReset;
    configuration.requireFixtureIsolation = true;
    NuvioStreamBackend isolated(fakeApi(), configuration);
    isolated.verifyIsolation();
    QTRY_COMPARE_WITH_TIMEOUT(fake().createCount, 1, 10000);
    for (const auto &root : {configuration.dataDirectory, configuration.cacheDirectory}) {
        QVERIFY(QDir(root).exists());
        QVERIFY(QDir(root).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
    }
}

void TestNuvioStreamBackend::prefetchDoesNotHijackActivePlayback() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    // The engine replaces prior routes for the same torrent, so a prefetch of
    // the playing source must reuse the route, never re-prepare it.
    QCOMPARE(fetch.first().at(0).toString(), QString::fromLatin1(kUrlA));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(fake().prepareCount, 1);
    QCOMPARE(fake().addTorrentCount, 1);
    // A prefetch for a different torrent answers with fetchReady, not
    // streamReady, so the kept-alive player page is never hijacked.
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 2, 10000);
    QCOMPARE(fetch.at(1).at(0).toString(), QString::fromLatin1(kUrlB));
    QCOMPARE(ready.count(), 1);
}

void TestNuvioStreamBackend::differentTorrentPrefetchPreservesPlayback() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString::fromLatin1(kUrlA));
    std::lock_guard lock(fake().mutex);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
}

void TestNuvioStreamBackend::sameTorrentOldPrepareIsNotAcceptedForNewFile() {
    FakeEngineReset reset;
    fake().fileCount = 2;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    // Capture the first file's completion (the provider stamped its real
    // request id) before the second selection is demanded.
    nuvio_engine_event oldEvent;
    {
        std::lock_guard lock(fake().mutex);
        oldEvent = *fake().heldPrepared;
    }
    backend.play(kInfoHashA, 1);
    // Deliver ONLY the old file's completion: it retires the superseded
    // request and unblocks the new file's prepare (still withheld).
    {
        std::lock_guard lock(fake().mutex);
        fake().pushEvent(oldEvent);
    }
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 2, 10000);
    QTest::qWait(400);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(backend.streamUrl(kInfoHashA, 1), QString());
    // Completing the CURRENT request must still publish exactly once, for
    // file 1 — suppression must not swallow the legitimate completion.
    {
        std::lock_guard lock(fake().mutex);
        if (fake().heldPrepared.has_value()) {
            fake().pushEvent(*fake().heldPrepared);
            fake().heldPrepared.reset();
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(ready.first().at(1).toString(), QString::fromLatin1(kInfoHashA));
    QCOMPARE(ready.first().at(2).toInt(), 1);
    QCOMPARE(backend.streamUrl(kInfoHashA, 1), QString::fromLatin1(kUrlA));
}

void TestNuvioStreamBackend::prefetchOfDifferentFileInActiveTorrentIsDeferred() {
    FakeEngineReset reset;
    fake().fileCount = 2;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    // The engine replaces routes for the SAME torrent, so preparing file 1 of
    // the playing torrent now would destroy playback. Foreground playback
    // owns the torrent; the prefetch is deferred, not issued.
    backend.prefetch(kInfoHashA, 1);
    QTest::qWait(400);
    QCOMPARE(fake().prepareCount, 1);
    QCOMPARE(fetch.count(), 0);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString::fromLatin1(kUrlA));
    // Once playback moves to another torrent, the deferred prefetch may run.
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(fetch.first().at(0).toString(), QString::fromLatin1(kUrlA));
    QCOMPARE(fetch.first().at(2).toInt(), 1);
    // A0 (play), B0 (play), A1 (deferred prefetch): exactly three prepares.
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 3, 10000);
}

void TestNuvioStreamBackend::prefetchRouteIsPromotedByPlayWithoutReprepare() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    const int prepares = fake().prepareCount;
    // Playing the prefetched source promotes the existing route instead of
    // re-preparing it (the engine would replace the route and stall playback).
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(ready.at(1).at(0).toString(), QString::fromLatin1(kUrlB));
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, prepares, 10000);
    QCOMPARE(backend.streamUrl(kInfoHashB, 0), QString::fromLatin1(kUrlB));
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    QTRY_VERIFY_WITH_TIMEOUT(fake().stopStreamIds.contains(QString::fromLatin1("stream-1")), 10000);
}

void TestNuvioStreamBackend::inFlightPrefetchIsPromotedByForegroundPlay() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashB);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 2, 10000);
    // Both the download and player still own the pending source. Each must
    // receive its completion, or the download remains stuck resolving.
    backend.play(kInfoHashB, 0);
    fake().holdPreparedTorrent.clear();
    {
        std::lock_guard lock(fake().mutex);
        if (fake().heldPrepared.has_value()) {
            fake().pushEvent(*fake().heldPrepared);
            fake().heldPrepared.reset();
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(ready.at(1).at(0).toString(), QString::fromLatin1(kUrlB));
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(backend.streamUrl(kInfoHashB, 0), QString::fromLatin1(kUrlB));
}

void TestNuvioStreamBackend::malformedUrlIsRejected() {
    const BadUrlKind kinds[] = {
        BadUrlKind::NonLoopbackHost,
        BadUrlKind::PortZero,
        BadUrlKind::PortTooLarge,
        BadUrlKind::PortSixtySixK,
        BadUrlKind::MissingPort,
        BadUrlKind::WithQuery,
        BadUrlKind::WithFragment,
        BadUrlKind::WithCredentials,
        BadUrlKind::EmptyToken,
        BadUrlKind::NonHexToken,
        BadUrlKind::HttpsScheme,
        BadUrlKind::LocalhostName,
    };
    for (const auto kind : kinds) {
        FakeEngineReset reset;
        fake().badUrl = true;
        fake().badUrlKind = kind;
        NuvioStreamBackend backend(fakeApi(), testConfiguration());
        QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
        QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
        backend.play(kInfoHashA, 0);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QVERIFY2(
            errors.first().first().toString().contains(QStringLiteral("invalid stream url")),
            badUrlText(kind)
        );
        QCOMPARE(ready.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(
            fake().stopStreamIds.count(QString::fromLatin1("stream-1")), 1, 10000
        );
        QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
        QVERIFY(!backend.ready());
    }
}

void TestNuvioStreamBackend::oldErrorEventsDoNotFailCurrentChain() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashB);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 2, 10000);
    // Reconstruct the shape of a retired request's error: a request id nobody
    // issued for the live chains, for the same torrent.
    std::uint64_t staleAddId = 0;
    {
        std::lock_guard lock(fake().mutex);
        staleAddId = fake().nextRequestId + 900;
    }
    auto error = fake().makeEvent(NUVIO_ENGINE_EVENT_TORRENT_ERROR, kInfoHashB);
    error.request_id = staleAddId;
    std::snprintf(error.message, sizeof(error.message), "%s", "stale failure");
    {
        std::lock_guard lock(fake().mutex);
        fake().pushEvent(error);
    }
    QTest::qWait(300);
    // The foreign error must not fail the in-flight prefetch.
    QCOMPARE(errors.count(), 0);
    fake().holdPreparedTorrent.clear();
    {
        std::lock_guard lock(fake().mutex);
        if (fake().heldPrepared.has_value()) {
            fake().pushEvent(*fake().heldPrepared);
            fake().heldPrepared.reset();
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(errors.count(), 0);
}

void TestNuvioStreamBackend::backgroundLifecycleInvalidatesAndReconstructs() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QVERIFY(backend.ready());
    backend.setForeground(false);
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    QVERIFY(!backend.ready());
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    QTRY_VERIFY_WITH_TIMEOUT(fake().stopStreamIds.contains(QString::fromLatin1("stream-1")), 10000);
    // The next explicit foreground demand reconstructs fresh engine state.
    backend.setForeground(true);
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().createCount, 2);
    QCOMPARE(ready.at(1).at(0).toString(), QString::fromLatin1(kUrlB));
}

void TestNuvioStreamBackend::backgroundDownloadKeepsSharedRouteWithoutPlayback() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    backend.setForeground(false);
    QTest::qWait(300);
    QCOMPARE(fake().destroyCount, 0);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
    QVERIFY(!backend.ready());
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    backend.play(kInfoHashB, 0); // Background playback is still forbidden.
    QTest::qWait(200);
    QCOMPARE(fake().addTorrentCount, 1);
    QCOMPARE(ready.count(), 1);
    backend.setForeground(true);
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString());
    backend.play(kInfoHashA, 0); // Explicit Play may reuse the download route.
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().prepareCount, 1);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, false)));
    QTest::qWait(200);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString::fromLatin1(kUrlA));
    backend.stop();
    QTRY_VERIFY_WITH_TIMEOUT(fake().stopStreamIds.contains(QStringLiteral("stream-1")), 10000);
}

void TestNuvioStreamBackend::backgroundDownloadCanFinishPreparing() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    backend.setForeground(false);
    {
        std::lock_guard lock(fake().mutex);
        QVERIFY(fake().heldPrepared.has_value());
        fake().pushEvent(*fake().heldPrepared);
        fake().heldPrepared.reset();
    }
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(fake().destroyCount, 0);
    QCOMPARE(fetch.at(0).at(0).toString(), QString::fromLatin1(kUrlA));
}

void TestNuvioStreamBackend::backgroundDownloadLeaseLossQuiesces() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    backend.setForeground(false);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, false)));
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    {
        std::lock_guard lock(fake().mutex);
        QVERIFY(fake().heldPrepared.has_value());
        fake().pushEvent(*fake().heldPrepared);
        fake().heldPrepared.reset();
    }
    backend.prefetch(kInfoHashB, 0);
    QTest::qWait(300);
    QCOMPARE(fetch.count(), 0);
    QCOMPARE(fake().createCount, 1);
}

void TestNuvioStreamBackend::backgroundSharedPrepareKeepsDownload_data() {
    QTest::addColumn<bool>("playFirst");
    QTest::newRow("play-then-download") << true;
    QTest::newRow("download-then-play") << false;
}

void TestNuvioStreamBackend::backgroundSharedPrepareKeepsDownload() {
    QFETCH(bool, playFirst);
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    if (playFirst) backend.play(kInfoHashA, 0);
    else backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    if (playFirst) backend.prefetch(kInfoHashA, 0);
    else backend.play(kInfoHashA, 0);
    backend.setForeground(false);
    QTest::qWait(100);
    {
        std::lock_guard lock(fake().mutex);
        QVERIFY(fake().heldPrepared.has_value());
        fake().pushEvent(*fake().heldPrepared);
        fake().heldPrepared.reset();
    }
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(fake().prepareCount, 1);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
}

void TestNuvioStreamBackend::completedDownloadRetiresOnlyDownloadOwnership() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, false)));
    QTRY_VERIFY_WITH_TIMEOUT(fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashB)), 10000);
    QVERIFY(!fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashA)));
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
    QCOMPARE(backend.streamUrl(kInfoHashA, 0), QString::fromLatin1(kUrlA));
}

void TestNuvioStreamBackend::closingPlayerKeepsSameSourceDownload() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    backend.stop();
    QTest::qWait(300);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
    QVERIFY(!fake().removedTorrentIds.contains(QString::fromLatin1(kInfoHashA)));
}

void TestNuvioStreamBackend::finalDownloadCompletionReleasesIdleEngine() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, true)));
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    QVERIFY(QMetaObject::invokeMethod(&backend, "setBackgroundDownloadsActive",
        Qt::DirectConnection, Q_ARG(bool, false)));
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 1000);
    QCOMPARE(fake().createCount, 1);
}

void TestNuvioStreamBackend::downloadCompletionBeforeHostStartReleasesRoute() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    backend.stopPrefetch();
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    QVERIFY(fake().stopStreamIds.contains(QStringLiteral("stream-1")));
}

void TestNuvioStreamBackend::differentFilePlaybackCannotReplaceDownloadRoute() {
    FakeEngineReset reset;
    fake().fileCount = 2;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy fetch(&backend, &NuvioStreamBackend::fetchReady);
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.prefetch(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fetch.count(), 1, 10000);
    backend.play(kInfoHashA, 1);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 1000);
    QVERIFY(errors.at(0).at(0).toString().contains(QStringLiteral("download")));
    QCOMPARE(fake().prepareCount, 1);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!fake().stopStreamIds.contains(QStringLiteral("stream-1")));
    // Pausing/completing that download releases the constraint. Explicit Play
    // can then prepare the requested file without inheriting the old route.
    backend.stopPrefetch();
    backend.play(kInfoHashA, 1);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(fake().prepareCount, 2);
}

void TestNuvioStreamBackend::demandStampedBeforeForegroundRestoreIsFenced() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.setForeground(false);
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    // The observed order: demand first, foreground restore second. The
    // restore's fence makes the demand stale; nothing is admitted.
    backend.play(kInfoHashA, 0);
    backend.setForeground(true);
    QTest::qWait(400);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(fake().addTorrentCount, 1);
    // Foreground restored first (what the facade's probe guarantees): admitted.
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(fake().addTorrentCount, 2);
}

void TestNuvioStreamBackend::staleStatsCallbacksStopAfterRetirement() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy stats(&backend, &NuvioStreamBackend::streamStats);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.watchStats(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(stats.count(), 1, 10000);
    // Backgrounding retires the route and tears the engine down; no queued
    // stats callback may publish the retired route afterwards.
    backend.setForeground(false);
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    QTest::qWait(300);
    const int settled = stats.count();
    QTest::qWait(300);
    QCOMPARE(stats.count(), settled);
}

void TestNuvioStreamBackend::statisticsCorrelateWithVerifiedBytes() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy stats(&backend, &NuvioStreamBackend::streamStats);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend.watchStats(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(stats.count(), 1, 10000);
    const auto mapped = stats.first().at(2).toMap();
    QCOMPARE(mapped.value(QStringLiteral("peers")).toInt(), 7);
    QCOMPARE(mapped.value(QStringLiteral("unchoked")).toInt(), 2);
    QCOMPARE(mapped.value(QStringLiteral("downloaded")).toULongLong(), 3ull * 1024 * 1024);
    QCOMPARE(mapped.value(QStringLiteral("downloadSpeed")).toULongLong(), 65536ull);
    QCOMPARE(mapped.value(QStringLiteral("streamLen")).toULongLong(), 10ull * 1024 * 1024);
    QCOMPARE(mapped.value(QStringLiteral("streamProgress")).toDouble(), 0.3);
    backend.unwatchStats();
    QTest::qWait(300);
    const int settled = stats.count();
    QTest::qWait(300);
    QCOMPARE(stats.count(), settled);
}

void TestNuvioStreamBackend::ownedTeardownDuringInflightPrepare() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    {
        NuvioStreamBackend backend(fakeApi(), testConfiguration());
        QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
        backend.play(kInfoHashA, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
        QVERIFY(backend.starting());
        // Destruction with a request still in flight hands engine teardown to
        // the joined reaper (see the delayed test below); the engine is
        // destroyed exactly once despite the in-flight request.
    }
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
}

void TestNuvioStreamBackend::teardownDoesNotBlockGuiAndDestroysExactlyOnce() {
    FakeEngineReset reset;
    fake().destroyDelayMs = 600;
    QElapsedTimer teardownClock;
    {
        NuvioStreamBackend backend(fakeApi(), testConfiguration());
        QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
        backend.play(kInfoHashA, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        // The GUI ownership path must not wait for engine destruction: the
        // slow destroy runs elsewhere while the caller keeps making progress.
        teardownClock.start();
    }
    const auto teardownElapsed = teardownClock.elapsed();
    QVERIFY2(
        teardownElapsed < 300,
        qPrintable(QStringLiteral("destructor blocked the GUI for %1 ms").arg(teardownElapsed))
    );
    // While teardown completes in the background, this thread keeps servicing
    // the event loop (GUI progress), then observes exactly-once destruction.
    int timerTicks = 0;
    QTimer ticker;
    ticker.setInterval(25);
    QObject::connect(&ticker, &QTimer::timeout, [&] { ++timerTicks; });
    ticker.start();
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    ticker.stop();
    QVERIFY2(timerTicks >= 5, "event loop made no progress during teardown");
    QTest::qWait(150);
    // Exactly once: neither a leak-driven repeat nor a skipped destroy.
    QCOMPARE(fake().destroyCount, 1);
}

#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND
void TestNuvioStreamBackend::facadeReflectsEngineUnavailableState() {
    // Host compile/behavior coverage for the Android facade translation unit
    // (streamserver_android.cpp with COLOSSEUM_ANDROID_NUVIO_BACKEND and a
    // missing engine library). Honest host coverage only: it does not
    // establish Android JNI, ABI, or packaging behavior.
    StreamServer server;
    QVERIFY(server.engineUnavailable());
    QVERIFY(!server.ready());
    QVERIFY(!server.starting());
    QVERIFY(server.streamUrl(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0).isEmpty());
    QSignalSpy errors(&server, &StreamServer::streamError);
    server.play(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
}

void TestNuvioStreamBackend::facadeSyncsForegroundBeforeDemand() {
    // Each demand reads the live foreground state before it reaches the
    // backend, independent of lifecycle signal delivery order.
    StreamServer server;
    int probes = 0;
    server.setForegroundProbe([&probes] { ++probes; return true; });
    server.play(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);
    QCOMPARE(probes, 1);
    server.prefetch(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);
    QCOMPARE(probes, 2);
}
#endif


// Exact three behavioral probes ported from the receiving review.
void TestNuvioStreamBackend::queuedReadyMustNotPublishAfterBackground() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    // Wait for the worker to prepare its route WITHOUT delivering Qt callbacks.
    for (int i=0; i<2000 && !backend.ready(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    QVERIFY(backend.ready());
    QCOMPARE(ready.count(), 0);
    backend.setForeground(false);
    QCoreApplication::processEvents();
    QCOMPARE(ready.count(), 0);
}

void TestNuvioStreamBackend::destructionDuringBackgroundDestroyMustNotBlockGui() {
    FakeEngineReset reset;
    fake().destroyDelayMs = 800;
    auto backend = std::make_unique<NuvioStreamBackend>(fakeApi(), testConfiguration());
    QSignalSpy ready(backend.get(), &NuvioStreamBackend::streamReady);
    backend->play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    backend->setForeground(false);
    bool entered=false;
    for (int i=0; i<2000; ++i) {
        { std::lock_guard lock(fake().mutex); entered=fake().destroyEntered; }
        if (entered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QVERIFY(entered);
    QElapsedTimer elapsed;
    elapsed.start();
    backend.reset();
    const auto duration=elapsed.elapsed();
    qInfo() << "Destructor during background destroy milliseconds:" << duration;
    QVERIFY2(duration < 200, "GUI caller waited for slow background engine destruction");
}

void TestNuvioStreamBackend::olderDeferredFileMustNotOverrideLatestDifferentTorrent() {
    FakeEngineReset reset;
    fake().fileCount=2;
    fake().holdPreparedTorrent=QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    backend.play(kInfoHashA, 1);
    QTest::qWait(200); // worker queues this same-torrent demand behind file 0
    backend.play(kInfoHashB, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(ready.first().at(1).toString(), QString::fromLatin1(kInfoHashB));
    {
        std::lock_guard lock(fake().mutex);
        fake().holdPreparedTorrent.clear();
        fake().pushEvent(*fake().heldPrepared);
        fake().heldPrepared.reset();
    }
    QTest::qWait(500);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(backend.streamUrl(kInfoHashB,0), QString::fromLatin1(kUrlB));
}

void TestNuvioStreamBackend::destructionDuringDelayedCreateMustNotBlockGui() {
    FakeEngineReset reset;
    fake().createDelayMs = 650;
    auto backend = std::make_unique<NuvioStreamBackend>(fakeApi(), testConfiguration());
    backend->play(kInfoHashA, 0);
    bool entered = false;
    for (int i = 0; i < 2000; ++i) {
        { std::lock_guard lock(fake().mutex); entered = fake().createEntered; }
        if (entered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QVERIFY(entered);
    QElapsedTimer elapsed;
    elapsed.start();
    backend.reset();
    const auto duration = elapsed.elapsed();
    QVERIFY2(duration < 200, "GUI caller blocked on engine create");
    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(25);
    connect(&ticker, &QTimer::timeout, [&]{ ++ticks; });
    ticker.start();
    QTRY_COMPARE_WITH_TIMEOUT(fake().destroyCount, 1, 10000);
    ticker.stop();
    QVERIFY(ticks > 2);
    QCOMPARE(fake().createCount, 1);
    QCOMPARE(fake().destroyCount, 1);
}

// Receiving-review behavioral regression, ported without changing its assertions.
void TestNuvioStreamBackend::backgroundPropertyChangesMustNotify() {
    FakeEngineReset reset;
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy changed(&backend, &NuvioStreamBackend::readyChanged);
    backend.play(kInfoHashA, 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QVERIFY(backend.ready());
    QCoreApplication::processEvents();
    changed.clear();
    backend.setForeground(false);
    QTest::qWait(300);
    QVERIFY(!backend.ready());
    QVERIFY2(changed.count() > 0, "ready changed to false without its Q_PROPERTY notification");
}

void TestNuvioStreamBackend::backgroundStartingAndRapidTransitionsMustNotify() {
    FakeEngineReset reset;
    fake().holdPreparedTorrent = QString::fromLatin1(kInfoHashA);
    NuvioStreamBackend backend(fakeApi(), testConfiguration());
    QSignalSpy starting(&backend, &NuvioStreamBackend::startingChanged);
    QSignalSpy ready(&backend, &NuvioStreamBackend::streamReady);
    QSignalSpy errors(&backend, &NuvioStreamBackend::streamError);
    backend.play(kInfoHashA, 0);
    QTRY_VERIFY_WITH_TIMEOUT(backend.starting(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(fake().prepareCount, 1, 10000);
    QCoreApplication::processEvents();
    starting.clear();
    backend.setForeground(false);
    QVERIFY(!backend.starting());
    QCOMPARE(starting.count(), 1);
    backend.setForeground(true);
    QVERIFY(!backend.starting());
    QVERIFY(!backend.ready());
    // The held completion belongs to the previous lifecycle and must not
    // surface or resurrect property state when the foreground is restored.
    {
        std::lock_guard lock(fake().mutex);
        if (fake().heldPrepared.has_value()) {
            fake().pushEvent(*fake().heldPrepared);
            fake().heldPrepared.reset();
        }
    }
    QTest::qWait(400);
    QVERIFY(!backend.starting());
    QVERIFY(!backend.ready());
    QCOMPARE(ready.count(), 0);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(starting.count(), 1);
}

QTEST_MAIN(TestNuvioStreamBackend)
#include "tst_nuvio_stream_backend.moc"
