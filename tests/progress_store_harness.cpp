// ProgressStore.forget() must clear the WHOLE Continue group, not one episode.
//
// The Continue row shows one tile PER SERIES: recent() collapses every episode of a
// show down to a single tile (see continueGroupKey / seriesRootId). The ✕ "remove"
// affordance passes the id of the ONE episode the tile happens to display. Before the
// fix, forget() deleted only that entry, so a sibling episode (an earlier one the user
// had watched) survived and re-surfaced on the next recent() — the show reappeared one
// episode back. This proves the fixed behavior: forgetting any episode of a series drops
// the entire series, while movies and manga (each its own group) are removed one-for-one
// and never over-reach.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe).
#include "ProgressStore.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QVariantMap videoEntry(const QString &id, const QString &title, double progress)
{
    QVariantMap m;
    m.insert(QStringLiteral("kind"), QStringLiteral("video"));
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("progress"), progress);
    return m;
}

bool hasIdContaining(const QVariantList &rows, const QString &needle)
{
    for (const QVariant &r : rows)
        if (r.toMap().value(QStringLiteral("id")).toString().contains(needle))
            return true;
    return false;
}

int countKind(const QVariantList &rows, const QString &kind)
{
    int n = 0;
    for (const QVariant &r : rows)
        if (r.toMap().value(QStringLiteral("kind")).toString() == kind)
            ++n;
    return n;
}

void runSuite()
{
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary QSettings directory exists");
    const QString path = tmp.filePath(QStringLiteral("progress.ini"));

    ProgressStore store(path);

    // One Piece: three episodes of the SAME series (tt-prefixed id → series root tt0388629).
    // The earlier two are finished (marked watched at >= 0.90); the newest is mid-watch and
    // is what the Continue tile displays.
    store.record(videoEntry(QStringLiteral("tt0388629:13:585"), QStringLiteral("One Piece 585"), 0.95));
    store.record(videoEntry(QStringLiteral("tt0388629:13:586"), QStringLiteral("One Piece 586"), 0.95));
    store.record(videoEntry(QStringLiteral("tt0388629:13:587"), QStringLiteral("One Piece 587"), 0.42));
    // A movie and a manga — different groups that must survive a series removal.
    store.record(videoEntry(QStringLiteral("tt1234567"), QStringLiteral("Some Movie"), 0.30));
    {
        QVariantMap m;
        m.insert(QStringLiteral("kind"), QStringLiteral("manga"));
        m.insert(QStringLiteral("id"), QStringLiteral("mangadex:one-piece"));
        m.insert(QStringLiteral("title"), QStringLiteral("One Piece (manga)"));
        m.insert(QStringLiteral("progress"), 0.5);
        store.record(m);
    }

    const QVariantList before = store.recent();
    require(before.size() == 3, "three Continue tiles before removal (series + movie + manga)");
    require(hasIdContaining(before, QStringLiteral("tt0388629")), "series tile present before removal");

    // Remove the show from Continue by forgetting the episode the tile displays (the newest).
    store.forget(QStringLiteral("video"), QStringLiteral("tt0388629:13:587"));

    const QVariantList after = store.recent();
    // The defect: an earlier episode (585/586) used to resurface here.
    require(!hasIdContaining(after, QStringLiteral("tt0388629")),
            "series is GONE after removal — no earlier episode resurfaces");
    require(after.size() == 2, "movie and manga survive the series removal");
    require(hasIdContaining(after, QStringLiteral("tt1234567")), "movie untouched by series removal");
    require(countKind(after, QStringLiteral("manga")) == 1, "manga untouched by series removal");

    // Movie removal still drops exactly the movie (a single-entry group is unchanged).
    store.forget(QStringLiteral("video"), QStringLiteral("tt1234567"));
    const QVariantList afterMovie = store.recent();
    require(!hasIdContaining(afterMovie, QStringLiteral("tt1234567")), "movie removed one-for-one");
    require(afterMovie.size() == 1, "only manga remains");

    // Persistence: a fresh store over the same INI reflects the removals. The background writer
    // is asynchronous (the worker thread serializes + syncs on its own thread, off the GUI/render
    // thread — that is the whole point of the 2026-07-29 video-stutter fix), so flush() is the
    // explicit read-your-writes barrier: it posts the current map and blocks until every queued
    // writeSnapshot has run. Without it, a reload could race an in-flight forget and read stale
    // data. This is the deterministic contract the async conversion requires.
    store.flush();
    ProgressStore reloaded(path);
    const QVariantList persisted = reloaded.recent();
    require(persisted.size() == 1, "removals persist across reload");
    require(!hasIdContaining(persisted, QStringLiteral("tt0388629")),
            "forgotten series stays forgotten after reload");
}

// No event loop is pumped between acceptance and retirement. The receipt must
// settle before the outgoing owner dies, and a separate process must see the
// new value rather than the pre-existing checkpoint or QSettings cache.
void runPendingRetirementSuite()
{
    QTemporaryDir tmp;
    require(tmp.isValid(), "pending receipt temporary directory exists");
    const QString path = tmp.filePath(QStringLiteral("pending.ini"));
    const QString id = QStringLiteral("final-pending-checkpoint");
    int calls = 0;
    bool succeeded = false;
    {
        ProgressStore store(path);
        store.recordSilent(videoEntry(id, QStringLiteral("Before"), 0.125));
        store.flush();
        store.recordSilent(videoEntry(id, QStringLiteral("Final"), 0.625));
        require(store.requestDurableReceipt([&](bool committed, const QString &error) {
            ++calls;
            succeeded = committed && error.isEmpty();
        }), "final receipt accepted");
        require(calls == 0, "receipt is pending immediately before retirement");
    }
    require(calls == 1 && succeeded, "retirement completes pending receipt exactly once");
    QProcess probe;
    probe.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--cold-check"), path, QStringLiteral("0.625")});
    require(probe.waitForFinished(10000) && probe.exitStatus() == QProcess::NormalExit
            && probe.exitCode() == 0, "fresh process reads exact final value");

    // A newer local mutation while the receipt is pending forces its rebase.
    calls = 0;
    {
        ProgressStore store(path);
        store.recordSilent(videoEntry(id, QStringLiteral("Intermediate"), 0.5));
        require(store.requestDurableReceipt([&](bool committed, const QString &) {
            ++calls;
            require(committed, "rebased receipt succeeds");
            store.flush(); // Reentrant flush must not deadlock.
            store.recordSilent(videoEntry(id, QStringLiteral("Callback final"), 0.875));
        }), "rebased receipt accepted");
        store.recordSilent(videoEntry(id, QStringLiteral("Newer"), 0.75));
        bool unrelatedCalled = false;
        QMetaObject::invokeMethod(&store, [&] { unrelatedCalled = true; }, Qt::QueuedConnection);
        store.flush();
        require(!unrelatedCalled, "flush does not dispatch unrelated store MetaCalls");
        require(calls == 1, "flush settles receipt rebase and callback exactly once");
    }
    probe.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--cold-check"), path, QStringLiteral("0.875")});
    require(probe.waitForFinished(10000) && probe.exitStatus() == QProcess::NormalExit
            && probe.exitCode() == 0, "callback final mutation is durable after retirement");

    // A disk failure must complete the pending receipt as failure, never turn
    // an accepted request or the old checkpoint into a success witness.
    const QString blockedParent = tmp.filePath(QStringLiteral("blocked"));
    require(QDir().mkpath(blockedParent), "create writable parent first");
    calls = 0;
    succeeded = true;
    int failedRemoteCalls = 0;
    {
        ProgressStore store(blockedParent + QStringLiteral("/progress.ini"));
        require(QDir().rmdir(blockedParent), "remove empty backing parent");
        QFile blocker(blockedParent);
        require(blocker.open(QIODevice::WriteOnly), "replace parent directory with file");
        blocker.close();
        store.recordSilent(videoEntry(id, QStringLiteral("Unwritable"), 0.625));
        require(store.requestDurableReceipt([&](bool committed, const QString &error) {
            ++calls;
            succeeded = committed;
            require(!error.isEmpty(), "failed receipt explains persistence error");
        }), "failing write was accepted before result");
        require(store.applySyncedEntryAsync(videoEntry(QStringLiteral("failed-remote"), QStringLiteral("Remote"), 0.5),
            [&](bool committed, const QString &error) {
                require(!committed && !error.isEmpty(), "remote writer failure is preserved");
                ++failedRemoteCalls;
            }), "failing remote write accepted before result");
        require(calls == 0 && failedRemoteCalls == 0, "failing receipts initially pending");
    }
    require(calls == 1 && !succeeded && failedRemoteCalls == 1,
            "retirement reports local and remote writer failures exactly once");

}

void runRemoteFlushSuite()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("remote.ini"));
    int remoteCalls = 0, localCalls = 0;
    {
        ProgressStore store(path);
        store.recordSilent(videoEntry(QStringLiteral("local"), QStringLiteral("Old"), 0.125));
        store.flush();
        require(store.applySyncedEntryAsync(videoEntry(QStringLiteral("remote-a"), QStringLiteral("A"), 0.25),
            [&](bool committed, const QString &) {
                require(committed, "first remote receipt succeeds");
                ++remoteCalls;
                store.flush();
                store.recordSilent(videoEntry(QStringLiteral("callback"), QStringLiteral("Callback"), 0.625));
            }), "first remote write accepted");
        require(store.applySyncedEntryAsync(videoEntry(QStringLiteral("remote-b"), QStringLiteral("B"), 0.5),
            [&](bool committed, const QString &) {
                require(committed, "second remote receipt succeeds");
                ++remoteCalls;
            }), "second remote write accepted");
        require(store.requestDurableReceipt([&](bool committed, const QString &) {
            require(committed, "interleaved local receipt succeeds");
            ++localCalls;
        }), "interleaved local receipt accepted");
        store.recordSilent(videoEntry(QStringLiteral("local"), QStringLiteral("Newest"), 0.75));
        require(remoteCalls == 0 && localCalls == 0, "all receipts pending before flush");
        store.flush();
        require(remoteCalls == 2 && localCalls == 1, "multiple rebased receipts complete once");
    }
    ProgressStore cold(path);
    for (const auto &expected : {qMakePair(QStringLiteral("local"), 0.75),
                                qMakePair(QStringLiteral("remote-a"), 0.25),
                                qMakePair(QStringLiteral("remote-b"), 0.5),
                                qMakePair(QStringLiteral("callback"), 0.625)}) {
        require(cold.get(QStringLiteral("video"), expected.first)
                    .value(QStringLiteral("progress")).toDouble() == expected.second,
                "remote rebases preserve every final local and remote value");
    }
}

void runShutdownChild(QCoreApplication &app, const QString &path)
{
    int calls = 0;
    {
        ProgressStore store(path);
        store.recordSilent(videoEntry(QStringLiteral("final-pending-checkpoint"), QStringLiteral("Before"), 0.125));
        store.flush();
        // Registered after the owner's aboutToQuit flush, like the guarded
        // native fixture. Destructor must settle this last pending receipt.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &store, [&] {
            store.recordSilent(videoEntry(QStringLiteral("final-pending-checkpoint"), QStringLiteral("Shutdown final"), 0.625));
            require(store.requestDurableReceipt([&](bool committed, const QString &error) {
                require(committed && error.isEmpty(), "shutdown receipt committed");
                ++calls;
            }), "shutdown receipt accepted");
            require(calls == 0, "last write pending at shutdown boundary");
        });
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        require(app.exec() == 0, "normal event loop shutdown");
    }
    require(calls == 1, "shutdown destructor completes final receipt once");
}

void runNormalShutdownSuite()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("shutdown.ini"));
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--shutdown-write"), path});
    require(child.waitForFinished(10000) && child.exitStatus() == QProcess::NormalExit
            && child.exitCode() == 0, "normal shutdown child verifies pending and completed receipt");
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--cold-check"), path, QStringLiteral("0.625")});
    require(child.waitForFinished(10000) && child.exitStatus() == QProcess::NormalExit
            && child.exitCode() == 0, "fresh process reads exact normal-shutdown final checkpoint");
}

void runChainedRemoteFlushSuite()
{
    for (int mode : {0, 1, 2}) {
        const bool retire = mode == 2;
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("chain.ini"));
        bool chainedAccepted = false, chainedCompleted = false, chainedSucceeded = false;
        int rejectedRetirementRequests = 0;
        auto store = std::make_unique<ProgressStore>(path);
        ProgressStore *owner = store.get();
        require(store->applySyncedEntryAsync(
            videoEntry(QStringLiteral("final-pending-checkpoint"), QStringLiteral("A"), 0.25),
            [&](bool committed, const QString &) {
                require(committed, "entry remote A succeeds");
                chainedAccepted = owner->applySyncedEntryAsync(
                    videoEntry(QStringLiteral("final-pending-checkpoint"), QStringLiteral("B"), 0.75),
                    [&](bool committedB, const QString &) {
                        chainedCompleted = true;
                        chainedSucceeded = committedB;
                    });
                if (retire) {
                    const auto rejected = [&](bool committed, const QString &error) {
                        require(!committed && error.contains(QStringLiteral("retiring")),
                                "new retirement request receives precise explicit failure");
                        ++rejectedRetirementRequests;
                    };
                    require(!owner->requestDurableReceipt(rejected), "retirement refuses new local receipt");
                    require(!owner->removeSyncedEntryAsync(QStringLiteral("video"),
                                QStringLiteral("final-pending-checkpoint"), rejected),
                            "retirement refuses new remote removal");
                    require(!owner->removeTrackerImportedProgressAsync(QStringLiteral("fixture"),
                                QStringLiteral("fixture-account"),
                                [&](bool removed, int, const QString &error) { rejected(removed, error); }),
                            "retirement refuses new tracker removal");
                    require(!owner->applyTrackerImportedEntryAsync({}, {}, {}, {}, false, {}, 0, 0, false, {},
                                [&](ProgressStore::TrackerImportApplyStatus status, const QVariantMap &, const QString &error) {
                                    rejected(status != ProgressStore::TrackerImportApplyStatus::Failed, error);
                                }), "retirement refuses new tracker import before mutation");
                }
            }), "entry remote A accepted");
        // Keep the pointer valid inside the callback during explicit deletion.
        if (retire) { delete store.release(); } else { store->flush(); }
        if (!retire) {
            require(chainedAccepted && !chainedCompleted,
                    "normal flush defers new remote receipt without rejecting it");
            if (mode == 1) {
                delete store.release();
                require(chainedCompleted && chainedSucceeded,
                        "immediate retirement drains previously deferred accepted remote write");
            }
            QElapsedTimer deadline;
            deadline.start();
            while (!chainedCompleted && deadline.elapsed() < 5000) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
            require(chainedSucceeded, "deferred chained remote receipt eventually succeeds");
        }
        require(chainedCompleted, "chained remote request receives explicit result");
        QProcess cold;
        cold.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--cold-check"), path,
             chainedSucceeded ? QStringLiteral("0.75") : QStringLiteral("0.25")});
        require(cold.waitForFinished(10000) && cold.exitStatus() == QProcess::NormalExit
                && cold.exitCode() == 0, "chained remote reported result matches exact fresh-process disk value");
        if (retire)
            require(!chainedAccepted && !chainedSucceeded && rejectedRetirementRequests == 4,
                    "all new async receipt families are explicitly rejected while retiring");
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 4 && app.arguments().at(1) == QStringLiteral("--cold-check")) {
        ProgressStore cold(app.arguments().at(2));
        require(cold.healthy(), "cold progress owner healthy");
        require(cold.get(QStringLiteral("video"), QStringLiteral("final-pending-checkpoint"))
                    .value(QStringLiteral("progress")).toDouble() == app.arguments().at(3).toDouble(),
                "cold exact checkpoint matches");
        return 0;
    }
    if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--shutdown-write")) {
        runShutdownChild(app, app.arguments().at(2));
        return 0;
    }
    runSuite();
    runPendingRetirementSuite();
    runRemoteFlushSuite();
    runNormalShutdownSuite();
    runChainedRemoteFlushSuite();
    std::cout << "ProgressStore forget-group and pending-retirement behavioral tests passed.\n";
    return 0;
}
