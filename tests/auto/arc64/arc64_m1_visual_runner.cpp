// Optional component-capture runner. Normal qmltestrunner runs skip image output.
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QDir>
#include <QFileInfo>

class Arc64CaptureSetup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        QString output = qEnvironmentVariable("ARC64_EVIDENCE_DIR");
        if (!output.isEmpty()) {
            output = QFileInfo(output).absoluteFilePath();
            for (const QString &part : {QStringLiteral("P1"), QStringLiteral("P2"), QStringLiteral("P3")}) {
                if (!QDir().mkpath(output + '/' + part))
                    qFatal("Cannot create ARC64_EVIDENCE_DIR output folder");
            }
        }
        engine->rootContext()->setContextProperty(QStringLiteral("arc64EvidenceOutputDirectory"), output);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(arc64_m1_visual, Arc64CaptureSetup)
#include "arc64_m1_visual_runner.moc"
