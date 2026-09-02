// progress_watched_override_harness.cpp — proves the manual-watched override:
// set/clear/read, series-root grouping, "manual wins over auto", forget clears it.
#include "../native/ProgressStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, label) do { if (!(cond)) { ++fails; std::printf("FAIL: %s\n", label); } } while (0)

static QVariantMap videoEntry(const QString &id, double progress) {
    QVariantMap m;
    m.insert(QStringLiteral("kind"), QStringLiteral("video"));
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("progress"), progress);
    return m;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    ProgressStore store(dir.filePath("progress.ini"));
    int completionCount = 0;
    QString completionKind;
    QString completionId;
    qint64 completionAt = 0;
    QObject::connect(&store, &ProgressStore::completionCrossed,
                     [&completionCount, &completionKind, &completionId, &completionAt](
                         const QString &kind, const QString &id, qint64 at) {
        ++completionCount;
        completionKind = kind;
        completionId = id;
        completionAt = at;
    });

    // default: no override
    CHECK(store.watchedMark(QStringLiteral("tt1:1:4")) == 0, "default mark is 0");

    // set on an episode id → reads back on ANY id of the same series (root-grouped)
    store.setWatchedMark(QStringLiteral("tt1:2:9"), true);
    CHECK(store.watchedMark(QStringLiteral("tt1")) == 1, "mark reads on root");
    CHECK(store.watchedMark(QStringLiteral("tt1:1:1")) == 1, "mark reads on sibling ep");

    // explicit unmark = -1 (manual UNwatched wins over auto)
    store.setWatchedMark(QStringLiteral("tt1"), false);
    CHECK(store.watchedMark(QStringLiteral("tt1:2:9")) == -1, "unmark is -1 not 0");

    // clear → back to 0
    store.clearWatchedMark(QStringLiteral("tt1"));
    CHECK(store.watchedMark(QStringLiteral("tt1")) == 0, "clear returns to 0");

    // forget() drops the mark with the group
    store.setWatchedMark(QStringLiteral("tt2:1:1"), true);
    QVariantMap e; e.insert("kind", "video"); e.insert("id", "tt2:1:1"); e.insert("progress", 0.5);
    store.record(e);
    store.forget(QStringLiteral("video"), QStringLiteral("tt2:1:1"));
    CHECK(store.watchedMark(QStringLiteral("tt2")) == 0, "forget clears the mark");

    // persistence: a second store on the same ini sees the mark
    store.setWatchedMark(QStringLiteral("tt3"), true);
    ProgressStore store2(dir.filePath("progress.ini"));
    CHECK(store2.watchedMark(QStringLiteral("tt3")) == 1, "mark persists");

    // ---- N-06 sync seam: snapshots export only valid persisted watch state ----
    const QString snapshotPath = dir.filePath("sync-snapshot.ini");
    {
        QSettings raw(snapshotPath, QSettings::IniFormat);
        raw.setValue(QStringLiteral("video/watchedMark/tt-root"), 1);
        raw.setValue(QStringLiteral("video/watchedMark/tt-unwatched"), -1);
        raw.setValue(QStringLiteral("video/watchedMark/tt-zero"), 0);
        raw.setValue(QStringLiteral("video/watchedMark/tt-invalid"), 2);
        raw.setValue(QStringLiteral("video/lastSeason/series-a"), 3);
        raw.setValue(QStringLiteral("video/lastSeason/series-zero"), 0);
        raw.setValue(QStringLiteral("video/lastSeason/series-negative"), -2);
        raw.sync();
    }
    ProgressStore snapshotStore(snapshotPath);
    QHash<QString, int> snapshotMarks = snapshotStore.syncWatchedMarks();
    CHECK(snapshotMarks.size() == 2, "sync watched snapshot contains only -1/1 values");
    CHECK(snapshotMarks.value(QStringLiteral("tt-root")) == 1, "sync watched snapshot keeps watched mark");
    CHECK(snapshotMarks.value(QStringLiteral("tt-unwatched")) == -1, "sync watched snapshot keeps explicit unwatched mark");
    CHECK(!snapshotMarks.contains(QStringLiteral("tt-zero")), "sync watched snapshot omits zero");
    CHECK(!snapshotMarks.contains(QStringLiteral("tt-invalid")), "sync watched snapshot omits invalid value");

    QHash<QString, int> snapshotSeasons = snapshotStore.syncLastSeasons();
    CHECK(snapshotSeasons.size() == 1, "sync season snapshot contains only positive values");
    CHECK(snapshotSeasons.value(QStringLiteral("series-a")) == 3, "sync season snapshot keeps positive season");

    snapshotStore.setWatchedMark(QStringLiteral("tt777:2:9"), true);
    snapshotMarks = snapshotStore.syncWatchedMarks();
    CHECK(snapshotMarks.value(QStringLiteral("tt777")) == 1, "sync watched snapshot exports series root identity");
    CHECK(!snapshotMarks.contains(QStringLiteral("tt777:2:9")), "sync watched snapshot never exports episode identity for root mark");

    // ---- N-06 remote apply/remove: persistent, idempotent, and no local sync echo ----
    const QString remotePath = dir.filePath("sync-remote.ini");
    ProgressStore remote(remotePath);
    const QString remoteEpisode = QStringLiteral("tt900:1:1");
    remote.record(videoEntry(remoteEpisode, 0.25));
    remote.flush();

    int changedCount = 0;
    int dirtyCount = 0;
    QObject::connect(&remote, &ProgressStore::changed,
                     [&changedCount]() { ++changedCount; });
    QObject::connect(&remote, &ProgressStore::syncDirty,
                     [&dirtyCount]() { ++dirtyCount; });

    const int initialRevision = remote.revision();
    CHECK(!remote.applySyncedWatchedMark(QString(), 1), "remote watched apply rejects empty id");
    CHECK(!remote.applySyncedWatchedMark(QStringLiteral("tt900"), 0), "remote watched apply rejects zero");
    CHECK(!remote.applySyncedWatchedMark(QStringLiteral("tt900"), 2), "remote watched apply rejects invalid mark");
    CHECK(!remote.applySyncedLastSeason(QString(), 5), "remote season apply rejects empty id");
    CHECK(!remote.applySyncedLastSeason(QStringLiteral("tt900"), 0), "remote season apply rejects zero");
    CHECK(remote.revision() == initialRevision && changedCount == 0 && dirtyCount == 0,
          "invalid remote watch state has no partial mutation");

    CHECK(remote.applySyncedWatchedMark(QStringLiteral("tt900:2:7"), 1), "remote watched apply succeeds");
    CHECK(remote.watchedMark(QStringLiteral("tt900")) == 1, "remote watched apply preserves series-root semantics");
    CHECK(remote.revision() == initialRevision + 1 && changedCount == 1 && dirtyCount == 0,
          "remote watched change bumps exactly once without syncDirty");
    CHECK(remote.applySyncedWatchedMark(QStringLiteral("tt900:9:9"), 1), "remote watched replay succeeds");
    CHECK(remote.revision() == initialRevision + 1 && changedCount == 1 && dirtyCount == 0,
          "identical remote watched replay is idempotent");

    CHECK(remote.applySyncedLastSeason(QStringLiteral("tt900"), 5), "remote season apply succeeds");
    CHECK(remote.lastSeason(QStringLiteral("tt900")) == 5, "remote season apply is visible");
    CHECK(remote.revision() == initialRevision + 2 && changedCount == 2 && dirtyCount == 0,
          "remote season change bumps exactly once without syncDirty");
    CHECK(remote.applySyncedLastSeason(QStringLiteral("tt900"), 5), "remote season replay succeeds");
    CHECK(remote.revision() == initialRevision + 2 && changedCount == 2 && dirtyCount == 0,
          "identical remote season replay is idempotent");

    remote.flush();
    ProgressStore remoteReload(remotePath);
    CHECK(remoteReload.watchedMark(QStringLiteral("tt900:3:1")) == 1, "remote watched apply persists across reload");
    CHECK(remoteReload.lastSeason(QStringLiteral("tt900")) == 5, "remote season apply persists across reload");
    CHECK(!remoteReload.get(QStringLiteral("video"), remoteEpisode).isEmpty(), "remote watch apply leaves Continue record intact");

    CHECK(remote.removeSyncedWatchedMark(QStringLiteral("tt900:4:2")), "remote watched remove succeeds");
    CHECK(remote.watchedMark(QStringLiteral("tt900")) == 0, "remote watched remove clears only mark");
    CHECK(remote.revision() == initialRevision + 3 && changedCount == 3 && dirtyCount == 0,
          "remote watched remove bumps exactly once without syncDirty");
    CHECK(remote.removeSyncedWatchedMark(QStringLiteral("tt900")), "missing remote watched remove succeeds");
    CHECK(remote.revision() == initialRevision + 3 && changedCount == 3 && dirtyCount == 0,
          "missing remote watched remove is idempotent");

    CHECK(remote.removeSyncedLastSeason(QStringLiteral("tt900")), "remote season remove succeeds");
    CHECK(remote.lastSeason(QStringLiteral("tt900")) == -1, "remote season remove clears only season");
    CHECK(remote.revision() == initialRevision + 4 && changedCount == 4 && dirtyCount == 0,
          "remote season remove bumps exactly once without syncDirty");
    CHECK(remote.removeSyncedLastSeason(QStringLiteral("tt900")), "missing remote season remove succeeds");
    CHECK(remote.revision() == initialRevision + 4 && changedCount == 4 && dirtyCount == 0,
          "missing remote season remove is idempotent");
    CHECK(!remote.get(QStringLiteral("video"), remoteEpisode).isEmpty(), "remote watch removals leave Continue record intact");

    remote.flush();
    ProgressStore remoteRemovedReload(remotePath);
    CHECK(remoteRemovedReload.watchedMark(QStringLiteral("tt900")) == 0, "removed watched mark stays absent after reload");
    CHECK(remoteRemovedReload.lastSeason(QStringLiteral("tt900")) == -1, "removed season stays absent after reload");
    CHECK(!remoteRemovedReload.get(QStringLiteral("video"), remoteEpisode).isEmpty(), "Continue survives watch-state removal reload");

    // ---- finishing a VAULT video marks it watched instead of erasing all trace ----
    // A vault id is "vault:<sha1>" (one colon), so it is never a "series episode": crossing
    // 0.90 takes persist()'s retire path, which drops the resume record. Before the fix that
    // was ALL it did — the finished film vanished from Progress entirely, restarted from zero
    // on reopen, and no watched state survived anywhere.
    const QString vaultId = QStringLiteral("vault:1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b");
    store.record(videoEntry(vaultId, 0.42));
    CHECK(!store.get(QStringLiteral("video"), vaultId).isEmpty(), "vault film mid-watch keeps a resume record");
    CHECK(store.watchedMark(vaultId) == 0, "vault film mid-watch is not marked watched");

    store.record(videoEntry(vaultId, 0.95));
    CHECK(store.get(QStringLiteral("video"), vaultId).isEmpty(), "finished vault film drops its resume record");
    CHECK(store.watchedMark(vaultId) == 1, "finished vault film is remembered as watched");

    // A catalogue (non-vault) movie must retire without manufacturing manual watched state,
    // while its automatic completion crosses the History bridge seam exactly once.
    const QString movieId = QStringLiteral("tt7654321");
    store.record(videoEntry(movieId, 0.42));
    store.record(videoEntry(movieId, 0.95));
    CHECK(store.get(QStringLiteral("video"), movieId).isEmpty(), "finished catalogue movie drops its resume record");
    CHECK(store.watchedMark(movieId) == 0, "catalogue retire adds no watched mark");
    CHECK(completionCount == 2, "vault and catalogue completions each cross once");
    CHECK(completionKind == "movie" && completionId == movieId && completionAt > 0,
          "catalogue completion event is a movie event");
    store.record(videoEntry(movieId, 0.95));
    CHECK(completionCount == 2, "catalogue completion does not repeat after retirement");

    // Both survive a reload over the same INI: the mark is a plain settings key, the dropped
    // records need the writer drained first (flush() is the read-your-writes barrier).
    store.flush();
    ProgressStore store3(dir.filePath("progress.ini"));
    CHECK(store3.watchedMark(vaultId) == 1, "vault watched mark persists across reload");
    CHECK(store3.get(QStringLiteral("video"), vaultId).isEmpty(), "finished vault film stays dropped after reload");
    CHECK(store3.watchedMark(movieId) == 0, "catalogue movie still has no mark after reload");

    std::printf(fails ? "FAILS: %d\n" : "progress_watched_override_harness: ALL PASS\n", fails);
    return fails;
}
