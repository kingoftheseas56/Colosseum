// Isolated real-component harness; never reads or writes the user's collection.
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QSettings>
class EmptyCollection : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision CONSTANT)
public:
    int revision() const { return 0; }
    Q_INVOKABLE QVariantList items(const QString&) const { return {}; }
    Q_INVOKABLE void remove(const QString&, const QString&) {}
};
class MangaBrowseSetup : public QObject {
    Q_OBJECT
    EmptyCollection collection;
    QTemporaryDir settings;
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName("ColosseumTests");
        QCoreApplication::setApplicationName("MangaBrowse");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty("Collection", &collection);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(manga_browse, MangaBrowseSetup)
#include "manga_browse_quicktest_main.moc"
