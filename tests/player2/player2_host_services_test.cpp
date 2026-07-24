// Task 14 — the orchestration seam contract. The player asks (typed requests); the host resolves
// EXACTLY ONCE via the matching signal with data, an empty collection, or an error. The lab host
// (HarnessHostServices) answers from deterministic fixtures and never touches a real catalog/source.
//
// Runs headless: HarnessHostServices constructs a default (GPU-free) D3D11VideoPipeline and a
// Player2Session with no pipeline attached, so none of the GPU/render path is exercised — only the
// request -> deterministic-fixture -> resolved-signal contract.
//
// Fixture conventions (this test IS the spec the harness implements):
//   media "series-mid"  : adjacent(+1)->"series-mid#next", adjacent(-1)->"series-mid#prev" (data)
//   media "series-head" : adjacent(-1)-> { dead:true } (a real series boundary, not an error)
//   media "empty"       : every collection request resolves to [] (no data, not an error)
//   media "boom"        : every request resolves to { error:<non-empty> }
//   any other id        : sources/subs/segments/metadata resolve with >=1 deterministic entry
//   download source "ok": queued -> active -> ready (a monotonic STATE STREAM, not resolve-once)
//   download source "no": failed with a non-empty error

#include "player2/host/HarnessHostServices.h"
#include "player2/host/Player2HostServices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Runs `trigger`, then spins the event loop until `done()` or the deadline. Returns whether it
// completed in time — a request that never resolves is a contract failure, not a hang.
bool pump(const std::function<void()> &trigger, const std::function<bool()> &done,
          int deadlineMs = 2'000)
{
    QEventLoop loop;
    bool finished = false;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done()) {
            finished = true;
            loop.quit();
        }
    });
    QTimer::singleShot(deadlineMs, &loop, &QEventLoop::quit);
    poll.start();
    trigger();
    if (!done())
        loop.exec();
    else
        finished = true;
    return finished || done();
}

// A single-resolve request must fire its signal exactly once with the expected media id.
void adjacentEpisodeResolvesOnceWithData()
{
    HarnessHostServices host;
    int count = 0;
    QVariantMap episode;
    QString gotMedia;
    int gotDir = 0;
    QObject::connect(&host, &Player2HostServices::adjacentEpisodeResolved, &host,
                     [&](const QString &m, int dir, const QVariantMap &e) {
                         ++count; gotMedia = m; gotDir = dir; episode = e;
                     });
    const bool ok = pump([&] { host.requestAdjacentEpisode(QStringLiteral("series-mid"), 1); },
                         [&] { return count > 0; });
    require(ok, "adjacent next never resolved within its deadline");
    require(count == 1, "adjacent next resolved more than once");
    require(gotMedia == QStringLiteral("series-mid") && gotDir == 1, "resolved wrong media/direction");
    require(!episode.value(QStringLiteral("mediaId")).toString().isEmpty(),
            "next episode must carry a media id");
    require(episode.value(QStringLiteral("error")).toString().isEmpty(), "next episode is not an error");
    require(!episode.value(QStringLiteral("dead")).toBool(), "a real next episode is not a boundary");
}

void adjacentEpisodeBoundaryResolvesDeadNotError()
{
    HarnessHostServices host;
    QVariantMap episode;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::adjacentEpisodeResolved, &host,
                     [&](const QString &, int, const QVariantMap &e) { ++count; episode = e; });
    const bool ok = pump([&] { host.requestAdjacentEpisode(QStringLiteral("series-head"), -1); },
                         [&] { return count > 0; });
    require(ok && count == 1, "boundary adjacent must resolve exactly once");
    require(episode.value(QStringLiteral("dead")).toBool(), "series boundary must resolve dead:true");
    require(episode.value(QStringLiteral("error")).toString().isEmpty(),
            "a boundary is not an error");
}

void alternateSourcesResolveRankedListWithCurrent()
{
    HarnessHostServices host;
    QVariantList sources;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::alternateSourcesResolved, &host,
                     [&](const QString &, const QVariantList &s) { ++count; sources = s; });
    const bool ok = pump([&] { host.requestAlternateSources(QStringLiteral("tt-movie")); },
                         [&] { return count > 0; });
    require(ok && count == 1, "alternate sources must resolve exactly once");
    require(sources.size() >= 2, "a real source query returns a ranked list");
    int current = 0;
    for (const QVariant &v : sources) {
        const QVariantMap m = v.toMap();
        require(!m.value(QStringLiteral("id")).toString().isEmpty(), "each source needs an id");
        require(!m.value(QStringLiteral("url")).toString().isEmpty(), "each source needs a url");
        if (m.value(QStringLiteral("current")).toBool())
            ++current;
    }
    require(current <= 1, "at most one source may be marked current");
}

void unknownMediaResolvesEmptyNotError()
{
    HarnessHostServices host;
    QVariantList sources;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::alternateSourcesResolved, &host,
                     [&](const QString &, const QVariantList &s) { ++count; sources = s; });
    const bool ok = pump([&] { host.requestAlternateSources(QStringLiteral("empty")); },
                         [&] { return count > 0; });
    require(ok && count == 1, "empty media must still resolve exactly once");
    require(sources.isEmpty(), "no results resolves to an empty list, never an error");
}

void skipSegmentsResolveTypedKinds()
{
    HarnessHostServices host;
    QVariantList segs;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::skipSegmentsResolved, &host,
                     [&](const QString &, const QVariantList &s) { ++count; segs = s; });
    const bool ok = pump([&] { host.requestSkipSegments(QStringLiteral("tt-episode")); },
                         [&] { return count > 0; });
    require(ok && count == 1, "skip segments must resolve exactly once");
    require(!segs.isEmpty(), "a known episode returns skip segments");
    for (const QVariant &v : segs) {
        const QVariantMap m = v.toMap();
        const QString kind = m.value(QStringLiteral("kind")).toString();
        require(kind == QStringLiteral("intro") || kind == QStringLiteral("recap") ||
                    kind == QStringLiteral("credits"),
                "segment kind must be intro/recap/credits");
        require(m.value(QStringLiteral("endSeconds")).toDouble() >
                    m.value(QStringLiteral("startSeconds")).toDouble(),
                "a segment must have positive duration");
    }
}

void onlineSubtitlesResolveList()
{
    HarnessHostServices host;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::onlineSubtitlesResolved, &host,
                     [&](const QString &, const QVariantList &) { ++count; });
    const bool ok = pump([&] { host.requestOnlineSubtitles(QStringLiteral("tt-movie")); },
                         [&] { return count > 0; });
    require(ok && count == 1, "online subtitles must resolve exactly once");
}

void metadataErrorSurfacesAsError()
{
    HarnessHostServices host;
    QVariantMap meta;
    int count = 0;
    QObject::connect(&host, &Player2HostServices::metadataResolved, &host,
                     [&](const QString &, const QVariantMap &m) { ++count; meta = m; });
    const bool ok = pump([&] { host.requestMetadata(QStringLiteral("boom")); },
                         [&] { return count > 0; });
    require(ok && count == 1, "metadata must resolve exactly once even on error");
    require(!meta.value(QStringLiteral("error")).toString().isEmpty(),
            "a failed hydration surfaces a typed error, never a silent empty");
}

void downloadEmitsMonotonicStateStreamToReady()
{
    HarnessHostServices host;
    QStringList states;
    QObject::connect(&host, &Player2HostServices::downloadStateChanged, &host,
                     [&](const QString &, const QVariantMap &s) {
                         states << s.value(QStringLiteral("state")).toString();
                     });
    const bool ok = pump([&] { host.requestDownload(QStringLiteral("tt-movie"), QStringLiteral("ok")); },
                         [&] { return !states.isEmpty() && states.last() == QStringLiteral("ready"); },
                         3'000);
    require(ok, "a good download must stream through to ready");
    require(states.first() == QStringLiteral("queued"), "download stream starts queued");
    require(states.contains(QStringLiteral("active")), "download stream passes through active");
    require(states.last() == QStringLiteral("ready"), "download stream ends ready");
}

void downloadBadSourceFailsWithError()
{
    HarnessHostServices host;
    QVariantMap last;
    QObject::connect(&host, &Player2HostServices::downloadStateChanged, &host,
                     [&](const QString &, const QVariantMap &s) { last = s; });
    const bool ok = pump([&] { host.requestDownload(QStringLiteral("tt-movie"), QStringLiteral("no")); },
                         [&] { return last.value(QStringLiteral("state")).toString() ==
                                      QStringLiteral("failed"); },
                         3'000);
    require(ok, "a bad source must reach a failed state");
    require(!last.value(QStringLiteral("error")).toString().isEmpty(),
            "a failed download carries a typed error");
}

void reportProgressIsFireAndForget()
{
    HarnessHostServices host;
    // No signal, no crash: progress persistence is the host's business.
    host.reportProgress(QStringLiteral("tt-movie"), 42.0, 1800.0);
    require(true, "reportProgress must not throw");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    try {
        adjacentEpisodeResolvesOnceWithData();
        adjacentEpisodeBoundaryResolvesDeadNotError();
        alternateSourcesResolveRankedListWithCurrent();
        unknownMediaResolvesEmptyNotError();
        skipSegmentsResolveTypedKinds();
        onlineSubtitlesResolveList();
        metadataErrorSurfacesAsError();
        downloadEmitsMonotonicStateStreamToReady();
        downloadBadSourceFailsWithError();
        reportProgressIsFireAndForget();
    } catch (const std::exception &error) {
        std::cerr << "player2_host_services_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_host_services_test: PASS\n";
    return EXIT_SUCCESS;
}
