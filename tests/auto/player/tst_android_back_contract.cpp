#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QWindow>
#include <QKeyEvent>
#include <QFile>
#include "platform/PlatformRuntime.h"
using namespace Colosseum::Platform;
class AndroidBackContract : public QObject {
    Q_OBJECT
private slots:
    void realRuntimeSignalReachesCurrentMainImeFirst() {
        Runtime runtime(nullptr,Kind::Android);QWindow window;runtime.attachWindow(&window);
        QVERIFY(runtime.systemBackAvailable());
        QFile main(QStringLiteral(COLOSSEUM_TEST_SOURCE_ROOT "/qml/Main.qml"));QVERIFY(main.open(QIODevice::ReadOnly));
        const auto source=main.readAll();const int start=source.indexOf("    function handleEscape() {");QVERIFY(start>=0);
        const int end=source.indexOf("\n    }",start);QVERIFY(end>start);
        const QByteArray handler=source.mid(start,end-start+6);
        const QString policy=QUrl::fromLocalFile(QStringLiteral(COLOSSEUM_TEST_SOURCE_ROOT "/qml/ShellBackPolicy.js")).toString();
        QByteArray fixture="import QtQml\nimport \""+policy.toUtf8()+"\" as ShellBackPolicy\nQtObject { id:win\n"
            "property var worldStack: ({current:\"Theatre\"})\nproperty bool androidHost:true\nproperty int navigations:0\nproperty int cancellations:0\n"
            "property QtObject shellInputMethod: QtObject { property bool visible:true; function hide(){visible=false} }\n"
            "function cancelCatalogueRoute(){cancellations++}\nfunction shellEscapeState(){return {worldOpen:true}}\n"
            "function closeWorld(){navigations++}\n"+handler+"\n}";
        QQmlEngine engine;QQmlComponent c(&engine);c.setData(fixture,QUrl());
        QScopedPointer<QObject> root(c.create());QVERIFY2(root,qPrintable(c.errorString()));
        connect(&runtime,&Runtime::backRequested,root.data(),[&]{QVERIFY(QMetaObject::invokeMethod(root.data(),"handleEscape"));});
        QSignalSpy spy(&runtime,&Runtime::backRequested);
        runtime.acceptBackGesture(0,0,0);runtime.acceptBackGesture(1,.5,0);runtime.acceptBackGesture(2,0,0);QCOMPARE(spy.count(),0);
        runtime.acceptBackGesture(3,1,0);QCOMPARE(spy.count(),1);QCOMPARE(root->property("navigations").toInt(),0);QCOMPARE(root->property("cancellations").toInt(),0);
        runtime.acceptBackGesture(3,1,0);QCOMPARE(spy.count(),2);QCOMPARE(root->property("navigations").toInt(),1);
        QKeyEvent key(QEvent::KeyPress,Qt::Key_Back,Qt::NoModifier);QCoreApplication::sendEvent(&window,&key);
        QCOMPARE(spy.count(),3);QCOMPARE(root->property("navigations").toInt(),2);
    }
    void desktopHasNoNativeBackCapability() {
        Runtime desktop(nullptr,Kind::WindowsDesktop);QSignalSpy spy(&desktop,&Runtime::backRequested);
        QVERIFY(!desktop.systemBackAvailable());desktop.dispatchSystemBack();desktop.acceptBackGesture(3,1,0);QCOMPARE(spy.count(),0);
    }
    void shellFallbackRequiresMissingNativeCapability() {
        QFile file(QStringLiteral(COLOSSEUM_TEST_SOURCE_ROOT "/qml/PhoneShellHost.qml"));QVERIFY(file.open(QIODevice::ReadOnly));const auto source=file.readAll();
        const int start=source.indexOf("        enabled: host.androidHost && !(typeof PlatformRuntime");QVERIFY(start>=0);
        const int end=source.indexOf("\n        onActivated:",start);QVERIFY(end>start);
        const auto expression=QString::fromUtf8(source.mid(start+17,end-start-17)).trimmed();
        QQmlEngine engine;
        const auto evaluate=[&](bool native) {
            QQmlComponent c(&engine);const auto qml=QString("import QtQml\nQtObject { property var host: ({androidHost:true}); property bool result: %1 }").arg(expression);
            Runtime runtime(nullptr,native?Kind::Android:Kind::WindowsDesktop);
            if(native)engine.rootContext()->setContextProperty("PlatformRuntime",&runtime);
            else engine.rootContext()->setContextProperty("PlatformRuntime",QVariant());
            c.setData(qml.toUtf8(),QUrl());QScopedPointer<QObject> object(c.create());
            if(!object){qWarning()<<c.errors();return false;}return object->property("result").toBool();
        };
        QVERIFY(evaluate(false));QVERIFY(!evaluate(true));
    }
};
QTEST_MAIN(AndroidBackContract)
#include "tst_android_back_contract.moc"
