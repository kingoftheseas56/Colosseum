#include "../../native/engine/AudiobookDownloader.h"
#include "../../native/engine/LibriVoxSearch.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
extern QString stubStreamUrl;
class LocalArchiveNetwork : public QNetworkAccessManager {
public:
 explicit LocalArchiveNetwork(quint16 port) : m_port(port) {}
protected:
 QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* body) override {
  QNetworkRequest local(request);
  QUrl url = request.url();
  url.setScheme("http");
  url.setHost("127.0.0.1");
  url.setPort(m_port);
  local.setUrl(url);
  return QNetworkAccessManager::createRequest(op, local, body);
 }
private:
 quint16 m_port;
};

class Tests:public QObject {
 Q_OBJECT
private:
 const QString A=QString(40,'a'), B=QString(40,'b'), C=QString(40,'c');
 const QString evidence=QDir::tempPath();
 QVariantMap libriVoxRequest() const {
  return {
   {"pairKey", "pride and prejudice|jane austen"},
   {"recordingKey", "pride and prejudice|jane austen#librivox:1480"},
   {"title", "Pride and Prejudice (version 3)"}, {"author", "Jane Austen"},
   {"sourceKind", "librivox"}, {"sourceRef", "1480"},
   {"chapters", QVariantList{QVariantMap{{"number", 1}, {"title", "Chapter 01"},
                  {"url", "https://www.archive.org/download/fixture/chapter_01.mp3"}}}}
  };
 }
 void clearOwnData() {
  const QString path=QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  QVERIFY(path.endsWith("/qttest/ColosseumNetworkTests/"+QCoreApplication::applicationName()));
  QVERIFY(QCoreApplication::applicationName().startsWith("audiobook-transport-"));
  if(QDir(path).exists()) QVERIFY(QDir(path).removeRecursively());
 }
private slots:
 void sourceConsentBlocksNewAcquisition() {
  QNetworkAccessManager nam;
  AudiobookDownloader downloader(&nam,nullptr);
  bool enabled=false;
  downloader.setSourceEnabledCheck([&enabled]{return enabled;});
  QSignalSpy failure(&downloader,&AudiobookDownloader::failed);
  downloader.downloadAudiobook("consent",A,"Fixture","Author");
  QCOMPARE(failure.count(),1);
  QVERIFY(failure.at(0).at(1).toString().contains("Enable AudioBookBay"));
  QCOMPARE(downloader.activeCount(),0);
  enabled=true;
  downloader.downloadAudiobook("consent",A,"Fixture","Author");
  QCOMPARE(failure.count(),2);
  QCOMPARE(failure.at(1).at(1).toString(),QStringLiteral("stream engine unavailable"));
 }
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

 void catalogueWorkIdentitySurvivesCompletionAndReload() {
  FakeEngine engine;
  engine.manifests[A]=FakeEngine::mixed();
  engine.autoComplete=true;
  QNetworkAccessManager nam;
  QTemporaryDir root(evidence+"/identity-XXXXXX");QVERIFY(root.isValid());
  AudioPairingStore pairing(root.path()+"/pairs.ini");
  const QString key="one|author#as:recording-123";
  const QString workId="work-456";
  {
   AudiobookDownloader books(&nam,nullptr);books.setPairing(&pairing);
   books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
   QSignalSpy done(&books,&AudiobookDownloader::finished);
   QVariantMap request{{"pairKey","one|author"},{"infoHash",A},{"title","One"},
                       {"author","Author"},{"workId",workId},{"bookId","reader-id"},
                       {"audiosiloRecordingId","recording-123"}};
   books.downloadRecording(request);
   request["workId"]="different-work";
   books.downloadRecording(request);
   QCOMPARE(books.activeDownloads().size(),1);
   QCOMPARE(books.activeDownloads()[0].toMap().value("workId").toString(),workId);
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
   QCOMPARE(done[0][0].toString(),key);
   QCOMPARE(engine.adds,QList<QString>{A});
   QCOMPARE(books.downloadedAudiobooks()[0].toMap().value("workId").toString(),workId);
   QCOMPARE(pairing.getPairing("reader-id").value("recordingKey").toString(),key);
   QVERIFY(pairing.getPairing(workId).isEmpty());
  }
  AudiobookDownloader restarted(&nam,nullptr);
  QCOMPARE(restarted.downloadedAudiobooks().size(),1);
  const QVariantMap downloaded=restarted.downloadedAudiobooks()[0].toMap();
  QCOMPARE(downloaded.value("workId").toString(),workId);
  QCOMPARE(downloaded.value("bookId").toString(),"reader-id");
  QCOMPARE(restarted.recordingsForWork("one|author").size(),1);
  QCOMPARE(restarted.recordingsForWork("one|author")[0].toMap().value("workId").toString(),workId);
 }
 void failedRecordingRetainsCatalogueWorkIdentity() {
  FakeEngine engine;
  engine.manifests[A]={QJsonObject{{"index",0},{"name","cover.jpg"},{"size",1}}};
  QNetworkAccessManager nam;
  AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy failed(&books,&AudiobookDownloader::failed);
  books.downloadRecording({{"pairKey","one|author"},{"infoHash",A},{"title","One"},
                           {"author","Author"},{"workId","work-456"}});
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,1000);
  QCOMPARE(books.activeDownloads().size(),1);
  const QVariantMap row=books.activeDownloads()[0].toMap();
  QCOMPARE(row.value("state").toString(),"failed");
  QCOMPARE(row.value("workId").toString(),"work-456");
  QVERIFY(books.downloadedAudiobooks().isEmpty());
 }
 void distinctRecordingsAndDeletion() {
  FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.manifests[B]=FakeEngine::mixed();engine.autoComplete=true;
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished);
  books.downloadAudiobook("one|author",A,"One","Author");
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
  books.downloadAudiobook("one|author",B,"One","Author");
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,1000);
  const QString ka="one|author#src:"+A,kb="one|author#src:"+B;
  const QString first=books.localAudiobook(ka),second=books.localAudiobook(kb);
  QVERIFY(!first.isEmpty());QVERIFY(!second.isEmpty());QVERIFY(first!=second);
  QCOMPARE(books.recordingsForWork("one|author").size(),2);
  QCOMPARE(books.localFiles(ka).size(),2);QCOMPARE(books.localFiles(kb).size(),2);
  QVERIFY(books.deleteAudiobook(ka).value("success").toBool());
  QVERIFY(books.localFiles(ka).isEmpty());QCOMPARE(books.localFiles(kb).size(),2);
  QCOMPARE(books.recordingsForWork("one|author").size(),1);
 }
 void recordingDefaultAndRecentPlay() {
  FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.manifests[B]=FakeEngine::mixed();engine.autoComplete=true;
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished);
  books.downloadAudiobook("same|author",A,"Same","Author");QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
  books.downloadAudiobook("same|author",B,"Same","Author");QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,1000);
  const QString ka="same|author#src:"+A,kb="same|author#src:"+B;
  QCOMPARE(books.resolveRecordingKey("same|author"),QString());
  QVERIFY(books.markRecordingPlayed(kb));QCOMPARE(books.resolveRecordingKey("same|author"),kb);
  QVERIFY(books.setDefaultRecording("same|author",ka));
  QCOMPARE(books.resolveRecordingKey("same|author"),ka);
  QVERIFY(!books.setDefaultRecording("other|author",kb));
  QCOMPARE(books.localFiles("same|author"),books.localFiles(ka));
  QVERIFY(books.deleteAudiobook(ka).value("success").toBool());
  QCOMPARE(books.resolveRecordingKey("same|author"),kb);
 }
 void legacyIndexKeepsBarePairKey() {
  const QString appDir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dir=appDir+"/audiobooks/legacy";QVERIFY(QDir().mkpath(dir));
  const QString path=dir+"/chapter.mp3";
  QFile audio(path);QVERIFY(audio.open(QIODevice::WriteOnly));audio.write("fixture");audio.close();
  const QJsonObject legacy{{"dir",dir},{"title","Legacy"},{"author","Writer"},
                           {"bytes",7},{"addedAt",1234},{"files",QJsonArray{path}}};
  QFile f(appDir+"/audiobooks/index.json");QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(QJsonObject{{"legacy|writer",legacy}}).toJson());f.close();
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  QCOMPARE(books.resolveRecordingKey("legacy|writer"),"legacy|writer");
  QCOMPARE(books.localFiles("legacy|writer"),QStringList{path});
  QCOMPARE(books.recordingsForWork("legacy|writer").size(),1);
  QCOMPARE(books.recordingsForWork("legacy|writer")[0].toMap().value("recordingKey").toString(),"legacy|writer");
  QVERIFY(books.downloadedAudiobooks()[0].toMap().value("workId").toString().isEmpty());
 }
 void legacyAndModernRecordingCoexistWithoutImplicitPreference() {
  const QString root=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dir=root+"/audiobooks/old";QVERIFY(QDir().mkpath(dir));
  const QString file=dir+"/narration.mp3";
  QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write("legacy");f.close();
  const QJsonObject old{{"dir",dir},{"title","Old"},{"author","Writer"},
                        {"files",QJsonArray{file}}};
  QFile index(root+"/audiobooks/index.json");
  QVERIFY(index.open(QIODevice::WriteOnly));
  index.write(QJsonDocument(QJsonObject{{"old|writer",old}}).toJson());index.close();
  FakeEngine engine;engine.manifests[A]=FakeEngine::mixed();engine.autoComplete=true;
  QNetworkAccessManager nam;AudiobookDownloader books(&nam,nullptr);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished);
  books.downloadAudiobook("old|writer",A,"Old","Writer");
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
  const QString modern="old|writer#src:"+A;
  QCOMPARE(books.recordingsForWork("old|writer").size(),2);
  QCOMPARE(books.resolveRecordingKey("old|writer"),QString());
  QVERIFY(books.setDefaultRecording("old|writer","old|writer"));
  QCOMPARE(books.resolveRecordingKey("old|writer"),"old|writer");
  QVERIFY(books.markRecordingPlayed(modern));
  QCOMPARE(books.resolveRecordingKey("old|writer"),"old|writer");
  QVERIFY(books.deleteAudiobook("old|writer").value("success").toBool());
  QCOMPARE(books.resolveRecordingKey("old|writer"),modern);
  QCOMPARE(books.localFiles(modern).size(),2);
 }
 void bundleNeverAttachesToWork() {
  FakeEngine engine;engine.manifests[C]=FakeEngine::mixed();engine.autoComplete=true;
  QTemporaryDir root(evidence+"/bundle-XXXXXX");QVERIFY(root.isValid());
  QNetworkAccessManager nam;AudioPairingStore pairing(root.path()+"/pairs.ini");
  AudiobookDownloader books(&nam,nullptr);books.setPairing(&pairing);
  books.setTorrentTransport(new AudiobookTorrentTransport(&engine,{300,300,5}));
  QSignalSpy done(&books,&AudiobookDownloader::finished);
  books.downloadRecording({{"pairKey","one|author"},{"infoHash",C},{"title","Books 1-4"},
                           {"author","Author"},{"bookId","reader-id"},{"workId","work-456"},{"bundle",true},
                           {"sourceKind","abb"},{"sourceRef","bundle-post"}});
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,1000);
  const QString key="bundle#src:"+C;
  QCOMPARE(done[0][0].toString(),key);
  QCOMPARE(books.localFiles(key).size(),2);
  QCOMPARE(books.recordingsForWork("one|author").size(),0);
  QCOMPARE(books.downloadedAudiobooks()[0].toMap().value("bundle").toBool(),true);
  QVERIFY(!books.downloadedAudiobooks()[0].toMap().contains("workId"));
  AudiobookDownloader restarted(&nam,nullptr);
  QCOMPARE(restarted.downloadedAudiobooks().size(),1);
  QVERIFY(!restarted.downloadedAudiobooks()[0].toMap().contains("workId"));
  QVERIFY(pairing.getPairing("reader-id").isEmpty());
 }
 void librivoxPersistsSortedEditionWithAbbDisabled() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  connect(&server, &QTcpServer::newConnection, &server, [&] {
   auto socket = server.nextPendingConnection();
   connect(socket, &QTcpSocket::readyRead, socket, [socket] {
    const auto request = socket->readAll();
    const auto data = request.contains("chapter_02.mp3") ? QByteArray("ID3second") : QByteArray("ID3tenth");
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: "
                  + QByteArray::number(data.size()) + "\r\nConnection: close\r\n\r\n" + data);
    socket->disconnectFromHost();
   });
   connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  LocalArchiveNetwork network(server.serverPort());
  AudiobookDownloader downloader(&network, nullptr);
  downloader.setSourceEnabledCheck([] { return false; });
  QSignalSpy finished(&downloader, &AudiobookDownloader::finished);
  const QString key = "pride and prejudice|jane austen#librivox:1480";
  const QVariantList chapters {
   QVariantMap{{"number", 10}, {"title", "Chapter 10"}, {"url", "https://www.archive.org/download/fixture/chapter_10.mp3"}},
   QVariantMap{{"number", 2}, {"title", "Chapter 02"}, {"url", "https://www.archive.org/download/fixture/chapter_02.mp3"}}
  };
  downloader.downloadRecording({
   {"pairKey", "pride and prejudice|jane austen"}, {"recordingKey", key},
   {"title", "Pride and Prejudice (version 3)"}, {"author", "Jane Austen"},
   {"sourceKind", "librivox"}, {"sourceRef", "1480"}, {"chapters", chapters},
   {"narrators", QStringList{"Karen Savage"}}
  });
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 1500);
  QCOMPARE(finished.first().first().toString(), key);
  QVERIFY(downloader.isDownloaded(key));
  const auto files = downloader.localFiles(key);
  QCOMPARE(files.size(), 2);
  QFile first(files[0]), second(files[1]);
  QVERIFY(first.open(QIODevice::ReadOnly));
  QVERIFY(second.open(QIODevice::ReadOnly));
  QCOMPARE(first.readAll(), QByteArray("ID3second"));
  QCOMPARE(second.readAll(), QByteArray("ID3tenth"));
  AudiobookDownloader restarted(&network, nullptr);
  QCOMPARE(restarted.localFiles(key), files);
  const auto indexed = restarted.downloadedAudiobooks().first().toMap();
  QCOMPARE(indexed.value("sourceRef").toString(), QString("1480"));
  QCOMPARE(indexed.value("narrators").toStringList(), QStringList{"Karen Savage"});
 }

 void librivoxRetriesTransientFailureAndFollowsArchiveRedirect() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  int requests = 0;
  connect(&server, &QTcpServer::newConnection, &server, [&] {
   auto socket = server.nextPendingConnection();
   connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests] {
    socket->readAll();
    ++requests;
    if (requests == 1)
     socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    else if (requests == 2)
     socket->write("HTTP/1.1 302 Found\r\nLocation: https://dn-test.archive.org/0/items/fixture/chapter_01.mp3\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    else
     socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 8\r\nConnection: close\r\n\r\nID3retry");
    socket->disconnectFromHost();
   });
   connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  LocalArchiveNetwork network(server.serverPort());
  AudiobookDownloader downloader(&network, nullptr);
  QSignalSpy finished(&downloader, &AudiobookDownloader::finished);
  downloader.downloadRecording(libriVoxRequest());
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
  QCOMPARE(requests, 3);
  QCOMPARE(downloader.downloadedAudiobooks().size(), 1);
 }

 void librivoxRejectsUntrustedRedirect() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  int requests = 0;
  connect(&server, &QTcpServer::newConnection, &server, [&] {
   auto socket = server.nextPendingConnection();
   connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests] {
    socket->readAll();
    ++requests;
    socket->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1/metadata.mp3\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    socket->disconnectFromHost();
   });
   connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  LocalArchiveNetwork network(server.serverPort());
  AudiobookDownloader downloader(&network, nullptr);
  QSignalSpy failure(&downloader, &AudiobookDownloader::failed);
  downloader.downloadRecording(libriVoxRequest());
  QTRY_COMPARE_WITH_TIMEOUT(failure.size(), 1, 1500);
  QCOMPARE(requests, 1);
  QVERIFY(downloader.downloadedAudiobooks().isEmpty());
 }

 void librivoxCancellationRetiresPartialRecording() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  LocalArchiveNetwork network(server.serverPort());
  AudiobookDownloader downloader(&network, nullptr);
  QSignalSpy removed(&downloader, &AudiobookDownloader::removed);
  QSignalSpy finished(&downloader, &AudiobookDownloader::finished);
  downloader.downloadRecording(libriVoxRequest());
  QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1500);
  const QString key = libriVoxRequest().value("recordingKey").toString();
  downloader.cancelDownload(key);
  QCOMPARE(removed.size(), 1);
  QCOMPARE(removed.first().first().toString(), key);
  QCOMPARE(downloader.activeCount(), 0);
  QVERIFY(downloader.localFiles(key).isEmpty());
  QVERIFY(downloader.downloadedAudiobooks().isEmpty());
  QVERIFY(finished.isEmpty());
 }

 void librivoxSearchUsesPrefixAndExposesSeparateEditions() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  QByteArray requestLine;
  const QByteArray response("{\"books\":["
  "   {\"id\":\"1480\",\"title\":\"Pride and Prejudice (version 3)\",\"language\":\"English\",\"authors\":[{\"first_name\":\"Jane\",\"last_name\":\"Austen\"}],\"sections\":["
  "    {\"section_number\":\"10\",\"title\":\"Chapter 10\",\"listen_url\":\"https://www.archive.org/download/savage/10.mp3\",\"readers\":[{\"reader_id\":\"103\",\"display_name\":\"Karen Savage\"}]},"
  "    {\"section_number\":\"2\",\"title\":\"Chapter 02\",\"listen_url\":\"https://www.archive.org/download/savage/02.mp3\",\"readers\":[{\"reader_id\":\"103\",\"display_name\":\"Karen Savage\"}]}]},"
  "   {\"id\":\"4023\",\"title\":\"Pride and Prejudice (version 4)\",\"language\":\"English\",\"authors\":[{\"first_name\":\"Jane\",\"last_name\":\"Austen\"}],\"sections\":["
  "    {\"section_number\":\"1\",\"title\":\"Chapter 1\",\"listen_url\":\"https://www.archive.org/download/klett/01.mp3\",\"readers\":[{\"reader_id\":\"1259\",\"display_name\":\"Elizabeth Klett\"}]}]},"
  "   {\"id\":\"6415\",\"title\":\"Pride and Prejudice: A Play\",\"authors\":[{\"first_name\":\"Jane\",\"last_name\":\"Austen\"}],\"sections\":["
  "    {\"section_number\":\"1\",\"title\":\"Chapter 1\",\"listen_url\":\"https://www.archive.org/download/play/01.mp3\"}]},"
  "   {\"id\":\"2007\",\"title\":\"Pride and Prejudice\",\"authors\":[{\"first_name\":\"Jane\",\"last_name\":\"Austen\"}],\"sections\":["
  "    {\"section_number\":\"1\",\"title\":\"Chapter 1\",\"listen_url\":\"https://archive.org.example.com/download/forged/01.mp3\"}]}"
  "  ]}");
  connect(&server, &QTcpServer::newConnection, &server, [&] {
   auto socket = server.nextPendingConnection();
   connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
    requestLine = socket->readAll().split('\n').first();
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                  + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
    socket->disconnectFromHost();
   });
   connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  LocalArchiveNetwork network(server.serverPort());
  LibriVoxSearch catalogue(&network);
  QSignalSpy done(&catalogue, &LibriVoxSearch::searchFinished);
  catalogue.search("request", "Pride and Prejudice", "Jane Austen");
  QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
  QVERIFY(requestLine.startsWith("GET /api/feed/audiobooks/?title=%5EPride%20and%20Prejudice&"));
  QCOMPARE(done[0][0].toString(), QString("request"));
  QVERIFY(done[0][2].toString().isEmpty());
  const auto rows = done[0][1].toList();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].toMap().value("id").toString(), QString("1480"));
  QCOMPARE(rows[0].toMap().value("narrators").toStringList(), QStringList{"Karen Savage"});
  QCOMPARE(rows[0].toMap().value("chapters").toList()[0].toMap().value("number").toInt(), 2);
  QCOMPARE(rows[1].toMap().value("id").toString(), QString("4023"));
  QCOMPARE(rows[1].toMap().value("narrators").toStringList(), QStringList{"Elizabeth Klett"});
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
