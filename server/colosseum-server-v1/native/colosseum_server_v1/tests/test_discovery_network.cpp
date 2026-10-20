#include "server1/discovery/PeerSearch.h"
#include "server1/discovery/QtDiscoveryNetwork.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <cstdlib>
#include <iostream>
using namespace server1::discovery;
void check(bool b, const char *m) {
  if (!b) {
    std::cerr << m << '\n';
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTcpServer fixture;
  check(fixture.listen(QHostAddress::LocalHost, 0), "listen");
  int requests = 0;
  QObject::connect(&fixture, &QTcpServer::newConnection, &fixture, [&] {
    auto *s = fixture.nextPendingConnection();
    QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
      auto r = s->readAll();
      if (!r.contains("\r\n\r\n"))
        return;
      ++requests;
      QByteArray b = "d5:peers6:";
      b += QByteArray::fromHex("7f0000011ae1");
      b += 'e';
      s->write(
          "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(b.size()) +
          "\r\nConnection: close\r\n\r\n" + b);
      s->disconnectFromHost();
    });
  });
  auto network = std::make_shared<QtDiscoveryNetwork>();
  PeerSearch search({"tracker:http://127.0.0.1:" +
                     std::to_string(fixture.serverPort()) + "/announce"},
                    40, 200, 0, {}, network, std::string(40, 'a'));
  QElapsedTimer time;
  time.start();
  while (time.elapsed() < 2000 && search.peerAdds().empty()) {
    QCoreApplication::processEvents();
    search.tick(time.elapsed());
    QThread::msleep(1);
  }
  check(search.takePeerAdds() == std::vector<std::string>{"127.0.0.1:6881"},
        "real tracker discovery reaches peer queue");
  check(search.stats()[0].numFound == 1 &&
            search.stats()[0].numFoundUniq == 1 &&
            search.stats()[0].numRequests == 1,
        "live discovery statistics");
  search.tick(30000);
  time.restart();
  while (time.elapsed() < 2000 && search.stats()[0].numFound < 2) {
    QCoreApplication::processEvents();
    search.tick(30001 + time.elapsed());
    QThread::msleep(1);
  }
  check(requests == 2 && search.stats()[0].numRequests == 2 &&
            search.stats()[0].numFound == 2 &&
            search.stats()[0].numFoundUniq == 1,
        "30-second source rerun and cross-client uniqueness");
  search.close();
  network->tracker(0,
                   "http://127.0.0.1:" + std::to_string(fixture.serverPort()),
                   std::string(40, 'a'));
  QCoreApplication::processEvents();
  check(requests == 2 && network->takePeers().empty(),
        "closed network ignores new work");
  std::cout << "discovery network integration passed\n";
}
