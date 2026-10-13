#include <QtQuickTest/quicktest.h>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>

class VinylQmlSetup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName("ColosseumTests");
        QCoreApplication::setApplicationName("VinylRegression");
        static QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(vinyl_regression,VinylQmlSetup)
#include "quicktest.moc"
