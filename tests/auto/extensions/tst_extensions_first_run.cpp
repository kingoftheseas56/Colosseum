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
};

QTEST_GUILESS_MAIN(tst_extensions_first_run)
#include "tst_extensions_first_run.moc"
