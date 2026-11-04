#include "bootstrap/CollectionLinkActivation.h"
#include "bootstrap/InstanceLock.h"
#include "bootstrap/StartupLayout.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QLocalSocket>
#include <QDesktopServices>
#include <QtTest>
#include <cstdio>
const QUrl legacy(QStringLiteral("https://service.test"));
QString link(QChar c=QLatin1Char('a')) { return "https://getcolosseum.world/c/"+QString(43,c); }
QByteArray read(const QString &path) { QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
bool forwardFixture(const QString &root, const QString &url) {
    QProcess sender;auto env=QProcessEnvironment::systemEnvironment();env.insert("COLLECTION_ACTIVATION_TEST_ROOT",root);sender.setProcessEnvironment(env);
    sender.start(QCoreApplication::applicationFilePath(),{"--collection-url",url});
    return sender.waitForFinished(2500) && sender.exitCode()==0;
}
class Checks : public QObject {
    Q_OBJECT
private slots:
    void rejectsProtocolArgumentInjection() {
        for(const auto &extra:QStringList{"evil.qml","--qml","--update-token","--collection-url","-platform","-platformpluginpath","--version"}){
            QStringList args{"app","--collection-url",link(),extra};const auto before=args;QString error;
            QVERIFY(!takeCollectionLaunchUrl(&args,&error));QVERIFY(!error.isEmpty());QCOMPARE(args,before);
        }
        for(const auto &value:QStringList{"https://getcolosseum.world/c/%61"+QString(42,'a'),link()+"?passphrase=x","colosseum://collection/short"}){
            QStringList args{"app","--collection-url",value};QString error;QVERIFY(!takeCollectionLaunchUrl(&args,&error));QVERIFY(!error.isEmpty());
        }
        QStringList missing{"app","--collection-url"};QString error;QVERIFY(!takeCollectionLaunchUrl(&missing,&error));QVERIFY(!error.isEmpty());
        QStringList normal{"app","dev.qml","-platform","offscreen"};auto before=normal;QVERIFY(!takeCollectionLaunchUrl(&normal,&error));QVERIFY(error.isEmpty());QCOMPARE(normal,before);
        QStringList good{"app","--collection-url",link()};QCOMPARE(takeCollectionLaunchUrl(&good,&error),std::optional<QUrl>(QUrl(link())));QCOMPARE(good,QStringList{"app"});
    }
    void latestBusyProfileDismissalAndShutdown() {
        QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());CollectionLinkActivation activation(lock,legacy);
        QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);
        QVERIFY(activation.submit(QUrl(link('a'))));QVERIFY(activation.submit(QUrl(link('b'))));QCoreApplication::processEvents();QCOMPARE(opened.size(),0);
        activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),1);QCOMPARE(opened[0][0].toUrl(),QUrl(link('b')));
        activation.setDestinationReady(false);activation.setDestinationReady(true);QCoreApplication::processEvents();QCOMPARE(opened.size(),1);
        activation.setDestinationReady(false);QVERIFY(activation.submit(QUrl(link('c'))));QVERIFY(activation.submit(QUrl(link('d'))));activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),2);QCOMPARE(opened[1][0].toUrl(),QUrl(link('d')));
        activation.deferForProfile();activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),3);
        activation.clearPending();activation.deferForProfile();activation.setDestinationReady(true);QCoreApplication::processEvents();QCOMPARE(opened.size(),3);
        QVERIFY(activation.submit(QUrl(link('e'))));activation.shutdown();QCoreApplication::processEvents();QCOMPARE(opened.size(),3);QVERIFY(!activation.submit(QUrl(link())));
    }
    void dismissalTicketsPreservePendingAndNewerPresentations() {
        for(bool identical:{false,true}) {
            QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());CollectionLinkActivation activation(lock,legacy);
            QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);activation.setDestinationReady(true);QVERIFY(activation.submit(QUrl(link('a'))));QTRY_COMPARE(opened.size(),1);
            const auto a=opened[0][1].toString();QVERIFY(!a.isEmpty());activation.setDestinationReady(false);
            QVERIFY(activation.submit(QUrl(link(identical?'a':'b'))));QVERIFY(!activation.dismissPresented(a));activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),2);
            const auto b=opened[1][1].toString();QVERIFY(b!=a);QVERIFY(!activation.dismissPresented(a));
            activation.deferForProfile();QVERIFY(!activation.dismissPresented(b));activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),3);
            const auto c=opened[2][1].toString();QVERIFY(c!=b);QVERIFY(!activation.dismissPresented(b));QVERIFY(activation.dismissPresented(c));
            activation.deferForProfile();activation.setDestinationReady(true);QCoreApplication::processEvents();QCOMPARE(opened.size(),3);
        }
    }
    void rejectedPresentationRetainsNewestIntentWithoutNewArrival() {
        for(bool newer:{false,true})for(bool identical:{false,true}) {
            QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());CollectionLinkActivation activation(lock,legacy);
            QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);activation.setDestinationReady(true);QVERIFY(activation.submit(QUrl(link('a'))));QTRY_COMPARE(opened.size(),1);
            const auto a=opened[0][1].toString();activation.setDestinationReady(false);
            if(newer)QVERIFY(activation.submit(QUrl(link(identical?'a':'b'))));
            QVERIFY(activation.deferPresented(a));activation.setDestinationReady(true);QTRY_COMPARE(opened.size(),2);
            QCOMPARE(opened[1][0].toUrl(),QUrl(link(newer && !identical?'b':'a')));const auto b=opened[1][1].toString();QVERIFY(a!=b);
            QVERIFY(!activation.deferPresented(a));QCoreApplication::processEvents();QCOMPARE(opened.size(),2);
        }
    }
    void handlerQueuedWorkStopsAtShutdown() {
        QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());
        CollectionLinkActivation activation(lock,legacy);QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);activation.registerPlatformHandlers();activation.setDestinationReady(true);
        QVERIFY(QDesktopServices::openUrl(QUrl("colosseum://collection/invalid")));QCoreApplication::processEvents();QCOMPARE(opened.size(),0);
        QVERIFY(QDesktopServices::openUrl(QUrl("colosseum://collection/"+QString(43,'h'))));QTRY_COMPARE(opened.size(),1);
        QVERIFY(QDesktopServices::openUrl(QUrl("colosseum://collection/"+QString(43,'i'))));activation.shutdown();QCoreApplication::processEvents();QCOMPARE(opened.size(),1);
    }
    void lockIdentityAndMalformedIpc() {
        QTemporaryDir a,b;auto lock=std::make_shared<ColosseumInstanceLock>(a.path());CollectionLinkActivation activation(lock,legacy);
        QVERIFY(!activation.startPrimary(a.path()));QVERIFY(lock->tryAcquire());QVERIFY(activation.startPrimary(a.path()));
        QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);activation.setDestinationReady(true);
        auto other=std::make_shared<ColosseumInstanceLock>(b.path());QVERIFY(other->tryAcquire());CollectionLinkActivation isolated(other,legacy);QVERIFY(isolated.startPrimary(b.path()));QVERIFY(activation.channelName()!=isolated.channelName());QVERIFY(!activation.startPrimary(b.path()));
        QSignalSpy isolatedOpened(&isolated,&CollectionLinkActivation::openRequested);isolated.setDestinationReady(true);
        QLocalSocket tagged;tagged.connectToServer(isolated.channelName());QVERIFY(tagged.waitForConnected(500));tagged.write(QByteArray(43,'z')+'\n');tagged.flush();
        QTRY_COMPARE(isolatedOpened.size(),1);QCOMPARE(opened.size(),0);QTRY_VERIFY(tagged.bytesAvailable()>0);QCOMPARE(tagged.readAll(),QByteArray("OK\n"));
        for(const auto &payload:QList<QByteArray>{"command:save\n",QByteArray(43,'a')+"\nextra",QByteArray(4096,'x')}){
            QLocalSocket socket;socket.connectToServer(activation.channelName());QVERIFY(socket.waitForConnected(500));socket.write(payload);socket.flush();
            QTRY_VERIFY(socket.state()==QLocalSocket::UnconnectedState);QCOMPARE(opened.size(),0);
        }
    }
    void prebufferedSplitAndExcessFramesHaveAtMostOneAcceptance() {
        QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());CollectionLinkActivation activation(lock,legacy);QVERIFY(activation.startPrimary(root.path()));activation.setDestinationReady(true);
        QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);
        for(int i=0;i<5;++i){
            QLocalSocket socket;socket.connectToServer(activation.channelName());QVERIFY(socket.waitForConnected(500));
            const auto frame=QByteArray(43,char('l'+i))+'\n';
            if(i%2){socket.write(frame.left(43));QVERIFY(socket.waitForBytesWritten(500));socket.write(frame.right(1));}
            else socket.write(frame);
            socket.flush();QVERIFY(socket.waitForReadyRead(500));QByteArray ack=socket.readAll();
            if(socket.state()!=QLocalSocket::UnconnectedState)socket.waitForDisconnected(500);
            ack+=socket.readAll();QCOMPARE(ack,QByteArray("OK\n"));QTRY_COMPARE(opened.size(),i+1);
            QCoreApplication::processEvents();QCOMPARE(opened.size(),i+1);
        }
        for(const auto &frame:QList<QByteArray>{QByteArray(43,'r')+"\nX",QByteArray(43,'s')+"\n"+QByteArray(43,'t')+"\n"}){
            QLocalSocket socket;socket.connectToServer(activation.channelName());QVERIFY(socket.waitForConnected(500));socket.write(frame);socket.flush();
            if(socket.state()!=QLocalSocket::UnconnectedState)socket.waitForDisconnected(1000);
            QVERIFY(socket.readAll().isEmpty());QCoreApplication::processEvents();QCOMPARE(opened.size(),5);
        }
        QVERIFY(activation.shutdown());
    }
    void ipcMailboxOrdersArrivalsAndGuardsDismissalBeforeGuiNotification() {
        for(bool identical:{false,true}) {
            QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());CollectionLinkActivation activation(lock,legacy);QVERIFY(activation.startPrimary(root.path()));
            QSignalSpy opened(&activation,&CollectionLinkActivation::openRequested);activation.setDestinationReady(true);QVERIFY(activation.submit(QUrl(link('a'))));QTRY_COMPARE(opened.size(),1);
            const auto a=opened[0][1].toString();
            // Blocking wait intentionally prevents the GUI mailbox notification.
            QVERIFY(forwardFixture(root.path(),link(identical?'a':'b')));QCOMPARE(opened.size(),1);
            QVERIFY(!activation.dismissPresented(a));QTRY_COMPARE(opened.size(),2);QCOMPARE(opened[1][0].toUrl(),QUrl(link(identical?'a':'b')));
            QVERIFY(!activation.dismissPresented(a));
            QVERIFY(forwardFixture(root.path(),link('c')));QCOMPARE(opened.size(),2);
            QVERIFY(activation.submit(QUrl(link('d'))));QTRY_COMPARE(opened.size(),3);QCOMPARE(opened[2][0].toUrl(),QUrl(link('d')));
            QVERIFY(activation.shutdown());
        }
    }
    void coldIntentAndBoundedStopLease() {
        QTemporaryDir root;auto lock=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(lock->tryAcquire());std::weak_ptr<ColosseumInstanceLock> lease=lock;
        auto activation=std::make_unique<CollectionLinkActivation>(lock,legacy);QSignalSpy opened(activation.get(),&CollectionLinkActivation::openRequested);
        QVERIFY(activation->submit(QUrl(link('e'))));QVERIFY(activation->startPrimary(root.path()));
        QVERIFY(forwardFixture(root.path(),link('f')));activation->setDestinationReady(true);QTRY_COMPARE(opened.size(),1);QCOMPARE(opened[0][0].toUrl(),QUrl(link('f')));
        QVERIFY(forwardFixture(root.path(),link('g')));QCOMPARE(opened.size(),1);
        QVERIFY(!activation->shutdown(0));activation.reset();lock.reset();
        // No GUI event processing yet: self-owned cleanup retains the exact lock.
        ColosseumInstanceLock contender(root.path());QVERIFY(!contender.tryAcquire());QVERIFY(!lease.expired());
        QTRY_VERIFY(lease.expired());QVERIFY(contender.tryAcquire());QCOMPARE(opened.size(),1);
    }
    void processPairAcknowledgesBlockedBootstrap() {
        QTemporaryDir root;const auto output=root.filePath("delivered");QProcess primary;
        primary.start(QCoreApplication::applicationFilePath(),{"--fixture-blocked-primary",root.path(),output,"blocked"});QVERIFY(primary.waitForStarted());QTRY_VERIFY(QFile::exists(root.filePath("owner")));
        QProcess sender;auto env=QProcessEnvironment::systemEnvironment();env.insert("COLLECTION_ACTIVATION_TEST_ROOT",root.path());sender.setProcessEnvironment(env);
        sender.start(QCoreApplication::applicationFilePath(),{"--collection-url",link('k')});QVERIFY(sender.waitForFinished(2500));QCOMPARE(sender.exitCode(),0);QVERIFY(read(output).isEmpty());
        QFile ready(root.filePath("ready"));QVERIFY(ready.open(QIODevice::WriteOnly));ready.write("ready");ready.close();QTRY_COMPARE(read(output),link('k').toUtf8()+'\n');
        primary.kill();QVERIFY(primary.waitForFinished());
    }
    void processPairsBootingAcknowledgmentLatestWarmAndRecovery() {
        QTemporaryDir root;const auto output=root.filePath("delivered");const auto marker=root.filePath("owner");
        QProcess primary;primary.start(QCoreApplication::applicationFilePath(),{"--fixture-primary",root.path(),output,"600"});QVERIFY(primary.waitForStarted());QTRY_VERIFY(QFile::exists(marker));
        auto send=[&](QChar c){QProcess sender;auto env=QProcessEnvironment::systemEnvironment();env.insert("COLLECTION_ACTIVATION_TEST_ROOT",root.path());sender.setProcessEnvironment(env);sender.start(QCoreApplication::applicationFilePath(),{"--collection-url",link(c)});if(!sender.waitForFinished(2500))return false;return sender.exitCode()==0;};
        QVERIFY(send('a'));QVERIFY(send('b'));QVERIFY(read(output).isEmpty());
        QFile ready(root.filePath("ready"));QVERIFY(ready.open(QIODevice::WriteOnly));ready.write("ready");ready.close();QTRY_COMPARE(read(output),link('b').toUtf8()+'\n');
        QVERIFY(send('c'));QTRY_COMPARE(read(output),(link('b')+'\n'+link('c')+'\n').toUtf8());
        for(const auto &extra:QStringList{"evil.qml","-platform","--update-token","--collection-url","-platformpluginpath","-plugin","-platformtheme"}){
            QProcess bad;auto env=QProcessEnvironment::systemEnvironment();env.insert("COLLECTION_ACTIVATION_TEST_ROOT",root.path());bad.setProcessEnvironment(env);bad.start(QCoreApplication::applicationFilePath(),{"--collection-url",link(),extra,"definitely-invalid"});QVERIFY(bad.waitForFinished(2000));QCOMPARE(bad.exitCode(),2);QVERIFY(bad.readAllStandardError().contains("exclusive_collection_activation"));
        }
        primary.kill();QVERIFY(primary.waitForFinished());auto recovered=std::make_shared<ColosseumInstanceLock>(root.path());QVERIFY(recovered->tryAcquire());CollectionLinkActivation replacement(recovered,legacy);QVERIFY(replacement.startPrimary(root.path()));
    }
};
int main(int argc,char **argv) {
    QStringList raw;for(int i=0;i<argc;++i)raw.append(QString::fromLocal8Bit(argv[i]));QString error;auto incoming=takeCollectionLaunchUrl(&raw,&error);
    if(!error.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(error));return 2;}
    if(incoming){argc=1;argv[1]=nullptr;}
    QCoreApplication app(argc,argv);
    if(incoming){if(app.arguments().size()!=1)return 7;auto lock=std::make_shared<ColosseumInstanceLock>(qEnvironmentVariable("COLLECTION_ACTIVATION_TEST_ROOT"));if(lock->tryAcquire())return 3;CollectionLinkActivation activation(lock,legacy);return activation.forwardToPrimary(*incoming)?0:4;}
    const auto args=app.arguments();
    if(args.value(1)=="--fixture-primary" || args.value(1)=="--fixture-blocked-primary"){
        auto lock=std::make_shared<ColosseumInstanceLock>(args[2]);
        if(!lock->tryAcquire())return 5;
        CollectionLinkActivation activation(lock,legacy);
        QObject::connect(&activation,&CollectionLinkActivation::openRequested,&app,[&](const QUrl &url){
            QFile f(args[3]);const auto bytes=url.toString().toUtf8()+'\n';
            if(!f.open(QIODevice::WriteOnly|QIODevice::Append) || f.write(bytes)!=bytes.size())app.exit(8);
        });
        if(args.value(1)=="--fixture-blocked-primary"){
            if(!activation.startPrimary(args[2]))return 6;
        }else QTimer::singleShot(100,&app,[&]{if(!activation.startPrimary(args[2]))app.exit(6);});
        QFile owner(args[2]+"/owner");
        if(!owner.open(QIODevice::WriteOnly) || owner.write("one owner")!=9)return 8;
        owner.close();
        QTimer readiness;readiness.setInterval(10);
        QObject::connect(&readiness,&QTimer::timeout,&activation,[&]{
            if(QFile::exists(args[2]+"/ready")){readiness.stop();activation.setDestinationReady(true);}
        });
        readiness.start();QTimer::singleShot(8000,&app,&QCoreApplication::quit);
        if(args.value(1)=="--fixture-blocked-primary")QThread::msleep(3500);
        return app.exec();
    }
    Checks checks;return QTest::qExec(&checks,argc,argv);
}
#include "collection_link_activation_test.moc"
