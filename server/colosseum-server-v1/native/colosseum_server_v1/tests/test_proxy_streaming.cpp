#include "server1/http/HttpContract.h"
#include "server1/network/ProxyService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <functional>
#include <iostream>
static void check(bool x, const char *s) {
  if (!x) {
    std::cerr << s << '\n';
    std::exit(1);
  }
}
static bool until(std::function<bool()> f) {
  QElapsedTimer t;
  t.start();
  while (!f() && t.elapsed() < 3000)
    QCoreApplication::processEvents();
  return f();
}
class TlsServer : public QTcpServer {
public:
  QSslCertificate cert;
  QSslKey key;
  void incomingConnection(qintptr descriptor) override {
    auto *s = new QSslSocket(this);
    s->setSocketDescriptor(descriptor);
    s->setLocalCertificate(cert);
    s->setPrivateKey(key);
    QObject::connect(s, &QSslSocket::encrypted, s, [this, s] {
      addPendingConnection(s);
      emit newConnection();
    });
    s->startServerEncryption();
  }
};
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc > 1 && QByteArray(argv[1]) == "--serve") {
    if (qEnvironmentVariableIsSet("PROXY_TEST_CA")) {
      QFile file(qEnvironmentVariable("PROXY_TEST_CA"));
      check(file.open(QIODevice::ReadOnly), "CA open");
      auto config = QSslConfiguration::defaultConfiguration();
      auto ca = config.caCertificates();
      ca.append(QSslCertificate::fromData(file.readAll()));
      config.setCaCertificates(ca);
      QSslConfiguration::setDefaultConfiguration(config);
    }
    auto router = server1_http_router_create();
    check(server1::network::registerProxyRoutes(router) == 1, "register");
    auto server = server1_http_server_create(router, 65536);
    check(server1_http_server_listen(server, 0), "server listen");
    std::cout << server1_http_server_port(server) << std::endl;
    return app.exec();
  }
  QTemporaryDir fixture;
  auto keyPath = fixture.filePath("key.pem"),
       certPath = fixture.filePath("cert.pem");
  QProcess openssl;
  openssl.start("openssl",
                {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout",
                 keyPath, "-out", certPath, "-days", "1", "-subj",
                 "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1"});
  check(openssl.waitForFinished() && openssl.exitCode() == 0,
        "fixture certificate");
  QFile certFile(certPath), keyFile(keyPath);
  check(certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly),
        "fixture files");
  auto certs = QSslCertificate::fromData(certFile.readAll());
  auto config = QSslConfiguration::defaultConfiguration();
  auto ca = config.caCertificates();
  ca.append(certs);
  config.setCaCertificates(ca);
  QSslConfiguration::setDefaultConfiguration(config);
  TlsServer upstream;
  upstream.cert = certs.first();
  upstream.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
  check(upstream.listen(QHostAddress::LocalHost), "listen");
  QByteArray seen;
  int malformed = 0;
  QObject::connect(&upstream, &QTcpServer::newConnection, [&] {
    auto s = upstream.nextPendingConnection();
    if (!s)
      return;
    QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
      seen += s->readAll();
      if (!seen.contains("\r\n\r\n"))
        return;
      if (malformed == 2) {
        auto compressed =
            QByteArray::fromHex("789c53768d08f1350ee5d2cfcf4bd52b29e60200");
        s->write("HTTP/1.1 200 OK\r\nContent-Type: "
                 "application/vnd.apple.mpegurl\r\nContent-Encoding: "
                 "deflate\r\nContent-Length: " +
                 QByteArray::number(compressed.size()) + "\r\n\r\n" +
                 compressed);
        return;
      }
      if (malformed) {
        s->write("HTTP/1.1 200 OK\r\nContent-Type: "
                 "application/vnd.apple.mpegurl\r\nContent-Encoding: "
                 "gzip\r\nContent-Length: 3\r\n\r\nbad");
        return;
      }
      s->write("HTTP/1.1 206 Partial Content\r\nContent-Type: "
               "application/octet-stream\r\nContent-Length: "
               "6\r\nContent-Range: bytes 1-6/9\r\nX-Drop: secret\r\n\r\nabc");
      QTimer::singleShot(40, s, [s] { s->write("def"); });
    });
  });
  auto router = server1_http_router_create();
  check(server1::network::registerProxyRoutes(router) == 1,
        "proxy route registration missing");
  QByteArray target = "/proxy/d=https%3A%2F%2F127.0.0.1%3A" +
                      QByteArray::number(upstream.serverPort()) +
                      "&h=X-Test%3Ayes&r=X-Out%3Aok/video?x=1";
  auto r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  check(server1_http_response_is_deferred(r), "metadata must wait");
  check(until([&] { return !server1_http_response_is_deferred(r); }),
        "metadata timed out");
  check(server1_http_response_status(r) == 206, "upstream status");
  check(QByteArray(server1_http_response_header(r, "X-Out")) == "ok",
        "response override");
  check(QByteArray(server1_http_response_header(r, "X-Drop")).isEmpty(),
        "allowlist");
  QByteArray body;
  check(until([&] {
          char b[2];
          auto n = server1_http_response_stream_read(r, b, 2);
          if (n > 0)
            body.append(b, n);
          return n == 0;
        }),
        "body timed out");
  check(body == "abcdef", "binary stream body");
  check(seen.startsWith("GET /video?x=1 HTTP/1.1"), "upstream URL");
  check(seen.toLower().contains("x-test: yes"), "request header override");
  server1_http_response_destroy(r);
  // Retiring a response before metadata must cancel the queued producer.
  seen.clear();
  r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  server1_http_response_destroy(r);
  QCoreApplication::processEvents();
  check(seen.isEmpty(), "cancel before request");
  // A connection may destroy the owner from the deferred-metadata wake itself.
  seen.clear();
  r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  bool retired = false;
  struct Retire {
    void *response;
    bool *retired;
  };
  auto *retire = new Retire{r, &retired};
  check(server1_http_response_bind_deferred(
            r,
            [](void *p) {
              auto *v = static_cast<Retire *>(p);
              *v->retired = true;
              server1_http_response_destroy(v->response);
            },
            retire, [](void *p) { delete static_cast<Retire *>(p); }),
        "bind deferred");
  check(until([&] { return retired; }), "retire within metadata wake");
  // Service destruction during a live response must terminate, never strand it.
  seen.clear();
  r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  check(until([&] { return !server1_http_response_is_deferred(r); }),
        "second metadata");
  server1_http_router_destroy(router);
  check(until([&] {
          char b[32];
          auto n = server1_http_response_stream_read(r, b, 32);
          return n == 0 || n == -1;
        }),
        "service destruction stranded stream");
  server1_http_response_destroy(r);
  router = server1_http_router_create();
  check(server1::network::registerProxyRoutes(router) == 1, "stall router");
  malformed = true;
  seen.clear();
  r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  check(until([&] { return !seen.isEmpty(); }),
        "stall request reached fixture");
  QElapsedTimer stall;
  stall.start();
  while (stall.elapsed() < 100)
    QCoreApplication::processEvents();
  check(server1_http_response_is_deferred(r),
        "malformed stream must remain deferred");
  server1_http_router_destroy(router);
  check(!server1_http_response_is_deferred(r),
        "service shutdown releases stalled response");
  server1_http_response_destroy(r);
  router = server1_http_router_create();
  check(server1::network::registerProxyRoutes(router) == 1,
        "partial stall router");
  malformed = 2;
  seen.clear();
  r = server1_http_router_dispatch(router, "GET", target.constData(), "");
  check(until([&] { return !server1_http_response_is_deferred(r); }),
        "partial stalled metadata");
  check(until([&] {
          char b[4096];
          return server1_http_response_stream_read(r, b, sizeof b) ==
                 SERVER1_HTTP_WOULD_BLOCK;
        }),
        "partial stream stalled");
  server1_http_router_destroy(router);
  check(until([&] {
          char b[4096];
          return server1_http_response_stream_read(r, b, sizeof b) == -1;
        }),
        "shutdown releases published stalled stream");
  server1_http_response_destroy(r);
  std::cout << "proxy streaming OK\n";
}
