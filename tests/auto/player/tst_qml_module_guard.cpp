#include "bootstrap/QmlModuleGuards.h"

#include <QFile>
#include <QDir>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

class tst_qml_module_guard : public QObject
{
    Q_OBJECT
private:
    void checkImport(bool guarded)
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString module = fixture.filePath("QtQuick");
        QVERIFY(QDir().mkpath(module));
        QFile qmldir(module + "/qmldir");
        QVERIFY(qmldir.open(QIODevice::WriteOnly));
        const QByteArray pluginPath = (QLibraryInfo::path(QLibraryInfo::QmlImportsPath)
                                      + "/QtQuick").toUtf8();
        // A different plugin identity forces a second resolution of the same URI.
        // Keep no fallback qmldir: the failed registration must remain observable.
        qmldir.write("module QtQuick\noptional plugin qtquick2plugin " + pluginPath
                     + "\noptional plugin qtquick2plugin_other " + pluginPath
                     + "\nclassname QtQuick2Plugin\nimport QtQml auto\n");
        qmldir.close();

        QQmlEngine engine;
        QQmlComponent shell(&engine);
        shell.setData("import QtQuick\nimport QtQuick.Window\nItem {}",
                      QUrl::fromLocalFile(fixture.filePath("Shell.qml")));
        QScopedPointer<QObject> root(shell.create());
        QVERIFY2(root, qPrintable(shell.errorString()));

        QByteArray playerImport("import QtQuick");
        if (guarded) {
            QVERIFY(Colosseum::Bootstrap::sealQtQuickModule());
            QFile page(QStringLiteral(PLAYER_PAGE_SOURCE));
            QVERIFY(page.open(QIODevice::ReadOnly));
            const auto match = QRegularExpression(
                                   QStringLiteral("(?m)^import QtQuick(?:[ \\t]+[0-9.]+)?[ \\t]*\\r?$"))
                                   .match(QString::fromUtf8(page.readAll()));
            QVERIFY(match.hasMatch());
            playerImport = match.captured().toUtf8();
        }
        engine.setImportPathList({fixture.path()});
        QQmlComponent player(&engine);
        player.setData(playerImport
                           + "\nItem { RegularExpressionValidator { regularExpression: /a/ } }",
                       QUrl::fromLocalFile(fixture.filePath("Player.qml")));
        if (!guarded) {
            QVERIFY(player.isError());
            QVERIFY2(player.errorString().contains("Cannot install element 'RegularExpressionValidator'"
                                                   " into protected module 'QtQuick' version '6'"),
                     qPrintable(player.errorString()));
            return;
        }
        QScopedPointer<QObject> item(player.create());
        QVERIFY2(item, qPrintable(player.errorString()));
    }
private slots:
    void unguarded_late_resolution_reproduces_reported_error() { checkImport(false); }
    void guarded_player_import_reuses_registered_types() { checkImport(true); }
};

QTEST_MAIN(tst_qml_module_guard)
#include "tst_qml_module_guard.moc"
