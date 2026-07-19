// CollectionStore is the "Your Collection" shelf: what the user CHOSE to save via
// + Library. Distinct from ProgressStore (auto history): an entry can exist
// unstarted and survives finishing. This proves: add/has/remove round-trip,
// newest-first items() per world, world isolation, upsert-not-duplicate,
// blank-id/world rejection, and persistence across reload.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe).
#include "CollectionStore.h"

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

QVariantMap entry(const QString &id, const QString &type, const QString &title, qint64 addedAt)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("type"), type);
    m.insert(QStringLiteral("title"), title);
    m.insert(QStringLiteral("cover"), QStringLiteral("file:///c.jpg"));
    m.insert(QStringLiteral("addedAt"), addedAt);   // explicit for deterministic ordering
    return m;
}

void runSuite()
{
    QTemporaryDir tmp;
    require(tmp.isValid(), "temporary QSettings directory exists");
    const QString path = tmp.filePath(QStringLiteral("collection.ini"));

    CollectionStore store(path);
    require(store.items(QStringLiteral("theatre")).isEmpty(), "starts empty");

    const int rev0 = store.revision();
    store.add(QStringLiteral("theatre"), entry("tt0388629", "series", "One Piece", 1000));
    store.add(QStringLiteral("theatre"), entry("tt1234567", "movie", "Some Movie", 2000));
    store.add(QStringLiteral("tankoban"), entry("Berserk", "manga", "Berserk", 1500));
    store.add(QStringLiteral("biblio"), entry("pk:joe-country", "book", "Joe Country", 1600));
    require(store.revision() > rev0, "revision bumps on add");

    require(store.has(QStringLiteral("theatre"), QStringLiteral("tt0388629")), "has() finds a saved id");
    require(!store.has(QStringLiteral("tankoban"), QStringLiteral("tt0388629")), "worlds are isolated");

    QVariantList theatre = store.items(QStringLiteral("theatre"));
    require(theatre.size() == 2, "items() is per-world");
    require(theatre.at(0).toMap().value(QStringLiteral("id")) == QStringLiteral("tt1234567"),
            "items() is newest-first by addedAt");
    require(theatre.at(0).toMap().value(QStringLiteral("type")) == QStringLiteral("movie"),
            "type rides on the entry (universe-tile law)");

    // Upsert: re-adding the same (world,id) replaces, never duplicates.
    store.add(QStringLiteral("theatre"), entry("tt0388629", "series", "One Piece (renamed)", 3000));
    theatre = store.items(QStringLiteral("theatre"));
    require(theatre.size() == 2, "re-add upserts, no duplicate");
    require(theatre.at(0).toMap().value(QStringLiteral("title")) == QStringLiteral("One Piece (renamed)"),
            "upsert replaces the entry and reorders by new addedAt");

    // Rejection: blank world/id are no-ops.
    const int revBefore = store.revision();
    store.add(QString(), entry("x", "movie", "X", 1));
    store.add(QStringLiteral("theatre"), entry(QString(), "movie", "X", 1));
    require(store.revision() == revBefore, "blank world/id rejected without a bump");

    store.remove(QStringLiteral("theatre"), QStringLiteral("tt1234567"));
    require(!store.has(QStringLiteral("theatre"), QStringLiteral("tt1234567")), "remove() drops the entry");
    require(store.items(QStringLiteral("theatre")).size() == 1, "only the removed entry left");
    require(store.has(QStringLiteral("biblio"), QStringLiteral("pk:joe-country")), "other worlds untouched by remove");

    // Persistence: a fresh store over the same INI reflects everything.
    CollectionStore reloaded(path);
    require(reloaded.items(QStringLiteral("theatre")).size() == 1, "entries persist across reload");
    require(reloaded.has(QStringLiteral("tankoban"), QStringLiteral("Berserk")), "manga title-key persists");
    require(!reloaded.has(QStringLiteral("theatre"), QStringLiteral("tt1234567")), "removal persists");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runSuite();
    std::cout << "CollectionStore behavioral tests passed.\n";
    return 0;
}
