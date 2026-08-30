// tst_store_isolation — proves the 2026-08-14 registry-leak fix for ProgressStore and
// CollectionStore: a COLOSSEUM_APPDATA_TAG-tagged (isolated Lanista test) session must
// persist to a private FILE under its own AppData root, never to the shared Windows
// registry key (HKCU\Software\Brotherhood\Colosseum) the daily app and every real user's
// Continue/Collection history live in.
//
// SAFETY CONTRACT (why this test never touches the real registry, tagged or untagged):
// ProgressStore/CollectionStore's default constructor decides its backing store with a
// simple, auditable branch — `if (progressStoreTaggedIniPath(...).isEmpty()) { registry
// ctor } else { file ctor }` (ProgressStore.h, CollectionStore.h). The two branches are
// mutually exclusive by construction, so this test proves the invariant WITHOUT ever
// constructing a registry-backed QSettings against the real "Brotherhood"/"Colosseum"
// keys:
//   1. Untagged: the gate function itself (progressStoreTaggedIniPath /
//      collectionStoreTaggedIniPath) returns an empty string — the exact same value it
//      always returned before this fix existed (the function is new, but an empty
//      "isolation path" is definitionally "not diverted" and therefore selects the
//      UNCHANGED registry constructor call). This is checked directly against the free
//      functions the constructors actually call, not re-derived.
//   2. Tagged: the gate function returns a non-empty path under
//      QStandardPaths::AppDataLocation, and a REAL ProgressStore/CollectionStore
//      constructed under that tag is proven, end-to-end, to persist to that exact file
//      (read back and parsed) — with QStandardPaths::setTestModeEnabled(true) sandboxing
//      AppDataLocation into Qt's own disposable "qttest" temp root for the whole test
//      process, so even the FILE path never lands anywhere near a real user's disk state.
//
// The registry itself (real or otherwise) is never opened, read, or written by this
// test — there is no code path here that could touch HKCU\Software\Brotherhood\Colosseum.
// The Qt Test suite runtime-validates the routing decision; the separate live-app runtime
// proof (a real tagged Lanista session showing an empty Continue map) is documented in
// the fix's report, not repeated here.

#include "CollectionStore.h"
#include "ProgressStore.h"

// ProgressStore.h keeps its gate helper in a named namespace (main.cpp includes both
// store headers, so the symbols must stay distinct); this test calls it unqualified.
using ProgressStoreDetail::progressStoreTaggedIniPath;

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>

namespace {
// A per-run-unique tag so repeated/parallel test runs never collide with each other's
// leftover isolation directories.
QString uniqueTag()
{
    return QStringLiteral("store-isolation-test-")
         + QUuid::createUuid().toString(QUuid::Id128);
}
}

class tst_store_isolation : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Every test starts from a known-clean env: no tag set. QStandardPaths test mode
        // is process-global and idempotent to re-enable; turning it on here (rather than
        // once in initTestCase) makes each test function independently safe to run alone.
        qunsetenv("COLOSSEUM_APPDATA_TAG");
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        qunsetenv("COLOSSEUM_APPDATA_TAG");
    }

    // ---- (b) untagged: the gate resolves to "use the registry", unchanged ----
    void gate_returns_empty_when_untagged()
    {
        QVERIFY(!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"));
        QVERIFY(progressStoreTaggedIniPath(QStringLiteral("progress-store.ini")).isEmpty());
        QVERIFY(collectionStoreTaggedIniPath(QStringLiteral("collection-store.ini")).isEmpty());
    }

    // ---- (a) tagged: the gate resolves to a private file path under AppDataLocation ----
    void gate_returns_tagged_path_when_set()
    {
        qputenv("COLOSSEUM_APPDATA_TAG", uniqueTag().toUtf8());

        const QString progressPath =
            progressStoreTaggedIniPath(QStringLiteral("progress-store.ini"));
        const QString collectionPath =
            collectionStoreTaggedIniPath(QStringLiteral("collection-store.ini"));

        QVERIFY(!progressPath.isEmpty());
        QVERIFY(!collectionPath.isEmpty());
        QVERIFY(progressPath.endsWith(QStringLiteral("/progress-store.ini")));
        QVERIFY(collectionPath.endsWith(QStringLiteral("/collection-store.ini")));

        // Both stores share the same tagged AppData root (both derive it the same way).
        QCOMPARE(QFileInfo(progressPath).absolutePath(),
                 QFileInfo(collectionPath).absolutePath());

        // The gate's mkpath side effect must have made the directory usable.
        QVERIFY(QDir(QFileInfo(progressPath).absolutePath()).exists());

        // Sandboxed by setTestModeEnabled — nowhere near a real Roaming/Brotherhood tree.
        QVERIFY(progressPath.contains(QStringLiteral("qttest"), Qt::CaseInsensitive));
    }

    // ---- (a) end-to-end: a tagged ProgressStore write lands in the tagged FILE ----
    void tagged_progress_store_writes_isolated_file()
    {
        const QString tag = uniqueTag();
        qputenv("COLOSSEUM_APPDATA_TAG", tag.toUtf8());

        const QString expectedPath =
            progressStoreTaggedIniPath(QStringLiteral("progress-store.ini"));
        QVERIFY(!expectedPath.isEmpty());
        // Clean slate: a prior run must not leave a stale file this assertion could pass
        // against by accident.
        QFile::remove(expectedPath);

        const QString markerId = QStringLiteral("isolation-test-") + tag;
        {
            ProgressStore store;   // default ctor — the exact one main.cpp uses
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), markerId);
            entry.insert(QStringLiteral("kind"), QStringLiteral("isolation-test"));
            entry.insert(QStringLiteral("title"), QStringLiteral("Store Isolation Marker"));
            entry.insert(QStringLiteral("progress"), 0.10);
            store.record(entry);
            store.flush();   // drain the background writer synchronously
        }

        QVERIFY2(QFileInfo::exists(expectedPath),
                 "tagged ProgressStore did not create its isolation file");

        QSettings verify(expectedPath, QSettings::IniFormat);
        const QByteArray blob =
            verify.value(QStringLiteral("continue/entries")).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(blob);
        QVERIFY(doc.isObject());
        bool found = false;
        for (auto it = doc.object().constBegin(); it != doc.object().constEnd(); ++it) {
            if (it.value().toObject().value(QStringLiteral("id")).toString() == markerId)
                found = true;
        }
        QVERIFY2(found, "tagged ProgressStore's write did not land in its own isolation file");

        QFile::remove(expectedPath);
    }

    // ---- (a) end-to-end: a tagged CollectionStore write lands in the tagged FILE ----
    void tagged_collection_store_writes_isolated_file()
    {
        const QString tag = uniqueTag();
        qputenv("COLOSSEUM_APPDATA_TAG", tag.toUtf8());

        const QString expectedPath =
            collectionStoreTaggedIniPath(QStringLiteral("collection-store.ini"));
        QVERIFY(!expectedPath.isEmpty());
        QFile::remove(expectedPath);

        const QString markerId = QStringLiteral("isolation-test-") + tag;
        {
            CollectionStore store;   // default ctor — the exact one main.cpp uses
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), markerId);
            entry.insert(QStringLiteral("type"), QStringLiteral("isolation-test"));
            entry.insert(QStringLiteral("title"), QStringLiteral("Store Isolation Marker"));
            store.add(QStringLiteral("isolation-test-world"), entry);
        }

        QVERIFY2(QFileInfo::exists(expectedPath),
                 "tagged CollectionStore did not create its isolation file");

        QSettings verify(expectedPath, QSettings::IniFormat);
        const QByteArray blob =
            verify.value(QStringLiteral("collection/entries")).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(blob);
        QVERIFY(doc.isObject());
        bool found = false;
        for (auto it = doc.object().constBegin(); it != doc.object().constEnd(); ++it) {
            if (it.value().toObject().value(QStringLiteral("id")).toString() == markerId)
                found = true;
        }
        QVERIFY2(found, "tagged CollectionStore's write did not land in its own isolation file");

        QFile::remove(expectedPath);
    }
};

QTEST_GUILESS_MAIN(tst_store_isolation)
#include "tst_store_isolation.moc"
