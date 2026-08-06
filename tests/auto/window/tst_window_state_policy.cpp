// tst_window_state_policy — the Qt Test arc's slice-3 pilot (test ledger,
// migration candidates). Converted from tests/window_state_policy_harness.cpp,
// whose qFatal idiom stopped at the FIRST failure and hid every later case —
// the named evidence benefit of this conversion is that each contract below
// now reports independently, and the geometry cases are data rows selectable
// by tag. The legacy harness stays built and registered until parity review
// retires it (ledger, migration policy).
//
// Scope (unchanged from the harness): WindowStatePolicy's pure geometry
// contracts and WindowModeStore's settings round-trip, under fully isolated
// INI settings in a QTemporaryDir. No window is created; GUILESS main.

#include "player/windowstatepolicy.h"
#include "player/windowmodestore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class tst_window_state_policy : public QObject
{
    Q_OBJECT

private slots:
    void size_contracts();
    void default_geometry_is_centered();
    void validated_normal_geometry_data();
    void validated_normal_geometry();
    void fullscreen_uses_complete_monitor();
    void fullscreen_invalid_screen_uses_fallback();
    void saved_window_mode_round_trips();
};

void tst_window_state_policy::size_contracts()
{
    QCOMPARE(WindowStatePolicy::defaultSize(), QSize(1280, 720));
    QCOMPARE(WindowStatePolicy::minimumSize(), QSize(1024, 640));
}

void tst_window_state_policy::default_geometry_is_centered()
{
    QCOMPARE(WindowStatePolicy::centeredDefault(QRect(0, 0, 1920, 1040)),
             QRect(320, 160, 1280, 720));
}

// The four validation behaviors as data rows: preserved / recentered /
// safe-defaulted / clamped. One wrong row fails alone, by name.
void tst_window_state_policy::validated_normal_geometry_data()
{
    QTest::addColumn<QRect>("saved");
    QTest::addColumn<QList<QRect>>("screens");
    QTest::addColumn<QRect>("expected");

    const QRect primary(0, 0, 1920, 1040);
    const QList<QRect> twoScreens{QRect(-1920, 0, 1920, 1040), primary};

    QTest::newRow("visible_secondary_geometry_is_preserved")
        << QRect(-1800, 80, 1280, 720) << twoScreens
        << QRect(-1800, 80, 1280, 720);
    QTest::newRow("fully_offscreen_geometry_recenters")
        << QRect(5000, 5000, 1280, 720) << twoScreens
        << QRect(320, 160, 1280, 720);
    QTest::newRow("undersized_geometry_uses_safe_default")
        << QRect(80, 80, 800, 500) << twoScreens
        << QRect(320, 160, 1280, 720);
    QTest::newRow("partially_offscreen_geometry_is_clamped")
        << QRect(1500, 700, 1280, 720) << QList<QRect>{primary}
        << QRect(640, 320, 1280, 720);
}

void tst_window_state_policy::validated_normal_geometry()
{
    QFETCH(QRect, saved);
    QFETCH(QList<QRect>, screens);
    QFETCH(QRect, expected);
    const QRect primary(0, 0, 1920, 1040);
    QCOMPARE(WindowStatePolicy::validatedNormalGeometry(saved, screens, primary),
             expected);
}

void tst_window_state_policy::fullscreen_uses_complete_monitor()
{
    // The complete monitor, including reserved (taskbar) edges.
    const QRect fullPrimary(0, 0, 1920, 1080);
    QCOMPARE(WindowStatePolicy::fullscreenGeometry(fullPrimary,
                                                   QRect(0, 0, 1280, 720)),
             fullPrimary);
}

void tst_window_state_policy::fullscreen_invalid_screen_uses_fallback()
{
    QCOMPARE(WindowStatePolicy::fullscreenGeometry(QRect(),
                                                   QRect(10, 20, 1600, 900)),
             QRect(10, 20, 1600, 900));
}

void tst_window_state_policy::saved_window_mode_round_trips()
{
    // Fresh isolated INI settings per run — the live registry/AppData is never
    // touched (same isolation contract as the legacy harness).
    QTemporaryDir settingsDir;
    QVERIFY2(settingsDir.isValid(), "temporary settings directory must exist");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowModeQtTest"));

    {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("window/baseMode"),
                          QStringLiteral("windowed"));
        settings.setValue(QStringLiteral("window/normalGeometry"),
                          QRect(100, 120, 1280, 720));
        settings.setValue(QStringLiteral("window/maximized"), true);
        settings.sync();
    }

    WindowModeStore restored;
    QVERIFY(restored.shellWindowed());
    QCOMPARE(restored.savedNormalGeometry(), QRect(100, 120, 1280, 720));
    QVERIFY(restored.savedMaximized());
}

QTEST_GUILESS_MAIN(tst_window_state_policy)
#include "tst_window_state_policy.moc"
