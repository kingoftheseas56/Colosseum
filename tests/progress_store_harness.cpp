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

    // Persistence: a fresh store over the same INI reflects the removals.
    ProgressStore reloaded(path);
    const QVariantList persisted = reloaded.recent();
    require(persisted.size() == 1, "removals persist across reload");
    require(!hasIdContaining(persisted, QStringLiteral("tt0388629")),
            "forgotten series stays forgotten after reload");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runSuite();
    std::cout << "ProgressStore forget-group behavioral tests passed.\n";
    return 0;
}
