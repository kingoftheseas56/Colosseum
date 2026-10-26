#include "server1/discovery/TrackerNetwork.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>
#include <QtEndian>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>
using server1::discovery::TrackerNetwork;
void check(bool b, const char *m) {
  if (!b) {
    std::cerr << m << '\n';
    std::exit(1);
  }
}
bool until(std::function<bool()> predicate, int ms = 2000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) {
    QCoreApplication::processEvents();
    if (predicate())
      return true;
    QThread::msleep(1);
  }
  return predicate();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost, 0), "listen");
  QByteArray request;
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
    auto *s = server.nextPendingConnection();
    QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
      request += s->readAll();
      if (!request.contains("\r\n\r\n"))
        return;
      QByteArray body = "d8:intervali1800e5:peers6:";
      body += QByteArray::fromHex("7f0000011ae1");
      body += "6:peers636:";
      body += QByteArray::fromHex("00000000000000000000ffffc00002011ae1");
      body += QByteArray::fromHex("000100000002000000000003000400051ae2");
      body += "e";
      s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
               QByteArray::number(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body);
      s->disconnectFromHost();
    });
  });
  std::vector<std::string> peers;
  TrackerNetwork client(QString("http://127.0.0.1:%1/announce?passkey=x")
                            .arg(server.serverPort()),
                        QString(40, 'a'));
  client.onPeer = [&](auto p) { peers.push_back(p); };
  client.start();
  check(until([&] { return !peers.empty(); }),
        "HTTP acquisition must emit a real tracker peer");
  check(peers == std::vector<std::string>{"127.0.0.1:6881",
                                          "[::ffff:c000:201]:6881",
                                          "[1::2:0:0:3:4:5]:6882"},
        "compact IPv4 peer");
  check(request.contains("passkey=x&") && request.contains("event=started") &&
            request.contains("port=6881") &&
            request.contains("peer_id=01234567890123456789") &&
            request.contains("info_hash=%AA%AA"),
        "oracle announce query");
  client.close();
  QUdpSocket udp;
  check(udp.bind(QHostAddress::LocalHost, 0), "UDP fixture bind");
  QByteArray announce;
  int connects = 0;
  QObject::connect(&udp, &QUdpSocket::readyRead, &udp, [&] {
    while (udp.hasPendingDatagrams()) {
      QByteArray b;
      b.resize(udp.pendingDatagramSize());
      QHostAddress from;
      quint16 port;
      udp.readDatagram(b.data(), b.size(), &from, &port);
      auto action = qFromBigEndian<quint32>(b.constData() + 8);
      QByteArray reply;
      if (action == 0) {
        ++connects;
        check(b.left(8) == QByteArray::fromHex("0000041727101980"),
              "UDP protocol id");
        reply = QByteArray(4, 0) + b.mid(12, 4) +
                QByteArray::fromHex("1122334455667788");
      } else {
        announce = b;
        reply = QByteArray::fromHex("00000001") + b.mid(12, 4) +
                QByteArray::fromHex("0000070800000000000000007f0000021ae2");
      }
      udp.writeDatagram(reply, from, port);
    }
  });
  TrackerNetwork udpClient(
      QString("udp://127.0.0.1:%1/announce").arg(udp.localPort()),
      QString(40, 'b'));
  udpClient.onWarning = [](auto w) { std::cerr << w << "\n"; };
  std::vector<std::string> udpPeers;
  udpClient.onPeer = [&](auto p) { udpPeers.push_back(p); };
  udpClient.start();
  check(until([&] { return !udpPeers.empty(); }),
        "UDP connect/announce acquisition");
  check(connects == 1 && announce.size() == 98 &&
            announce.mid(16, 20) == QByteArray(20, char(0xbb)) &&
            announce.mid(36, 20) == "01234567890123456789" &&
            announce.mid(64, 8) == QByteArray(8, char(255)) &&
            qFromBigEndian<quint32>(announce.constData() + 80) == 2 &&
            qFromBigEndian<quint32>(announce.constData() + 92) == 50,
        "UDP oracle fields");
  check(udpPeers == std::vector<std::string>{"127.0.0.2:6882"},
        "UDP peer endpoint");
  udpClient.close();
  QTcpServer delayed;
  check(delayed.listen(QHostAddress::LocalHost, 0), "delayed bind");
  QTcpSocket *pending = nullptr;
  QObject::connect(&delayed, &QTcpServer::newConnection, &delayed,
                   [&] { pending = delayed.nextPendingConnection(); });
  TrackerNetwork cancelled(
      QString("http://127.0.0.1:%1/announce").arg(delayed.serverPort()),
      QString(40, 'c'));
  int late = 0;
  cancelled.onPeer = [&](auto) { ++late; };
  cancelled.start();
  check(until([&] { return pending != nullptr; }),
        "cancelled request in flight");
  cancelled.close();
  QCoreApplication::processEvents();
  check(late == 0, "no peer delivery after close");
  QTcpServer recurring;
  check(recurring.listen(QHostAddress::LocalHost, 0), "interval fixture bind");
  std::vector<QByteArray> recurringRequests;
  QObject::connect(&recurring, &QTcpServer::newConnection, &recurring, [&] {
    auto *s = recurring.nextPendingConnection();
    auto buffer = std::make_shared<QByteArray>();
    QObject::connect(s, &QTcpSocket::readyRead, s, [&, s, buffer] {
      *buffer += s->readAll();
      if (!buffer->contains("\r\n\r\n"))
        return;
      recurringRequests.push_back(*buffer);
      QByteArray body =
          recurringRequests.size() == 1
              ? "d8:intervali0e12:min intervali1e10:tracker id3:a be"
              : "d8:intervali1ee";
      s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
               QByteArray::number(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body);
      s->disconnectFromHost();
    });
  });
  TrackerNetwork repeated(
      QString("http://127.0.0.1:%1/announce").arg(recurring.serverPort()),
      QString(40, 'd'));
  repeated.start();
  check(until([&] { return recurringRequests.size() >= 3; }, 3500),
        "tracker response interval schedules updates");
  check(recurringRequests[1].contains("event=&") &&
            recurringRequests[1].contains("trackerid=a%20b") &&
            recurringRequests[2].contains("trackerid=a%20b"),
        "tracker id persists across responses omitting it");
  repeated.close();
  auto *selfDeleting = new TrackerNetwork(
      QString("http://127.0.0.1:%1/announce").arg(server.serverPort()),
      QString(40, 'a'));
  bool deleted = false;
  selfDeleting->onPeer = [&](auto) {
    delete selfDeleting;
    selfDeleting = nullptr;
    deleted = true;
  };
  selfDeleting->start();
  check(until([&] { return deleted; }), "peer callback can destroy tracker");
  auto *udpDeleting = new TrackerNetwork(
      QString("udp://127.0.0.1:%1/announce").arg(udp.localPort()),
      QString(40, 'b'));
  bool udpDeleted = false;
  udpDeleting->onPeer = [&](auto) {
    delete udpDeleting;
    udpDeleting = nullptr;
    udpDeleted = true;
  };
  udpDeleting->start();
  check(until([&] { return udpDeleted; }),
        "UDP peer callback can destroy tracker");
  QTcpServer warningServer;
  check(warningServer.listen(QHostAddress::LocalHost, 0),
        "warning fixture bind");
  QObject::connect(&warningServer, &QTcpServer::newConnection, &warningServer,
                   [&] {
                     auto *s = warningServer.nextPendingConnection();
                     QObject::connect(s, &QTcpSocket::readyRead, s, [s] {
                       s->readAll();
                       QByteArray body = "d15:warning message4:test5:peers6:";
                       body += QByteArray::fromHex("7f0000011ae1");
                       body += 'e';
                       s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                QByteArray::number(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body);
                       s->disconnectFromHost();
                     });
                   });
  auto *warningParent = new QObject;
  auto *warningDeleting = new TrackerNetwork(
      QString("http://127.0.0.1:%1/announce").arg(warningServer.serverPort()),
      QString(40, 'a'), warningParent);
  bool warningDeleted = false;
  int warnedPeers = 0;
  warningDeleting->onPeer = [&](auto) { ++warnedPeers; };
  warningDeleting->onWarning = [&](auto) {
    delete warningParent;
    warningParent = nullptr;
    warningDeleted = true;
  };
  warningDeleting->start();
  check(until([&] { return warningDeleted; }),
        "warning callback can destroy parent");
  check(warnedPeers == 0, "no peers after warning destruction");
  auto *dnsCancelled =
      new TrackerNetwork("udp://localhost:9/announce", QString(40, 'a'));
  dnsCancelled->start();
  delete dnsCancelled;
  QCoreApplication::processEvents();
  TrackerNetwork invalid("http://127.0.0.1/announce", "not a hash");
  int warnings = 0;
  invalid.onWarning = [&](auto) { ++warnings; };
  invalid.start();
  check(warnings == 1, "invalid hash warning");
  std::cout << "tracker network tests passed\n";
}
