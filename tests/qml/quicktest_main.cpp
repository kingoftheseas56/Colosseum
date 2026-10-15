// colosseum_qml_tests — the shared Qt Quick Test runner (Qt Test arc, slice 4).
//
// One repo-built runner replaces the Qt-install qmltestrunner.exe the two
// existing tst_*.qml files depended on (a hardcoded external path, broken on
// any Qt bump). It discovers every tests/qml/tst_*.qml via the -input source
// directory the CTest registration supplies, so the tests' file-relative
// production imports ("../../qml/...") keep resolving against the real tree —
// never a copied component (arc non-goal).
//
// WITH_SETUP because production QML carries `Settings` blocks that fail to
// initialize without an application identity (verified live on first run:
// "QML Settings: Failed to initialize QSettings instance. Status code is: 1").
// The identity is a TEST identity, INI-format, rooted in a per-run temporary
// dir — the live registry/AppData identity is never touched (same isolation
// contract as the native harness estate).
//
// These tests create REAL visible windows (physical mouse hit-testing is the
// whole point of tst_comicreader_title_controls). They carry the `qml` CTest
// label, not `unit`, and are not part of an offscreen gate.
#include <QtQuickTest/quicktest.h>
#include <QtQml/qqml.h>

#include <QCoreApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include "account/ProfilePaths.h"
#include "trackers/TrackerConnectionStore.h"
#include "trackers/TrackerDeliveryStore.h"
#include "trackers/TrackerImportStore.h"
#include "trackers/TrackerMappingStore.h"
#include "trackers/TrackerScrobbleStore.h"
#include "trackers/TrackerSyncCenterModel.h"
#include "trackers/TrackerSyncSettingsStore.h"

#include <memory>

// TEST-ONLY seam for the real Player2Shell. The production tracker is deliberately not linked
// into the shared QML runner; this type only makes the shell's existing calls loadable and inert.
class TestActivityPlaybackTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *sink READ sink WRITE setSink)
    Q_PROPERTY(quint64 lifecycleScopeGeneration READ lifecycleScopeGeneration
                   WRITE setLifecycleScopeGeneration)
public:
    explicit TestActivityPlaybackTracker(QObject *parent = nullptr) : QObject(parent) {}

    QObject *sink() const { return m_sink; }
    void setSink(QObject *sink) { m_sink = sink; }
    quint64 lifecycleScopeGeneration() const { return m_lifecycleScopeGeneration; }
    void setLifecycleScopeGeneration(quint64 generation)
    {
        m_lifecycleScopeGeneration = generation;
    }

    Q_INVOKABLE void begin(const QVariantMap &, const QString &) {}
    Q_INVOKABLE void sample(qint64, qint64, qint64, bool) {}
    Q_INVOKABLE void discontinuity(qint64, qint64, qint64) {}
    Q_INVOKABLE void naturalEof() {}
    Q_INVOKABLE void naturalEof(qint64, qint64) {}
    Q_INVOKABLE void endSession() {}
    Q_INVOKABLE void endSession(qint64, qint64) {}
    Q_INVOKABLE void playbackStateChanged(bool, qint64, qint64) {}
    Q_INVOKABLE void endSessionForProfileDeactivation(qint64, qint64) {}
    Q_INVOKABLE bool localCompletionPersisted() const { return false; }

signals:
    void playbackLifecycleChanged(const QVariantMap &event);

private:
    QObject *m_sink = nullptr;
    quint64 m_lifecycleScopeGeneration = 0;
};

class ColosseumQmlTestSetup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        qmlRegisterType<TestActivityPlaybackTracker>("Colosseum.Activity", 1, 0,
                                                     "ActivityPlaybackTracker");
        QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("test.colosseum.brotherhood"));
        QCoreApplication::setApplicationName(QStringLiteral("ColosseumQmlTests"));
        // INI in a per-run temp dir: Settings blocks initialize AND stay
        // disposable. The dir lives as long as the process (static lifetime).
        static QTemporaryDir settingsDir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           settingsDir.path());
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        if (!engine)
            return;
        static QTemporaryDir trackerDataRoot;
        static std::unique_ptr<TrackerConnectionStore> connections;
        static std::unique_ptr<TrackerMappingStore> mappings;
        static std::unique_ptr<TrackerDeliveryStore> delivery;
        static std::unique_ptr<TrackerScrobbleStore> scrobble;
        static std::unique_ptr<TrackerSyncSettingsStore> settings;
        static std::unique_ptr<TrackerImportStore> imports;
        static std::unique_ptr<TrackerSyncCenterModel> model;
        if (!trackerDataRoot.isValid())
            return;
        const auto profile = ProfilePaths::account(
            QStringLiteral("44444444-4444-4444-8444-444444444444"),
            trackerDataRoot.path());
        if (!profile)
            return;
        connections = std::make_unique<TrackerConnectionStore>(*profile);
        mappings = std::make_unique<TrackerMappingStore>(*profile);
        delivery = std::make_unique<TrackerDeliveryStore>(*profile, mappings.get(), connections.get());
        scrobble = std::make_unique<TrackerScrobbleStore>(*profile);
        settings = std::make_unique<TrackerSyncSettingsStore>(*profile);
        imports = std::make_unique<TrackerImportStore>(*profile, mappings.get(), connections.get());
        model = std::make_unique<TrackerSyncCenterModel>(
            connections.get(), imports.get(), delivery.get(), scrobble.get(), settings.get(),
            trackerBuiltInProviderCatalog());
        engine->rootContext()->setContextProperty(QStringLiteral("TrackerSyncCenter"),
                                                   model.get());
    }
};

QUICK_TEST_MAIN_WITH_SETUP(colosseum_qml, ColosseumQmlTestSetup)
#include "quicktest_main.moc"
