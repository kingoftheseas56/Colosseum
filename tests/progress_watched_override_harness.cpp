// progress_watched_override_harness.cpp — proves the manual-watched override:
// set/clear/read, series-root grouping, "manual wins over auto", forget clears it.
#include "../native/ProgressStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, label) do { if (!(cond)) { ++fails; std::printf("FAIL: %s\n", label); } } while (0)

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

    std::printf(fails ? "FAILS: %d\n" : "progress_watched_override_harness: ALL PASS\n", fails);
    return fails;
}
