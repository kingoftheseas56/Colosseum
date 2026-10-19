// CollectionStore is the "Your Collection" shelf: what the user CHOSE to save via
// + Library. Distinct from ProgressStore (auto history): an entry can exist
// unstarted and survives finishing. This proves: add/has/remove round-trip,
// newest-first items() per world, world isolation, upsert-not-duplicate,
// blank-id/world rejection, and persistence across reload.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe).
#include "CollectionStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

int checks = 0;
void require(bool condition, const char *message)
{
    ++checks;
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
    QVariantMap berserk = entry("Berserk", "manga", "Berserk", 1500);
    QVariantMap berserkPayload;
    berserkPayload.insert(QStringLiteral("tag"), QStringLiteral("berserk-slug"));
    berserkPayload.insert(QStringLiteral("tagId"), 42);
    berserk.insert(QStringLiteral("payload"), berserkPayload);
    store.add(QStringLiteral("tankoban"), berserk);
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

    // The nested payload map (the reopen snapshot) must survive the JSON round-trip.
    const QVariantList tankoAfter = reloaded.items(QStringLiteral("tankoban"));
    require(tankoAfter.size() == 1, "manga entry persisted");
    const QVariantMap reloadedPayload = tankoAfter.at(0).toMap().value(QStringLiteral("payload")).toMap();
    require(reloadedPayload.value(QStringLiteral("tag")).toString() == QStringLiteral("berserk-slug"),
            "nested payload string survives reload");
    require(reloadedPayload.value(QStringLiteral("tagId")).toInt() == 42,
            "nested payload int survives reload");
}

// A catalogue refresh must publish removals and replacements as one owner write.
void runBatchSuite()
{
    QTemporaryDir tmp;
    require(tmp.isValid(), "batch temporary directory exists");
    const QString path = tmp.filePath("batch.ini");
    CollectionStore store(path);
    require(store.add("Vinyl", entry("old", "album", "Old", 1)), "seed batch old item");
    require(store.add("theatre", entry("old", "movie", "Other world", 1)), "seed other world");
    auto replace = [&](const QStringList &removed, const QVariantList &added) {
        bool result = false;
        const bool called = QMetaObject::invokeMethod(&store, "replaceItems", Qt::DirectConnection,
            Q_RETURN_ARG(bool, result), Q_ARG(QString, QStringLiteral("Vinyl")),
            Q_ARG(QStringList, removed), Q_ARG(QVariantList, added));
        require(called, "atomic Collection replacement API exists");
        return result;
    };
    const auto before = store.items("Vinyl");
    const int rev = store.revision();
    require(!replace({"old"}, {entry("new", "album", "New", 2), entry("", "album", "Invalid", 3)}),
            "late malformed entry rejects whole batch");
    require(store.items("Vinyl") == before && store.revision() == rev, "invalid batch leaves old records/revision intact");
    require(!replace({}, {entry("x", "album", "X", 2), entry("x", "album", "Duplicate", 3)}),
            "duplicate IDs reject whole batch");
    require(store.items("Vinyl") == before, "duplicate batch preserves old records");
    int dirty = 0;
    QObject::connect(&store, &CollectionStore::syncDirty, [&] { ++dirty; });
    require(replace({"old"}, {entry("new-a", "album", "A", 2), entry("new-b", "album", "B", 3)}),
            "valid batch commits removals and additions");
    require(!store.has("Vinyl", "old") && store.has("Vinyl", "new-a") && store.has("Vinyl", "new-b"),
            "whole replacement visible");
    require(store.has("theatre", "old"), "batch keeps other worlds intact");
    require(store.revision() == rev + 1 && dirty == 1, "batch emits one revision and one sync notification");
    CollectionStore reload(path);
    require(reload.items("Vinyl") == store.items("Vinyl"), "batch persisted completely");
    require(replace({}, {}), "empty batch is no-op success");
    require(store.revision() == rev + 1 && dirty == 1, "empty batch does not publish a revision");

    const QString folder = tmp.filePath("fault");
    require(QDir().mkpath(folder), "create fault fixture directory");
    {
        CollectionStore fault(folder + "/collection.ini");
        require(fault.add("Vinyl", entry("kept", "album", "Keep", 1)), "seed durable fault fixture");
        const auto good = fault.items("Vinyl");
        const int goodRev = fault.revision();
        require(QDir().rename(folder, folder + "-backup"), "move owned fixture directory");
        QFile blocker(folder);
        require(blocker.open(QIODevice::WriteOnly), "block fixture path with regular file");
        blocker.close();
        bool result = true;
        require(QMetaObject::invokeMethod(&fault, "replaceItems", Qt::DirectConnection,
            Q_RETURN_ARG(bool, result), Q_ARG(QString, QStringLiteral("Vinyl")),
            Q_ARG(QStringList, QStringList{"kept"}),
            Q_ARG(QVariantList, QVariantList{entry("lost", "album", "Must not publish", 2)})), "invoke failed-write batch");
        require(!result, "filesystem write failure is reported");
        require(fault.items("Vinyl") == good && fault.revision() == goodRev, "failed write preserves in-memory last good state");
        require(QFile::remove(folder) && QDir().rename(folder + "-backup", folder), "restore owned fixture path");
    }
    CollectionStore recovered(folder + "/collection.ini");
    require(recovered.has("Vinyl", "kept") && !recovered.has("Vinyl", "lost"),
            "destructor cannot replay failed pending batch over last good file");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runSuite();
    runBatchSuite();
    std::cout << "CollectionStore behavioral tests passed: " << checks << " checks.\n";
    return 0;
}
