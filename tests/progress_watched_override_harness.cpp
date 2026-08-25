// progress_watched_override_harness.cpp — proves the manual-watched override:
// set/clear/read, series-root grouping, "manual wins over auto", forget clears it.
#include "../native/ProgressStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
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

    // A catalogue (non-vault) movie must retire EXACTLY as it did before: record gone, and this
    // path adds no watched mark of its own.
    const QString movieId = QStringLiteral("tt7654321");
    store.record(videoEntry(movieId, 0.42));
    store.record(videoEntry(movieId, 0.95));
    CHECK(store.get(QStringLiteral("video"), movieId).isEmpty(), "finished catalogue movie drops its resume record");
    CHECK(store.watchedMark(movieId) == 0, "catalogue retire adds no watched mark");

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
