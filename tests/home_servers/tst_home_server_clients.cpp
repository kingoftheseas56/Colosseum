#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QTimer>
#include <QLockFile>
#include "HomeServerClient.h"

class ReplayServer : public QTcpServer {
public:
    struct Reply { int status = 200; QByteArray body = "{}"; QByteArray location; int delay = 0; };
    QList<Reply> replies;
    QList<QByteArray> requests;
    ReplayServer() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                auto data = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, data] {
                    *data += socket->readAll();
                    const auto end = data->indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("answered").toBool()) return;
                    qsizetype length = 0;
                    for (const auto &line : data->left(end).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (data->size() < end + 4 + length) return;
                    socket->setProperty("answered", true);
                    requests.append(*data);
                    const Reply reply = replies.isEmpty() ? Reply{} : replies.takeFirst();
                    QTimer::singleShot(reply.delay, socket, [socket, reply] {
                        if (socket->state() != QAbstractSocket::ConnectedState) return;
                        QByteArray wire = "HTTP/1.1 " + QByteArray::number(reply.status) + " Replay\r\nContent-Type: application/json\r\nConnection: close\r\n";
                        if (!reply.location.isEmpty()) wire += "Location: " + reply.location + "\r\n";
                        wire += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n\r\n" + reply.body;
                        socket->write(wire); socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl origin() const { return QUrl("http://127.0.0.1:" + QString::number(serverPort())); }
    static QByteArray fixture(const char *name) {
        QFile file(QStringLiteral(HOME_SERVER_FIXTURE_DIR) + '/' + QString::fromLatin1(name));
        if (!file.open(QIODevice::ReadOnly)) return {};
        auto bytes = file.readAll();
        if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
        return bytes;
    }
};
class HomeServerClientsTest : public QObject {
 Q_OBJECT
private slots:
 void normalization() {
  QCOMPARE(HomeServerClient::normalizeAddress("user:pass@example.com:8096/base/?key=secret#x").toString(), QString("http://example.com:8096/base"));
  QCOMPARE(HomeServerClient::normalizeAddress("https://example.com/a%2Fb/").toEncoded(), QByteArray("https://example.com/a%2Fb"));
  QVERIFY(HomeServerClient::normalizeAddress("file:///tmp").isEmpty());
  QVERIFY(HomeServerClient::normalizeAddress("https://bad host/").isEmpty());
  QCOMPARE(HomeServerClient::relativeUrl(QUrl("https://example.com/base"), "/Users/AuthenticateByName").toString(), QString("https://example.com/base/Users/AuthenticateByName"));
  QVERIFY(HomeServerClient::relativeUrl(QUrl("https://example.com/base"), "../escape").isEmpty());
  QVERIFY(HomeServerClient::relativeUrl(QUrl("https://example.com/base"), "%2e%2e/escape").isEmpty());
  QVERIFY(HomeServerClient::relativeUrl(QUrl("https://example.com/base"), "https://elsewhere.test").isEmpty());
  QCOMPARE(HomeServerClient::relativeUrl(QUrl("https://example.com/base"), "/Items/a%2Fb").toEncoded(), QByteArray("https://example.com/base/Items/a%2Fb"));
 }
 void stableIdentityAndFailClosed() {
  QTemporaryDir dir;
  HomeServerDeviceIdentity first(dir.path()); QVERIFY(first.isReady());
  HomeServerDeviceIdentity restart(dir.path()); QCOMPARE(restart.installationId(), first.installationId());
  HomeServerClient profileA(nullptr, dir.path()), profileB(nullptr, dir.path());
  QCOMPARE(profileA.installationId(), profileB.installationId());
  QFile file(dir.filePath("installation-id")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("corrupt"); file.close();
  HomeServerClient corrupt(nullptr, dir.path()); QVERIFY(!corrupt.isReady());
  QTemporaryDir blocked; QFile blocker(blocked.filePath("file")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
  HomeServerDeviceIdentity failed(blocker.fileName() + "/nested"); QVERIFY(!failed.isReady());
 }
 void identityLockAndReadOnlyFile() {
  QTemporaryDir dir; QLockFile lock(dir.filePath("installation-id.lock")); QVERIFY(lock.tryLock(0));
  HomeServerDeviceIdentity locked(dir.path()); QVERIFY(!locked.isReady()); QCOMPARE(locked.error(),QString("Installation identity is locked"));
  lock.unlock(); HomeServerDeviceIdentity first(dir.path()); QVERIFY(first.isReady());
  const auto path=dir.filePath("installation-id"); const auto permissions=QFile::permissions(path);
  QVERIFY(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::ReadUser));
  HomeServerDeviceIdentity readOnly(dir.path());
  QVERIFY(QFile::setPermissions(path,permissions));
  QVERIFY(!readOnly.isReady()); QCOMPARE(readOnly.error(),QString("Installation identity is not writable"));
 }
 void embyTokenHeaders() {
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path());
  const auto headers=client.headers("emby","REPLAY-TOKEN");
  QVERIFY(headers.value("Authorization").startsWith("Emby Client="));
  QCOMPARE(headers.value("X-Emby-Token"),QByteArray("REPLAY-TOKEN"));
  QVERIFY(!headers.value("Authorization").contains("REPLAY-TOKEN"));
 }
 void authenticate_data() {
  QTest::addColumn<QString>("provider"); QTest::addColumn<QByteArray>("fixture"); QTest::addColumn<QByteArray>("header");
  QTest::newRow("jellyfin") << QString("jellyfin") << QByteArray("jellyfin-auth.json") << QByteArray("Authorization: MediaBrowser");
  QTest::newRow("emby") << QString("emby") << QByteArray("emby-auth.json") << QByteArray("Authorization: Emby");
 }
 void authenticate() {
  QFETCH(QString,provider); QFETCH(QByteArray,fixture); QFETCH(QByteArray,header);
  ReplayServer server; server.replies.append({200, ReplayServer::fixture(fixture.constData())});
  QTemporaryDir dir; HomeServerClient client(nullptr, dir.path());
  bool done=false; HomeServerClient::Response response;
  client.authenticate(provider, server.origin().toString()+"/proxy/", "Replay User", "not-real-password", true, [&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY2(response.ok(), qPrintable(response.error));
  QCOMPARE(server.requests.size(),1); const auto request=server.requests.first();
  QVERIFY(request.startsWith("POST /proxy/Users/AuthenticateByName HTTP/1.1"));
  QVERIFY(request.contains(header)); QVERIFY(request.contains("Client=\"Colosseum\""));
  QVERIFY(request.contains("\"Pw\":\"not-real-password\"")); QVERIFY(request.contains("\"Username\":\"Replay User\""));
  QVERIFY(!response.body.toObject().value("AccessToken").toString().isEmpty());
 }
 void consentAndUnauthorized() {
  ReplayServer server; QTemporaryDir dir; HomeServerClient client(nullptr,dir.path());
  bool done=false; HomeServerClient::Response response;
  client.authenticate("jellyfin",server.origin().toString(),"user","secret",false,[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(!response.ok()); QCOMPARE(server.requests.size(),0);
  server.replies.append({401,"{\"error\":\"secret must not echo\"}"}); done=false;
  client.authenticate("jellyfin",server.origin().toString(),"user","secret",true,[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(response.expired()); QCOMPARE(server.requests.size(),1); QVERIFY(response.body.isNull()); QVERIFY(!response.error.contains("secret"));
 }
 void plexFlow() {
  ReplayServer server; QTemporaryDir dir; HomeServerClient client(nullptr,dir.path(),server.origin());
  bool done=false; HomeServerClient::Response response;
  auto callback=[&](auto r){response=r;done=true;};
  server.replies.append({200,ReplayServer::fixture("plex-pin.json")}); client.createPlexPin(callback);
  QTRY_VERIFY(done); QVERIFY(response.ok()); QCOMPARE(response.body.toObject().value("code").toString(),QString("ABCD"));
  QVERIFY(server.requests.last().startsWith("POST /api/v2/pins?strong=false ")); QVERIFY(server.requests.last().contains("X-Plex-Client-Identifier: "+client.installationId().toLatin1()));
  done=false; server.replies.append({200,ReplayServer::fixture("plex-poll.json")}); client.pollPlexPin(140,"ABCD",callback);
  QTRY_VERIFY(done); QVERIFY(response.ok()); QVERIFY(server.requests.last().startsWith("GET /api/v2/pins/140?code=ABCD "));
  done=false; server.replies.append({200,ReplayServer::fixture("plex-resources.json")}); client.discoverPlexServers("REPLAY-PLEX-ACCOUNT-TOKEN",callback);
  QTRY_VERIFY(done); QVERIFY(response.ok()); QCOMPARE(response.body.toArray().size(),1);
  const auto resource=response.body.toArray().first().toObject(); QCOMPARE(resource.value("token").toString(),QString("REPLAY-PLEX-SERVER-TOKEN"));
  const auto endpoints=resource.value("endpoints").toArray(); QCOMPARE(endpoints.size(),3); QVERIFY(endpoints.first().toObject().value("secure").toBool()); QVERIFY(endpoints.last().toObject().value("relay").toBool());
  QVERIFY(server.requests.last().contains("includeHttps=1&includeRelay=1&includeIPv6=1"));
  done=false; server.replies.append({200,ReplayServer::fixture("plex-identity.json")}); client.verifyPlexServer(server.origin(),"REPLAY-PLEX-SERVER-TOKEN",true,callback);
  QTRY_VERIFY(done); QVERIFY(response.ok()); QCOMPARE(response.body.toObject().value("id").toString(),QString("plex-machine"));
  QVERIFY(server.requests.last().startsWith("GET /identity ")); QVERIFY(server.requests.last().contains("X-Plex-Token: REPLAY-PLEX-SERVER-TOKEN"));
 }
 void crossOriginRedirectNeverLeaks() {
  ReplayServer first,other; first.replies.append({307,"{}",other.origin().toEncoded()+"/stolen"});
  HomeServerTransport transport; bool done=false; HomeServerTransport::Response response;
  transport.send(first.origin(),"POST",{{"Authorization","SECRET"},{"Content-Type","application/json"}},"{\"password\":\"SECRET\"}",[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(!response.ok()); QCOMPARE(first.requests.size(),1); QCOMPARE(other.requests.size(),0);
 }
 void deadlineAndTeardown() {
  ReplayServer server; server.replies.append({200,"{}",{},250});
  HomeServerTransport transport; bool done=false; HomeServerTransport::Response response;
  transport.send(server.origin(),"GET",{}, {},[&](auto r){response=r;done=true;},40);
  QTRY_VERIFY(done); QCOMPARE(response.error,QString("Request timed out"));
  bool afterDestruction=false;
  auto *temporary=new HomeServerTransport;
  server.replies.append({200,"{}",{},250});
  temporary->send(server.origin(),"GET",{}, {},[&](auto){afterDestruction=true;}); delete temporary;
  QTest::qWait(300); QVERIFY(!afterDestruction);
 }
 void rejectInjection() {
  ReplayServer server; HomeServerTransport transport; bool done=false;
  transport.send(server.origin(),"GET",{{"X-Token","secret\r\nInjected: yes"}}, {},[&](auto r){QVERIFY(!r.ok());done=true;});
  QTRY_VERIFY(done); QCOMPARE(server.requests.size(),0);
 }
 void malformedAuthAndIdentityFailure() {
  ReplayServer server; server.replies.append({200,"{\"AccessToken\":\"REPLAY\"}"});
  QTemporaryDir dir; HomeServerClient client(nullptr,dir.path()); bool done=false; HomeServerClient::Response response;
  client.authenticate("jellyfin",server.origin().toString(),"user","fake",true,[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(!response.ok()); QVERIFY(response.body.isNull());
  QFile file(dir.filePath("installation-id")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("bad"); file.close();
  HomeServerClient corrupt(nullptr,dir.path()); done=false;
  corrupt.authenticate("emby",server.origin().toString(),"user","fake",true,[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QCOMPARE(response.error,QString("Invalid installation identity")); QCOMPARE(server.requests.size(),1);
 }
 void sameOriginReplayAndMethodRewriteRejected() {
  ReplayServer server; server.replies.append({307,"{}","/moved"}); server.replies.append({200,"{\"ok\":true}"});
  HomeServerTransport transport; bool done=false; HomeServerTransport::Response response;
  transport.send(server.origin(),"POST",{{"Authorization","REPLAY"},{"Content-Type","application/json"}},"{\"x\":1}",[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(response.ok()); QCOMPARE(server.requests.size(),2);
  QVERIFY(server.requests.last().startsWith("POST /moved ")); QVERIFY(server.requests.last().contains("Authorization: REPLAY")); QVERIFY(server.requests.last().endsWith("{\"x\":1}"));
  server.replies.append({302,"{}","/unsafe"}); done=false;
  transport.send(server.origin(),"POST",{{"Content-Type","application/json"}},"{}",[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QVERIFY(!response.ok()); QCOMPARE(server.requests.size(),3);
 }
 void deadlineIncludesRedirects() {
  ReplayServer server; server.replies.append({307,"{}","/moved",500}); server.replies.append({200,"{}",{},1200});
  HomeServerTransport transport; bool done=false; HomeServerTransport::Response response;
  transport.send(server.origin(),"GET",{}, {},[&](auto r){response=r;done=true;},1500);
  QTRY_VERIFY(done); QCOMPARE(response.error,QString("Request timed out")); QCOMPARE(server.requests.size(),2);
 }
 void responseLimitAndCancel() {
  ReplayServer server; server.replies.append({200,QByteArray(32*1024*1024+1,'x')});
  HomeServerTransport transport; bool done=false; HomeServerTransport::Response response;
  transport.send(server.origin(),"GET",{}, {},[&](auto r){response=r;done=true;});
  QTRY_VERIFY(done); QCOMPARE(response.error,QString("Server response too large"));
  server.replies.append({200,"{}",{},500}); done=false;
  transport.send(server.origin(),"GET",{}, {},[&](auto r){response=r;done=true;}); transport.cancelAll();
  QVERIFY(done); QCOMPARE(response.error,QString("Request cancelled"));
 }
};
QTEST_GUILESS_MAIN(HomeServerClientsTest)
#include "tst_home_server_clients.moc"
