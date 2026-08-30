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
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

// TEST-ONLY seam for the real Player2Shell. The production tracker is deliberately not linked
// into the shared QML runner; this type only makes the shell's existing calls loadable and inert.
class TestActivityPlaybackTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *sink READ sink WRITE setSink)
public:
    explicit TestActivityPlaybackTracker(QObject *parent = nullptr) : QObject(parent) {}

    QObject *sink() const { return m_sink; }
    void setSink(QObject *sink) { m_sink = sink; }

    Q_INVOKABLE void begin(const QVariantMap &, const QString &) {}
    Q_INVOKABLE void sample(qint64, qint64, qint64, bool) {}
    Q_INVOKABLE void discontinuity(qint64, qint64, qint64) {}
    Q_INVOKABLE void naturalEof() {}
    Q_INVOKABLE void endSession() {}

private:
    QObject *m_sink = nullptr;
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
};

QUICK_TEST_MAIN_WITH_SETUP(colosseum_qml, ColosseumQmlTestSetup)
#include "quicktest_main.moc"
