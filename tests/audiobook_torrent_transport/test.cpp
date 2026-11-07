#include "../../native/engine/AudiobookDownloader.h"
#include "../../native/player/streamserver.h"
#include "../../native/engine/AudiobookTorrentTransport.h"
#include "../../native/AudioPairingStore.h"
#include "fake_engine.h"
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QtTest>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStandardPaths>
#include <QDir>
extern QString stubStreamUrl;
class Tests:public QObject {
 Q_OBJECT
private:
 const QString A=QString(40,'a'), B=QString(40,'b'), C=QString(40,'c');
 const QString evidence=QDir::tempPath();
 void clearOwnData() {
  const QString path=QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  QVERIFY(path.endsWith("/qttest/ColosseumNetworkTests/"+QCoreApplication::applicationName()));
  QVERIFY(QCoreApplication::applicationName().startsWith("audiobook-transport-"));
  if(QDir(path).exists()) QVERIFY(QDir(path).removeRecursively());
 }
private slots:
 void init(){clearOwnData();}
 void cleanup(){clearOwnData();}
 void nativeNestedSuccessPreservesIndexAndPairing() {
  QTemporaryDir root(evidence+"/success-XXXXXX");QVERIFY(root.isValid());
  FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.autoComplete=true;
  QNetworkAccessManager nam;AudioPairingStore pairing(root.path()+"/pairing.ini");
  QStringList original;
  {
   AudiobookDownloader books(&nam,nullptr);books.setPairing(&pairing);
   books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
   QSignalSpy done(&books,&AudiobookDownloader::finished),failed(&books,&AudiobookDownloader::failed),progress(&books,&AudiobookDownloader::progress);
   books.downloadAudiobook("one|author",A,"One","Author","reader-id","/synthetic/book.epub");
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);QCOMPARE(failed.size(),0);
   QCOMPARE(engine.priorities,QList<QVector<int>>({{0,4,4}}));QCOMPARE(engine.payloadStarts,1);
   QCOMPARE(engine.deletions,QList<bool>({false}));
   original=books.localFiles("one|author");QCOMPARE(original.size(),2);
   QVERIFY(original[0].endsWith("/Book/Chapter 2.mp3"));QVERIFY(original[1].endsWith("/Book/Chapter 10.mp3"));
   QCOMPARE(progress.last()[1].toLongLong(),5);QCOMPARE(progress.last()[2].toLongLong(),5);
   QCOMPARE(pairing.getPairing("reader-id").value("audioDir").toString(),books.localAudiobook("one|author"));
  }
  AudiobookDownloader restarted(&nam,nullptr);QCOMPARE(restarted.localFiles("one|author"),original);
  QCOMPARE(pairing.getPairing("reader-id").value("pairKey").toString(),"one|author");
 }
 void preownedHashIsUntouched() {
  QTemporaryDir root(evidence+"/owned-XXXXXX");FakeEngine engine;engine.paths[A]=root.path();
  AudiobookTorrentTransport transport(&engine,{60,60,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path()+"/request");QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);
  QVERIFY(failed[0][1].toString().contains("already in use"));
  QCOMPARE(engine.adds.size(),0);QCOMPARE(engine.priorities.size(),0);QCOMPARE(engine.payloadStarts,0);QCOMPARE(engine.deletions.size(),0);QCOMPARE(engine.paths[A],root.path());
 }
 void duplicateActualPathIsUntouched() {
  QTemporaryDir root(evidence+"/duplicate-XXXXXX");FakeEngine engine;engine.duplicatePath=root.path();
  AudiobookTorrentTransport transport(&engine,{60,60,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path()+"/request");QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);
  QCOMPARE(engine.adds.size(),1);QCOMPARE(engine.priorities.size(),0);QCOMPARE(engine.payloadStarts,0);QCOMPARE(engine.deletions.size(),0);
 }
 void aggregateFinishDoesNotPublishIncompleteFiles() {
  QTemporaryDir root(evidence+"/incomplete-XXXXXX");FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();
  AudiobookTorrentTransport transport(&engine,{500,500,5});QSignalSpy done(&transport,&AudiobookTorrentTransport::completed),failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(engine.payloadStarts,1,500);
  engine.complete(A);engine.counts[A]=QJsonArray{1,3,1};emit engine.changed(A);
  QTest::qWait(40);QCOMPARE(done.size(),0);QCOMPARE(failed.size(),0);
  engine.counts[A]=QJsonArray{1,3,2};emit engine.changed(A);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,500);
 }
 void verifiedBytesNeedExactContainedDiskFiles() {
  QTemporaryDir root(evidence+"/missing-XXXXXX");FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.counts[A]={1,3,2};
  AudiobookTorrentTransport transport(&engine,{60,60,5});QSignalSpy done(&transport,&AudiobookTorrentTransport::completed),failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);QCOMPARE(done.size(),0);
 }
 void invalidManifest_data() {
  QTest::addColumn<QString>("path");
  QTest::newRow("traversal")<<"../outside.mp3";QTest::newRow("absolute")<<"C:/outside.mp3";
  QTest::newRow("unc")<<"//server/outside.mp3";QTest::newRow("empty-segment")<<"Book//one.mp3";
 }
 void invalidManifest() {
  QFETCH(QString,path);QTemporaryDir root(evidence+"/invalid-XXXXXX");FakeEngine engine;
  engine.manifests[A]={QJsonObject{{"index",0},{"name",path},{"size",2}}};
  AudiobookTorrentTransport transport(&engine,{60,60,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);
  QCOMPARE(engine.priorities.size(),0);QCOMPARE(engine.deletions,QList<bool>({false}));
 }
 void nativeInactivityAndNoAudioAreBounded() {
  QTemporaryDir root(evidence+"/stall-XXXXXX");FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();
  AudiobookTorrentTransport transport(&engine,{70,40,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);
  QVERIFY(failed[0][1].toString().contains("without new verified"));QCOMPARE(engine.deletions,QList<bool>({true}));
  engine.manifests[A]={QJsonObject{{"index",0},{"name","cover.jpg"},{"size",2}}};
  transport.start(2,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(failed.size(),2,500);QVERIFY(failed[1][1].toString().contains("no audio"));
 }
 void symlinkCannotEscapeOrDeleteOtherFiles() {
  QTemporaryDir root(evidence+"/symlink-XXXXXX");const QString outside=root.path()+"/outside.mp3";
  QFile file(outside);QVERIFY(file.open(QIODevice::WriteOnly));file.write("safe");file.close();
  FakeEngine engine;engine.manifests[A]={QJsonObject{{"index",0},{"name","linked.mp3"},{"size",4}}};
  bool linked=false;
  engine.afterAdd=[&](const QString&,const QString& path) {
   const QString dest=path+"/linked.mp3";
#ifdef Q_OS_WIN
   linked=CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(dest.utf16()),reinterpret_cast<LPCWSTR>(outside.utf16()),2)!=0;
#else
   linked=QFile::link(outside,dest);
#endif
  };
  AudiobookTorrentTransport transport(&engine,{80,80,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,A,root.path()+"/private");QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),1,500);
  if(!linked) {transport.cancel(1);QSKIP("Host does not permit a real file symlink");}
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);QCOMPARE(engine.deletions,QList<bool>({false}));
  QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),"safe");
 }
 void queuedCancellationAndRemovedObserverRetireFirst() {
  FakeEngine engine;QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy failed(&books,&AudiobookDownloader::failed),removed(&books,&AudiobookDownloader::removed);
  connect(&books,&AudiobookDownloader::removed,&books,[&](const QString& key){QCOMPARE(books.statusOf(key).value("state").toString(),"none");});
  books.downloadAudiobook("a|author",A,"A","Author");books.downloadAudiobook("b|author",B,"B","Author");
  books.cancelDownload("b|author");QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),1,500);
  QCOMPARE(engine.adds[0],A);books.cancelDownload("a|author");QTest::qWait(30);
  QTRY_COMPARE_WITH_TIMEOUT(removed.size(),2,500);QCOMPARE(failed.size(),0);QCOMPARE(books.activeCount(),0);
  QVERIFY(books.removeIncompleteDownloads().value("success").toBool());
 }
 void ownershipChangedByProgressObserverCannotPublishOrRemove() {
  QTemporaryDir root(evidence+"/changed-XXXXXX");FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.autoComplete=true;
  AudiobookTorrentTransport transport(&engine,{100,100,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed),done(&transport,&AudiobookTorrentTransport::completed);
  connect(&transport,&AudiobookTorrentTransport::progress,&transport,[&](quint64,qint64,qint64){engine.paths[A]=root.path();});
  transport.start(1,A,root.path()+"/private");QTest::qWait(100);
  QCOMPARE(failed.size(),0);QCOMPARE(done.size(),0);QCOMPARE(engine.deletions.size(),0);
 }
 void failureObserverSameKeyRetryHasNoLateFailure() {
  FakeEngine engine;QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{40,100,5}));
  QSignalSpy failed(&books,&AudiobookDownloader::failed),done(&books,&AudiobookDownloader::finished);
  bool retried=false;connect(&books,&AudiobookDownloader::failuresChanged,&books,[&]{
   if(retried)return;retried=true;engine.manifests[A]=FakeEngine::mixed();engine.autoComplete=true;
   books.downloadAudiobook("a|author",A,"A","Author");
  });
  books.downloadAudiobook("a|author",A,"A","Author");QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
  QCOMPARE(failed.size(),0);QCOMPARE(engine.adds.size(),2);
 }
 void activeFacadeAndEngineDestructionOrders() {
  QNetworkAccessManager nam;FakeEngine engine;auto books=new AudiobookDownloader(&nam,nullptr);
  books->setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));books->downloadAudiobook("a|author",A,"A","Author");
  QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),1,500);delete books;QCOMPARE(engine.deletions.size(),1);emit engine.changed(A);QTest::qWait(10);
  auto other=new FakeEngine;AudiobookDownloader survivor(&nam,nullptr);survivor.setTorrentTransport(new AudiobookTorrentTransport(other,{300,300,5}));
  QSignalSpy failed(&survivor,&AudiobookDownloader::failed);survivor.downloadAudiobook("b|author",B,"B","Author");QTRY_COMPARE_WITH_TIMEOUT(other->adds.size(),1,500);
  delete other;QCOMPARE(failed.size(),1);QCOMPARE(survivor.activeCount(),0);
 }
 void settlementRetainsFacadeAndIgnoresOldTokens() {
  FakeEngine engine;engine.holdRemovals=true;QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy removed(&books,&AudiobookDownloader::removed),failed(&books,&AudiobookDownloader::failed);
  books.downloadAudiobook("a|author",A,"A","Author");QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),1,500);
  books.cancelDownload("a|author");QCOMPARE(books.activeCount(),1);QCOMPARE(books.activeDownloads()[0].toMap()["state"].toString(),"stopping");
  QCOMPARE(removed.size(),0);QVERIFY(!books.removeIncompleteDownloads()["success"].toBool());
  const QString old=engine.tokens[0];engine.ack(old);QTRY_COMPARE_WITH_TIMEOUT(removed.size(),1,500);QCOMPARE(books.activeCount(),0);
  books.downloadAudiobook("a|author",A,"A","Author");QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),2,500);
  emit engine.removalSettled(old,true,0);QTest::qWait(10);QCOMPARE(books.activeCount(),1);QCOMPARE(removed.size(),1);QCOMPARE(failed.size(),0);
  books.cancelDownload("a|author");engine.ack(engine.tokens.last());QTRY_COMPARE_WITH_TIMEOUT(removed.size(),2,500);
  QVERIFY(books.removeIncompleteDownloads()["success"].toBool());
 }
 void deleteFailureIsRealAndFinishingCancelMayComplete() {
  FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.autoComplete=true;engine.holdRemovals=true;
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished),failed(&books,&AudiobookDownloader::failed);
  books.downloadAudiobook("a|author",A,"A","Author");QTRY_COMPARE_WITH_TIMEOUT(engine.tokens.size(),1,500);
  QCOMPARE(books.activeDownloads()[0].toMap()["state"].toString(),"finishing");QCOMPARE(done.size(),0);
  books.cancelDownload("a|author");QCOMPARE(engine.tokens.size(),1);QCOMPARE(engine.deletions[0],false);
  engine.ack(engine.tokens[0]);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,500);
  engine.autoComplete=false;engine.manifests[B]=FakeEngine::mixed();books.downloadAudiobook("b|author",B,"B","Author");QTRY_COMPARE_WITH_TIMEOUT(engine.payloadStarts,2,500);
  books.cancelDownload("b|author");QCOMPARE(books.activeCount(),1);engine.ack(engine.tokens.last(),false,13);
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);QVERIFY(failed[0][1].toString().contains("13"));QCOMPARE(books.activeCount(),0);
 }
 void reentrantCancellationDuringEngineCalls_data() {
  QTest::addColumn<int>("stage");QTest::newRow("start")<<0;QTest::newRow("add")<<1;QTest::newRow("priorities")<<2;QTest::newRow("payload")<<3;
 }
 void reentrantCancellationDuringEngineCalls() {
  QFETCH(int,stage);QTemporaryDir root(evidence+"/reentrant-XXXXXX");FakeEngine engine;engine.running=stage!=0;engine.manifests[A]=FakeEngine::mixed();
  AudiobookTorrentTransport transport(&engine,{100,100,5});QSignalSpy cancelled(&transport,&AudiobookTorrentTransport::cancelled),failed(&transport,&AudiobookTorrentTransport::failed);
  auto cancel=[&]{transport.cancel(1);};
  if(stage==0)engine.duringStart=cancel;
  if(stage==1)engine.afterAdd=[&](const QString&,const QString&){cancel();};
  if(stage==2)engine.onPriorities=cancel;
  if(stage==3)engine.onPayload=cancel;
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(),1,500);QCOMPARE(failed.size(),0);
  QCOMPARE(engine.paths.size(),0);QCOMPARE(engine.adds.size(),stage==0?0:1);
 }
 void destructionDuringAddRetiresCapturedAdmission() {
  QTemporaryDir root(evidence+"/destroy-add-XXXXXX");FakeEngine engine;auto transport=new AudiobookTorrentTransport(&engine,{100,100,5});
  engine.afterAdd=[&](const QString&,const QString&){delete transport;transport=nullptr;};
  transport->start(1,A,root.path());QTRY_VERIFY_WITH_TIMEOUT(transport==nullptr,500);
  QCOMPARE(engine.paths.size(),0);QCOMPARE(engine.tokens.size(),1);QCOMPARE(engine.deletions[0],false);
 }
 void deadlinesAndCancelGeneration() {
  QTemporaryDir root(evidence+"/deadline-XXXXXX");FakeEngine engine;
  AudiobookTorrentTransport transport(&engine,{70,70,5});QSignalSpy failed(&transport,&AudiobookTorrentTransport::failed),cancelled(&transport,&AudiobookTorrentTransport::cancelled);
  transport.start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),1,500);
  QTest::qWait(30);transport.cancel(1);QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(),1,500);transport.start(2,A,root.path());
  QTRY_COMPARE_WITH_TIMEOUT(engine.adds.size(),2,500);emit engine.changed(A);QTest::qWait(35);QCOMPARE(failed.size(),0);
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,500);QCOMPARE(failed[0][0].toULongLong(),quint64(2));QVERIFY(failed[0][1].toString().contains("metadata timed out"));
 }
 void failureObserverAndFifo() {
  FakeEngine engine;engine.manifests[B]=FakeEngine::mixed();engine.manifests[C]=FakeEngine::mixed();engine.autoComplete=true;
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{60,60,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished),failed(&books,&AudiobookDownloader::failed);
  bool inserted=false;connect(&books,&AudiobookDownloader::failuresChanged,&books,[&] {
   if(inserted)return;inserted=true;QCOMPARE(books.statusOf("a|author").value("state").toString(),"none");books.downloadAudiobook("c|author",C,"C","Author");
  });
  books.downloadAudiobook("a|author",A,"A","Author");books.downloadAudiobook("b|author",B,"B","Author");
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,1500);QCOMPARE(failed.size(),1);QCOMPARE(engine.adds,QList<QString>({A,B,C}));
  QCOMPARE(done[0][0].toString(),"b|author");QCOMPARE(done[1][0].toString(),"c|author");
 }
 void engineAndTransportDestructionOrders() {
  QTemporaryDir root(evidence+"/lifetime-XXXXXX");auto engine=new FakeEngine;
  auto transport=new AudiobookTorrentTransport(engine,{300,300,5});QSignalSpy failed(transport,&AudiobookTorrentTransport::failed);
  transport->start(1,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(engine->adds.size(),1,500);
  delete engine;QCOMPARE(failed.size(),1);delete transport;
  FakeEngine second;auto other=new AudiobookTorrentTransport(&second,{300,300,5});other->start(2,A,root.path());QTRY_COMPARE_WITH_TIMEOUT(second.adds.size(),1,500);delete other;QCOMPARE(second.deletions.size(),1);
 }

 void desktopHttpErrorsAreHonest() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  connect(&server,&QTcpServer::newConnection,&server,[&] { auto s=server.nextPendingConnection(); connect(s,&QTcpSocket::readyRead,s,[s] { s->readAll();s->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");s->disconnectFromHost(); });connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater); });
  stubStreamUrl=QString("http://127.0.0.1:%1/stream/opaque").arg(server.serverPort());
  QNetworkAccessManager nam; StreamServer stream; AudiobookDownloader books(&nam,&stream);
  QSignalSpy failed(&books,&AudiobookDownloader::failed);
  books.downloadAudiobook("synthetic|author",QString(40,'a'),"Synthetic","Author");
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,9000);
  QVERIFY(failed[0][1].toString().contains("405"));
  QVERIFY(!failed[0][1].toString().contains("no peers"));
 }
};
int main(int argc,char**argv) { QCoreApplication app(argc,argv); app.setOrganizationName("ColosseumNetworkTests");app.setApplicationName("audiobook-transport-"+QString::number(app.applicationPid()));QStandardPaths::setTestModeEnabled(true); Tests tests;return QTest::qExec(&tests,argc,argv); }
#include "test.moc"
