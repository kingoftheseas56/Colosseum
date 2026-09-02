// tst_extensions_first_run — Living Guide Task 11 (product prerequisite). Proves the fresh-profile
// acquisition-source consent contract, the Stremio model the locked product rule requires:
//   - core catalogues are seeded ENABLED;
//   - every removable acquisition/playback WELL — a non-core extension that provides `stream` sources —
//     is seeded INSTALLED BUT DISABLED until the user turns it on;
//   - non-fetching capabilities (catalog/meta/subtitles/universe) are never disabled by the gate.
// The decision is derived from manifest resources + `core`, never by string-matching a name. GUILESS;
// isolated AppData via QStandardPaths test mode + a wiped index per test.

#include "engine/ExtensionsStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

class tst_extensions_first_run : public QObject
{
    Q_OBJECT

    static void wipeIndex()
    {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(base + QStringLiteral("/extensions/installed.json"));
    }
    static bool providesStream(const QVariantMap& e)
    {
        return e.value(QStringLiteral("manifest")).toMap()
                .value(QStringLiteral("resources")).toStringList()
                .contains(QStringLiteral("stream"));
    }
    static bool isRemovableWell(const QVariantMap& e)
    {
        return !e.value(QStringLiteral("core")).toBool() && providesStream(e);
    }
    static QVariantMap findById(const QVariantList& items, const QString& id)
    {
        for (const QVariant& v : items)
            if (v.toMap().value(QStringLiteral("id")).toString() == id)
                return v.toMap();
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumFirstRunTest"));
        QCoreApplication::setApplicationName(QStringLiteral("colosseum_first_run_test"));
        QStandardPaths::setTestModeEnabled(true);
    }
    void init() { wipeIndex(); }
    void cleanup() { wipeIndex(); }

    // A fresh profile seeds core catalogues enabled, but every removable acquisition/playback well disabled.
    void fresh_profile_requires_consent_for_removable_wells()
    {
        ExtensionsStore store(nullptr);
        const QVariantList items = store.installed();
        QVERIFY(!items.isEmpty());

        int wells = 0, coreCatalogues = 0;
        for (const QVariant& v : items) {
            const QVariantMap e = v.toMap();
            const QString id = e.value(QStringLiteral("id")).toString();
            if (e.value(QStringLiteral("core")).toBool()) {
                ++coreCatalogues;
                QVERIFY2(e.value(QStringLiteral("enabled")).toBool(),
                         qPrintable(QStringLiteral("core catalogue seeded disabled: ") + id));
            }
            if (isRemovableWell(e)) {
                ++wells;
                QVERIFY2(!e.value(QStringLiteral("enabled")).toBool(),
                         qPrintable(QStringLiteral("removable acquisition/playback well seeded ENABLED (no consent): ") + id));
            }
        }
        QVERIFY2(coreCatalogues >= 1, "expected at least one core catalogue in the seed");
        QVERIFY2(wells >= 1, "expected at least one removable acquisition/playback well in the seed");
    }

    // Derivation is by resources + core, NOT string-matching: non-core capabilities that do not provide
    // streams (catalog / meta / subtitles / universe) must stay enabled.
    void non_fetching_capabilities_stay_enabled()
    {
        ExtensionsStore store(nullptr);
        for (const QVariant& v : store.installed()) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("core")).toBool()) continue;
            if (providesStream(e)) continue;   // a well — covered above
            const QString id = e.value(QStringLiteral("id")).toString();
            QVERIFY2(e.value(QStringLiteral("enabled")).toBool(),
                     qPrintable(QStringLiteral("non-well capability wrongly disabled by the consent gate: ") + id));
        }
    }

    // Consent is real: enabling a seeded-disabled well is honored.
    void enabling_a_well_is_honored()
    {
        ExtensionsStore store(nullptr);
        QString wellId;
        for (const QVariant& v : store.installed()) {
            const QVariantMap e = v.toMap();
            if (isRemovableWell(e) && !e.value(QStringLiteral("enabled")).toBool()) {
                wellId = e.value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY2(!wellId.isEmpty(), "expected a seeded-disabled removable well");
        store.setEnabled(wellId, true);
        QVERIFY2(findById(store.installed(), wellId).value(QStringLiteral("enabled")).toBool(),
                 "setEnabled(well, true) was not honored");
    }

    // Existing profile (Preflight #6 hardening): a user's explicit enable of a seeded-disabled well
    // SURVIVES store reconstruction — the new fresh-install default never rewrites a persisted choice
    // (no defaults-version bump, and an existing profile is not re-seeded).
    void enabled_choice_survives_store_reconstruction()
    {
        QString wellId;
        {
            ExtensionsStore store(nullptr);            // fresh profile (init() wiped the index)
            for (const QVariant& v : store.installed()) {
                const QVariantMap e = v.toMap();
                if (isRemovableWell(e) && !e.value(QStringLiteral("enabled")).toBool()) {
                    wellId = e.value(QStringLiteral("id")).toString();
                    break;
                }
            }
            QVERIFY(!wellId.isEmpty());
            store.setEnabled(wellId, true);            // persists the choice to installed.json
        }
        ExtensionsStore reopened(nullptr);             // reconstruct WITHOUT wiping — an existing profile
        QVERIFY2(findById(reopened.installed(), wellId).value(QStringLiteral("enabled")).toBool(),
                 "a persisted enable choice was reset on store reconstruction");
    }

    // R1 (release gate for 1.1.1, 2026-08-21, "nyaa ships dark"): the manga Nyaa well
    // rides the SAME generic removable-well gate proven above (fresh_profile_requires_
    // consent_for_removable_wells) -- this case names it explicitly so the release
    // gate has its own direct, traceable assertion rather than relying only on the
    // generic "at least one well" count. Mirrors Torrentio's own treatment exactly:
    // both are non-core, both provide "stream", both seed enabled:false.
    void manga_nyaa_well_seeded_disabled()
    {
        ExtensionsStore store(nullptr);
        const QVariantMap nyaa = findById(store.installed(), QStringLiteral("colosseum.well.nyaa"));
        QVERIFY2(!nyaa.isEmpty(), "colosseum.well.nyaa missing from the house seed");
        QVERIFY2(isRemovableWell(nyaa), "colosseum.well.nyaa is not classified as a removable well");
        QVERIFY2(!nyaa.value(QStringLiteral("enabled")).toBool(),
                 "colosseum.well.nyaa seeded ENABLED on a fresh install -- nyaa must ship dark");
    }

    void legacy_weebcentral_well_migrates_in_place_to_tankoyomi()
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/extensions/installed.json");
        int originalIndex = -1;
        {
            ExtensionsStore seeded(nullptr);
            const QVariantList items = seeded.installed();
            for (int i = 0; i < items.size(); ++i) {
                if (items.at(i).toMap().value(QStringLiteral("id")).toString()
                    == QStringLiteral("colosseum.well.tankoyomi")) {
                    originalIndex = i;
                    break;
                }
            }
        }
        QVERIFY(originalIndex >= 0);

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        QJsonArray rows = root.value(QStringLiteral("extensions")).toArray();
        QVERIFY(originalIndex < rows.size());
        QJsonObject legacy = rows.at(originalIndex).toObject();
        legacy.insert(QStringLiteral("id"), QStringLiteral("colosseum.well.weebcentral.pages"));
        legacy.insert(QStringLiteral("transportUrl"), QStringLiteral("colosseum://well/weebcentral.pages"));
        legacy.insert(QStringLiteral("enabled"), true);
        QJsonObject legacyManifest = legacy.value(QStringLiteral("manifest")).toObject();
        legacyManifest.insert(QStringLiteral("id"), QStringLiteral("colosseum.well.weebcentral.pages"));
        legacyManifest.insert(QStringLiteral("name"), QStringLiteral("WeebCentral"));
        legacy.insert(QStringLiteral("manifest"), legacyManifest);
        rows.replace(originalIndex, legacy);
        root.insert(QStringLiteral("defaultsVersion"), 11);
        root.insert(QStringLiteral("extensions"), rows);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();

        ExtensionsStore migrated(nullptr);
        const QVariantList after = migrated.installed();
        QCOMPARE(after.at(originalIndex).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("colosseum.well.tankoyomi"));

        QVERIFY(after.at(originalIndex).toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(findById(after, QStringLiteral("colosseum.well.weebcentral.pages")).isEmpty());
        const QVariantMap tankoyomi = findById(after, QStringLiteral("colosseum.well.tankoyomi"));
        QVERIFY(!tankoyomi.isEmpty());
        QCOMPARE(tankoyomi.value(QStringLiteral("transportUrl")).toString(),
                 QStringLiteral("colosseum://well/tankoyomi"));
        QCOMPARE(tankoyomi.value(QStringLiteral("manifest")).toMap()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("Tankoyomi"));
    }

    void configured_manifest_urls_keep_query_before_manifest_suffix()
    {
        ExtensionsStore store(nullptr);
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state?token=fixture")),
                 QStringLiteral("https://example.test/user-state/manifest.json?token=fixture"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state/manifest.json?token=fixture")),
                 QStringLiteral("https://example.test/user-state/manifest.json?token=fixture"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("https://example.test/user-state#fragment")),
                 QStringLiteral("https://example.test/user-state/manifest.json#fragment"));
        QCOMPARE(store.normalizeUrl(QStringLiteral("colosseum://house/source")),
                 QStringLiteral("colosseum://house/source"));
    }
};

QTEST_GUILESS_MAIN(tst_extensions_first_run)
#include "tst_extensions_first_run.moc"
