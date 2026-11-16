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
#include <QFile>
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
    void startup_viewport_geometry_data();
    void startup_viewport_geometry();
    void startup_viewport_override_is_memory_only();
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

void tst_window_state_policy::startup_viewport_geometry_data()
{
    QTest::addColumn<QString>("request");
    QTest::addColumn<QRect>("screen");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<QRect>("expected");
    const QRect fullScreen(0, 0, 1280, 720);
    QTest::newRow("unset_is_no_override") << QString() << QRect() << true << QRect();
    // The work area may be only 688 high; an explicit fixture can use the full
    // screen without changing the production work-area policy.
    QTest::newRow("exact_1280x700_inside_full_screen")
        << QStringLiteral("1280x700") << fullScreen << true << QRect(0, 10, 1280, 700);
    QTest::newRow("center_on_secondary_screen")
        << QStringLiteral("1280x700") << QRect(-1920, 100, 1920, 1080)
        << true << QRect(-1600, 290, 1280, 700);
    QTest::newRow("minimum_size")
        << QStringLiteral("1024x640") << fullScreen << true << QRect(128, 40, 1024, 640);
    QTest::newRow("maximum_bounded_size")
        << QStringLiteral("16384x16384") << QRect(0, 0, 16384, 16384)
        << true << QRect(0, 0, 16384, 16384);
    QTest::newRow("screen_too_short")
        << QStringLiteral("1280x700") << QRect(0, 0, 1280, 688) << false << QRect();
    QTest::newRow("screen_too_narrow")
        << QStringLiteral("1280x700") << QRect(0, 0, 1200, 800) << false << QRect();
    QTest::newRow("missing_screen")
        << QStringLiteral("1280x700") << QRect() << false << QRect();
    for (const QString &invalid : {QStringLiteral(" 1280x700"), QStringLiteral("1280x700 "),
             QStringLiteral("1280x700\n"), QStringLiteral("1280X700"),
             QStringLiteral("01280x700"), QStringLiteral("1280.0x700"),
             QStringLiteral("1280x7e2"), QStringLiteral("NaNx700"),
             QStringLiteral("infx700"), QStringLiteral("-1280x700"),
             QStringLiteral("1023x700"), QStringLiteral("1280x639"),
             QStringLiteral("0x0"), QStringLiteral("16385x700"),
             QStringLiteral("1280x16385"), QStringLiteral("999999999999x700")}) {
        QTest::newRow(qPrintable(QStringLiteral("invalid_") + invalid))
            << invalid << fullScreen << false << QRect();
    }
}

void tst_window_state_policy::startup_viewport_geometry()
{
    QFETCH(QString, request);
    QFETCH(QRect, screen);
    QFETCH(bool, accepted);
    QFETCH(QRect, expected);
    const QRect sentinel(1, 2, 3, 4);
    QRect actual = sentinel;
    QCOMPARE(WindowModeStore::startupViewportGeometry(request, screen, &actual), accepted);
    QCOMPARE(actual, accepted ? expected : sentinel);
}

void tst_window_state_policy::startup_viewport_override_is_memory_only()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowViewportQtTest"));
    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("window/baseMode"), QStringLiteral("fullscreen"));
    settings.setValue(QStringLiteral("window/normalGeometry"), QRect(100, 120, 1100, 680));
    settings.setValue(QStringLiteral("window/maximized"), true);
    settings.setValue(QStringLiteral("window/fillPolicy"), 1);
    settings.sync();
    QFile ini(settings.fileName());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QByteArray original = ini.readAll();
    ini.close();

    WindowModeStore normal;
    QVERIFY(!normal.shellWindowed());
    QVERIFY(normal.savedMaximized());
    const QRect saved = normal.savedNormalGeometry();
    QVERIFY(normal.configureStartupViewport(QString(), QRect()));
    QVERIFY(!normal.shellWindowed());
    QCOMPARE(normal.savedNormalGeometry(), saved);
    QVERIFY(normal.savedMaximized());
    QVERIFY(!normal.configureStartupViewport(QStringLiteral("1280x700 "), QRect(0, 0, 1280, 720)));
    QVERIFY(!normal.configureStartupViewport(QStringLiteral("1280x700"), QRect(0, 0, 1280, 688)));
    QVERIFY(!normal.shellWindowed());
    QCOMPARE(normal.savedNormalGeometry(), saved);
    QVERIFY(normal.savedMaximized());

    WindowModeStore fixture;
    QSignalSpy changed(&fixture, &WindowModeStore::changed);
    QVERIFY(fixture.configureStartupViewport(QStringLiteral("1280x700"), QRect(0, 0, 1280, 720)));
    QVERIFY(fixture.shellWindowed());
    QVERIFY(!fixture.savedMaximized());
    QCOMPARE(fixture.savedNormalGeometry(), QRect(0, 10, 1280, 700));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!fixture.configureStartupViewport(QStringLiteral("NaNx700"), QRect(0, 0, 1280, 720)));
    QCOMPARE(fixture.savedNormalGeometry(), QRect(0, 10, 1280, 700));
    QVERIFY(!fixture.savedMaximized());
    QCOMPARE(changed.count(), 1);

    // A second ordinary store still sees the original fullscreen/fill policy,
    // including the old-policy maximize default. Configuration never writes INI.
    WindowModeStore unchanged;
    QVERIFY(!unchanged.shellWindowed());
    QCOMPARE(unchanged.savedNormalGeometry(), saved);
    QVERIFY(unchanged.savedMaximized());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    QCOMPARE(ini.readAll(), original);
    QCOMPARE(settings.value(QStringLiteral("window/fillPolicy")).toInt(), 1);
}

QTEST_GUILESS_MAIN(tst_window_state_policy)
#include "tst_window_state_policy.moc"
