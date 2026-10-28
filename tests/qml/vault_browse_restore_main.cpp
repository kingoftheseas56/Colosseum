#include <QtQuickTest/quicktest.h>
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

// Dedicated runner: this projection fixture never replaces VaultLibrary in the
// shared QML suite. The page and its Settings/navigation logic remain production.
class VaultBrowseRestoreSetup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
        QCoreApplication::setApplicationName(QStringLiteral("VaultBrowseRestoreTests"));
        static QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        QQmlComponent component(engine, QUrl::fromLocalFile(
            QStringLiteral(COLOSSEUM_VAULT_BROWSE_FIXTURE)));
        QObject* fixture = component.create();
        if (!fixture) qFatal("Vault projection fixture failed: %s", qPrintable(component.errorString()));
        fixture->setParent(engine);
        engine->rootContext()->setContextProperty(QStringLiteral("VaultLibrary"), fixture);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(vault_browse_restore, VaultBrowseRestoreSetup)
#include "vault_browse_restore_main.moc"
