#include <QtTest>

#include "GuiStallProbe.h"

class GuiResponsivenessProbeTest : public QObject {
    Q_OBJECT

private slots:
    void startupMilestoneFormatIsStable()
    {
        QCOMPARE(Colosseum::Diagnostics::startupMilestoneLine(QStringLiteral("first-frame"), 37),
                 QStringLiteral("COLOSSEUM_STARTUP_MILESTONE milestone=first-frame atMs=37"));
    }

    void stallSeverityBucketsAreStable()
    {
        QCOMPARE(Colosseum::Diagnostics::stallSeverity(40, 40), QStringLiteral("warning"));
        QCOMPARE(Colosseum::Diagnostics::stallSeverity(149, 40), QStringLiteral("warning"));
        QCOMPARE(Colosseum::Diagnostics::stallSeverity(150, 40), QStringLiteral("severe"));
        QCOMPARE(Colosseum::Diagnostics::stallSeverity(250, 40), QStringLiteral("critical"));
    }

    void stallContextIsExplicitAndEscaped()
    {
        QCOMPARE(Colosseum::Diagnostics::stallContextFields(QStringLiteral("open details"),
                                                              QStringLiteral("home=surface")),
                 QStringLiteral(" operation=open%20details surface=home%3Dsurface"));
        QCOMPARE(Colosseum::Diagnostics::stallContextFields({}, {}), QString());
    }
};

QTEST_GUILESS_MAIN(GuiResponsivenessProbeTest)
#include "tst_gui_responsiveness_probe.moc"
