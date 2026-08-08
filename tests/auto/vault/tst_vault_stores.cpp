// tst_vault_stores — Slice 2. Proves the Vault's two stores:
//   VaultConfig   — user-intent round-trip, atomic-write recovery from the
//                   last-known-good .bak, and path normalization;
//   VaultIdentity — content-addressed id stability + path normalization, the
//                   unique-signature rename re-attachment, and the two-candidate
//                   ambiguity guard that refuses to silently merge.
// Pure QtCore, GUILESS. Config uses a QTemporaryDir for real atomic-IO; identity
// reconciliation is pure over file facts, no disk churn.

#include "engine/VaultConfig.h"
#include "engine/VaultIdentity.h"
#include "engine/VaultRecent.h"
#include "engine/VaultStoreIo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class tst_vault_stores : public QObject
{
    Q_OBJECT

private:
    static void writeRaw(const QString& path, const QByteArray& bytes)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
        f.close();
    }

private slots:
    // ── VaultConfig ──
    void config_round_trips_all_intent();
    void config_recovers_from_backup();
    void config_fresh_when_both_corrupt();
    void config_normalizes_paths();

    // ── VaultIdentity ──
    void id_is_stable_for_same_triple();
    void id_normalizes_path();
    void reconcile_rename_reattaches_progress();
    void reconcile_two_candidate_ambiguity_does_not_migrate();
    void reconcile_persists_across_reload();

    // ── VaultRecent (Slice 9) ──
    void recent_records_dedups_caps_and_reloads();
    void recent_clear_wipes_shortcuts_not_progress();
};

void tst_vault_stores::config_round_trips_all_intent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    {
        VaultConfig c(tmp.path());
        c.addRoot(QStringLiteral("D:/M"), 111);
        c.confirmRoot(QStringLiteral("D:/M"));
        c.setKind(QStringLiteral("D:/M/Berserk"), QStringLiteral("comic"));
        c.setScanIgnore(QStringList{QStringLiteral("sample"), QStringLiteral("extras")});
        c.setHidden(QStringLiteral("vault:abc"), true);
    }
    {
        VaultConfig c(tmp.path());
        QVERIFY(c.hasRoot(QStringLiteral("D:/M")));
        QVERIFY(c.isRootConfirmed(QStringLiteral("D:/M")));
        QCOMPARE(c.kindFor(QStringLiteral("D:/M/Berserk")), QStringLiteral("comic"));
        QCOMPARE(c.scanIgnore(),
                 QStringList({QStringLiteral("sample"), QStringLiteral("extras")}));
        QVERIFY(c.isHidden(QStringLiteral("vault:abc")));
        c.setHidden(QStringLiteral("vault:abc"), false);
        QVERIFY(!c.isHidden(QStringLiteral("vault:abc")));
    }
}

void tst_vault_stores::config_recovers_from_backup()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    {
        VaultConfig c(tmp.path());
        c.addRoot(QStringLiteral("D:/RootA")); // save -> config.json (no .bak yet)
        c.addRoot(QStringLiteral("D:/RootB")); // save -> rotates {A} to .bak; main={A,B}
    }
    // Corrupt the primary; the .bak still holds the good {A}-only snapshot.
    writeRaw(QDir(tmp.path()).filePath(QStringLiteral("config.json")),
             QByteArrayLiteral("{ this is not : valid json"));

    VaultConfig c(tmp.path());
    QVERIFY(c.recoveredFromBackup());
    QVERIFY(c.hasRoot(QStringLiteral("D:/RootA")));
    QVERIFY(!c.hasRoot(QStringLiteral("D:/RootB")));
}

void tst_vault_stores::config_fresh_when_both_corrupt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    writeRaw(QDir(tmp.path()).filePath(QStringLiteral("config.json")),
             QByteArrayLiteral("garbage"));
    writeRaw(QDir(tmp.path()).filePath(QStringLiteral("config.json.bak")),
             QByteArrayLiteral("also garbage"));

    VaultConfig c(tmp.path());
    QVERIFY(!c.recoveredFromBackup());
    QVERIFY(c.roots().isEmpty());
}

void tst_vault_stores::config_normalizes_paths()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultConfig c(tmp.path());
    c.addRoot(QStringLiteral("D:\\Manga"));
    QVERIFY(c.hasRoot(QStringLiteral("D:/Manga"))); // separators normalized
#ifdef Q_OS_WIN
    QVERIFY(c.hasRoot(QStringLiteral("d:/manga"))); // case-insensitive on Windows
#endif
    c.setKind(QStringLiteral("D:\\Manga\\Berserk"), QStringLiteral("comic"));
    QCOMPARE(c.kindFor(QStringLiteral("D:/Manga/Berserk")), QStringLiteral("comic"));
}

void tst_vault_stores::id_is_stable_for_same_triple()
{
    const QString a = VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 100, 5);
    const QString b = VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 100, 5);
    QCOMPARE(a, b);
    QVERIFY(a.startsWith(QStringLiteral("vault:")));
    QCOMPARE(a.length(), 6 + 40); // "vault:" + full SHA-1 hex
    // A different size or mtime is a different identity.
    QVERIFY(a != VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 101, 5));
    QVERIFY(a != VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 100, 6));
}

void tst_vault_stores::id_normalizes_path()
{
    QCOMPARE(VaultIdentity::computeId(QStringLiteral("D:\\A\\x.cbz"), 100, 5),
             VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 100, 5));
#ifdef Q_OS_WIN
    QCOMPARE(VaultIdentity::computeId(QStringLiteral("D:/A/x.cbz"), 100, 5),
             VaultIdentity::computeId(QStringLiteral("d:/a/X.CBZ"), 100, 5));
#endif
}

void tst_vault_stores::reconcile_rename_reattaches_progress()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIdentity id(tmp.path());

    const auto r1 = id.reconcile({ {QStringLiteral("D:/lib/a.cbz"), 100, 5} });
    QCOMPARE(r1.fresh.size(), 1);
    const QString idA = VaultIdentity::computeId(QStringLiteral("D:/lib/a.cbz"), 100, 5);
    QVERIFY(id.knows(idA));

    // Rename a.cbz -> b.cbz (size + mtime preserved) — the unique-signature case.
    const auto r2 = id.reconcile({ {QStringLiteral("D:/lib/b.cbz"), 100, 5} });
    QCOMPARE(r2.migrated.size(), 1);
    const QString cidB = VaultIdentity::computeId(QStringLiteral("D:/lib/b.cbz"), 100, 5);
    QCOMPARE(r2.migrated.first().at(0), idA);  // oldId
    QCOMPARE(r2.migrated.first().at(1), cidB); // newComputedId
    QCOMPARE(id.resolve(cidB), idA);           // progress follows the rename
    QCOMPARE(id.idForFile(QStringLiteral("D:/lib/b.cbz"), 100, 5), idA);
    QCOMPARE(id.pathAliases().size(), 1);       // Reader 2 bridge record
}

void tst_vault_stores::reconcile_two_candidate_ambiguity_does_not_migrate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultIdentity id(tmp.path());

    const QString idA = VaultIdentity::computeId(QStringLiteral("D:/lib/a.cbz"), 100, 5);
    id.reconcile({ {QStringLiteral("D:/lib/a.cbz"), 100, 5} }); // register A

    // A vanishes; TWO new files share A's (size, mtime) — genuinely ambiguous.
    const auto r = id.reconcile({ {QStringLiteral("D:/lib/b.cbz"), 100, 5},
                                  {QStringLiteral("D:/lib/c.cbz"), 100, 5} });
    QCOMPARE(r.migrated.size(), 0);          // never silently merged
    QVERIFY(r.parked.contains(idA));
    QCOMPARE(r.fresh.size(), 2);
    const QString cidB = VaultIdentity::computeId(QStringLiteral("D:/lib/b.cbz"), 100, 5);
    QVERIFY(id.resolve(cidB) != idA);
}

void tst_vault_stores::reconcile_persists_across_reload()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString idA = VaultIdentity::computeId(QStringLiteral("D:/lib/a.cbz"), 100, 5);
    const QString cidB = VaultIdentity::computeId(QStringLiteral("D:/lib/b.cbz"), 100, 5);
    {
        VaultIdentity id(tmp.path());
        id.reconcile({ {QStringLiteral("D:/lib/a.cbz"), 100, 5} });
        id.reconcile({ {QStringLiteral("D:/lib/b.cbz"), 100, 5} }); // migrate
    }
    {
        VaultIdentity id(tmp.path()); // reload from identity.json
        QCOMPARE(id.resolve(cidB), idA); // alias survived the round-trip
        QVERIFY(id.knows(idA));
    }
}

void tst_vault_stores::recent_records_dedups_caps_and_reloads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // a real file for the availability check; a made-up path stays unavailable
    const QString real = QDir(tmp.path()).filePath(QStringLiteral("here.cbz"));
    writeRaw(real, QByteArrayLiteral("x"));
    const QString gone = QStringLiteral("D:/nope/missing.cbz");

    {
        VaultRecent r(tmp.path());
        r.record(gone, QStringLiteral("Missing"), QStringLiteral("comic"), QStringLiteral("vault:1"));
        r.record(real, QStringLiteral("Here"), QStringLiteral("comic"), QStringLiteral("vault:2"));
        QCOMPARE(r.count(), 2);

        const QVariantList items = r.items();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("path")).toString(), real); // most-recent-first
        QVERIFY(items.at(0).toMap().value(QStringLiteral("available")).toBool());     // exists on disk
        QVERIFY(!items.at(1).toMap().value(QStringLiteral("available")).toBool());    // dead path shows state

        // dedup: re-recording the missing one moves it to the front, count stays 2
        r.record(gone, QStringLiteral("Missing"), QStringLiteral("comic"), QStringLiteral("vault:1"));
        QCOMPARE(r.count(), 2);
        QCOMPARE(r.items().at(0).toMap().value(QStringLiteral("path")).toString(), gone);

        // cap at kMax
        for (int i = 0; i < VaultRecent::kMax + 5; ++i)
            r.record(QStringLiteral("D:/f/%1.cbz").arg(i), QStringLiteral("f"),
                     QStringLiteral("comic"), QStringLiteral("vault:f%1").arg(i));
        QCOMPARE(r.count(), VaultRecent::kMax);
    }
    // survives restart (round-trip through open-recent.json)
    VaultRecent r2(tmp.path());
    QCOMPARE(r2.count(), VaultRecent::kMax);
}

void tst_vault_stores::recent_clear_wipes_shortcuts_not_progress()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // a stand-in for the SEPARATE progress store that lives beside the recent store
    const QString progress = QDir(tmp.path()).filePath(QStringLiteral("progress.json"));
    const QByteArray progressBytes = QByteArrayLiteral("{\"vault:2\":{\"page\":7}}");
    writeRaw(progress, progressBytes);

    VaultRecent r(tmp.path());
    r.record(QStringLiteral("D:/a.cbz"), QStringLiteral("A"), QStringLiteral("comic"), QStringLiteral("vault:2"));
    r.record(QStringLiteral("D:/b.mp4"), QStringLiteral("B"), QStringLiteral("video"), QStringLiteral("vault:3"));
    QCOMPARE(r.count(), 2);

    r.clear();
    QCOMPARE(r.count(), 0);          // shortcuts wiped
    QVERIFY(r.items().isEmpty());

    // reading progress is a SEPARATE store — Clear must never touch it
    QVERIFY(QFileInfo::exists(progress));
    QFile pf(progress);
    QVERIFY(pf.open(QIODevice::ReadOnly));
    QCOMPARE(pf.readAll(), progressBytes);
}

QTEST_GUILESS_MAIN(tst_vault_stores)
#include "tst_vault_stores.moc"
