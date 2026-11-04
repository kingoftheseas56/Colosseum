#include "account/CollectionPublicationController.h"
#include "NamedCollectionsStore.h"
#include "account/AccountHttpTransport.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class Transport final : public AccountTransport {
public:
    quint64 id=0; AccountTransportRequest request;
    using AccountTransport::AccountTransport;
    void send(quint64 value,const AccountTransportRequest &r) override { id=value; request=r; }
    void finish(const AccountTransportReply &reply) { emit finished(id,reply); }
};
class Checks : public QObject {
    Q_OBJECT
    const QString share = QString(43, QLatin1Char('a'));
    QJsonObject snapshot() {
        return {{"schema_version",1},{"name","A gift"},{"description","A note"},
            {"presentation",QJsonObject{{"preset","midnight"},{"dedication","For you"},{"signature","A friend"},{"cover_data",""}}},
            {"members",QJsonArray{QJsonObject{{"world","tankoban"},{"kind","manga"},{"source","mal"},{"source_id","13"},
                {"edition_id",""},{"canonical_id",""},{"canonical_source_id",""},{"album_source_id",""},{"album_browse_id",""},{"comic_post_ids",QJsonArray{}},{"title","One Piece"},{"creator",""},{"year",""},{"note","Personal"}}}}};
    }
    QJsonObject envelope() { return {{"share_id",share},{"kind","gift"},{"revision",1},{"snapshot",snapshot()},{"revoked",false}}; }
private slots:
    void transportDeleteBodyAndReplyBound() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0));
        const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        AccountHttpTransport transport(base); AccountClient client(&transport); client.setAccessToken("synthetic-token");
        QSignalSpy completed(&client,&AccountClient::completed);
        client.revokeCollectionPublication(share,{{"operation_id","test-operation"},{"expected_revision",1}});
        QTRY_VERIFY(server.hasPendingConnections());
        auto *socket=server.nextPendingConnection(); QVERIFY(socket);
        QByteArray request;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { request+=socket->readAll(); return request.contains("test-operation"); })(),2000);
        QVERIFY(request.startsWith("DELETE /v1/collections/publications/"));
        QVERIFY(request.contains("Authorization: Bearer synthetic-token"));
        QVERIFY(request.contains("\"expected_revision\":1"));
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
        socket->disconnectFromHost(); QTRY_COMPARE(completed.size(),1);
        client.openSharedCollection(share,"");
        QTRY_VERIFY(server.hasPendingConnections());
        auto *oversized=server.nextPendingConnection(); QVERIFY(oversized);
        QTRY_VERIFY(oversized->bytesAvailable()>0);
        const auto anon=oversized->readAll(); QVERIFY(!anon.contains("Authorization:"));
        const QByteArray body(2*1024*1024+10,'x');
        oversized->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n");
        oversized->write(body); oversized->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(),2,5000);
        const auto reply=qvariant_cast<AccountTransportReply>(completed.at(1).at(3));
        QVERIFY(reply.networkError); QCOMPARE(reply.errorCode,QString("response_too_large"));
    }
    void anonymousOpenAndLocalKeep() {
        QTemporaryDir dir; NamedCollectionsStore store(dir.filePath("collections.json"));
        Transport transport; AccountClient client(&transport); client.setAccessToken("must-not-leak");
        CollectionPublicationController controller(&client,QUrl("https://service.test")); controller.activate(&store,false);
        QVERIFY(!controller.publish("none",true));
        QVERIFY(controller.openLink("https://service.test/collections/s/"+share,"phrase"));
        QVERIFY(transport.request.bearerToken.isEmpty());
        QCOMPARE(transport.request.body.value("passphrase").toString(),QString("phrase"));
        QCOMPARE(transport.request.maxReplyBytes,qint64(2*1024*1024));
        transport.finish({200,envelope(),{},{},false});
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.received().value("kind").toString(),QString("gift"));
        QSignalSpy kept(&controller,&CollectionPublicationController::kept);
        QVERIFY(controller.keepReceivedGift()); QCOMPARE(kept.size(),1);
        const QString id=kept.takeFirst().at(0).toString(); QVERIFY(!id.isEmpty());
        QVERIFY(controller.keepReceivedGift()); QCOMPARE(store.collections().size(),1);
        QVERIFY(controller.makeOwnVersion(id,"My version")); QCOMPARE(store.collections().size(),2);
        QVERIFY(controller.makeOwnVersion(id,"My version")); QCOMPARE(store.collections().size(),2);
        NamedCollectionsStore restart(dir.filePath("collections.json")); QCOMPARE(restart.collections().size(),2);
        QVERIFY(!controller.openEditionFile(QUrl::fromLocalFile(dir.filePath("missing.json"))));
        QVERIFY(controller.received().isEmpty());
        QVERIFY(!controller.keepReceivedGift());
    }
    void profileSwitchDiscardsLateReply() {
        QTemporaryDir dir; NamedCollectionsStore a(dir.filePath("a.json")),b(dir.filePath("b.json"));
        Transport transport; AccountClient client(&transport);
        CollectionPublicationController controller(&client,QUrl("https://service.test")); controller.activate(&a,false);
        QVERIFY(controller.openLink("https://service.test/collections/s/"+share));
        controller.deactivate(); controller.activate(&b,false);
        transport.finish({200,envelope(),{},{},false}); QCoreApplication::processEvents();
        QVERIFY(controller.received().isEmpty()); QVERIFY(!controller.busy()); QVERIFY(b.collections().isEmpty());
    }
    void maliciousLinksAndEdition() {
        QTemporaryDir dir; NamedCollectionsStore store(dir.filePath("c.json"));
        Transport transport; AccountClient client(&transport);
        CollectionPublicationController controller(&client,QUrl("https://service.test")); controller.activate(&store,false);
        QVERIFY(!controller.openLink("https://evil.test/collections/s/"+share,"secret"));
        QVERIFY(!controller.openLink("https://service.test@evil.test/collections/s/"+share));
        QVERIFY(!controller.openLink("https://service.test/collections/s/../private"));
        QVERIFY(!controller.openLink("https://service.test/collections/s/"+share+"?password=x"));
        QCOMPARE(transport.id,quint64(0));
        auto e=envelope(); auto s=snapshot(); s.insert("credentials","secret"); e.insert("snapshot",s);
        QFile file(dir.filePath("bad.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(e).toJson()); file.close();
        QVERIFY(!controller.openEditionFile(QUrl::fromLocalFile(file.fileName()))); QVERIFY(controller.received().isEmpty());
        QVERIFY(store.collections().isEmpty());
    }
    void sessionRotationAndExplicitRecovery() {
        QTemporaryDir dir; NamedCollectionsStore store(dir.filePath("c.json"));
        const QString id=store.createCollection("A collection");
        Transport transport; AccountClient client(&transport);client.setAccessToken("old");
        CollectionPublicationController controller(&client,QUrl("https://service.test"));controller.activate(&store,true);
        QVERIFY(controller.publish(id,false,"first phrase"));
        const auto oldOperation=transport.request.body.value("operation_id").toString();
        transport.finish({0,{},"network","Interrupted",true});QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.retryResetAvailable());
        QVERIFY(controller.refresh());transport.finish({200,{{"publications",QJsonArray{}}},{},{},false});QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.retryResetAvailable()); // inspecting links does not lose recovery
        QVERIFY(controller.startNewPublicationRequest()); QVERIFY(!controller.retryResetAvailable());
        QVERIFY(controller.publish(id,false,"different phrase"));
        QVERIFY(transport.request.body.value("operation_id").toString()!=oldOperation);
        client.setAccessToken("rotated");
        auto receipt=envelope();receipt.insert("kind","collection");receipt.insert("snapshot",transport.request.body.value("snapshot"));
        transport.finish({201,receipt,{},{},false});QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.error().isEmpty());QVERIFY(controller.publications().isEmpty());
    }
    void durableRetryAndOwnershipReceipt() {
        QTemporaryDir dir; NamedCollectionsStore store(dir.filePath("c.json"));
        const QString id=store.createCollection("Collection"); QVERIFY(!id.isEmpty());
        Transport transport; AccountClient client(&transport);client.setAccessToken("token");
        CollectionPublicationController controller(&client,QUrl("https://service.test"));controller.activate(&store,true);
        QVERIFY(controller.publish(id,false,"phrase"));
        const QString op=transport.request.body.value("operation_id").toString(); QVERIFY(!op.isEmpty());
        QCOMPARE(transport.request.bearerToken,QByteArray("token"));
        transport.finish({0,{},"network","Interrupted",true});QTRY_VERIFY(!controller.busy());
        {
            CollectionPublicationController retry(&client,QUrl("https://service.test"));retry.activate(&store,true);
            QVERIFY(retry.publish(id,false,"phrase")); QCOMPARE(transport.request.body.value("operation_id").toString(),op);
            transport.finish({0,{},"network","Still interrupted",true}); QTRY_VERIFY(!retry.busy());
        }
        QVERIFY(controller.publish(id,false,"phrase")); QCOMPARE(transport.request.body.value("operation_id").toString(),op);
        auto receipt=envelope();receipt.insert("kind","collection");receipt.insert("snapshot",transport.request.body.value("snapshot"));
        transport.finish({201,receipt,{},{},false}); QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.publications().size(),1);
        QCOMPARE(controller.publications().first().toMap().value("source_collection_id").toString(),id);
        QVERIFY(!controller.publishChanges(share,1,"wrong",""));
        QVERIFY(controller.publishChanges(share,1,id,"phrase"));
        QCOMPARE(transport.request.method,QByteArray("PUT"));
        transport.finish({409,{},"revision_conflict","Refresh and retry",false});QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.error().isEmpty());
        QFile journal(store.filePath()+".publication-operations.json"); QVERIFY(journal.open(QIODevice::ReadOnly));
        auto bytes=journal.readAll(); journal.close(); QVERIFY(!bytes.contains("phrase")); QVERIFY(!bytes.contains("token"));
        CollectionPublicationController restart(&client,QUrl("https://service.test"));restart.activate(&store,true);
        QVERIFY(restart.publish(id,false,"phrase"));QVERIFY(transport.request.body.value("operation_id").toString()!=op);
    }
};
QTEST_MAIN(Checks)
#include "controller_test.moc"
