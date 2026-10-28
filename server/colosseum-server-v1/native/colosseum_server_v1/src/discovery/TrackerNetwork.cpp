#include "server1/discovery/TrackerNetwork.h"
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>
#include <QtEndian>
#include <limits>
#include <stdexcept>
namespace server1::discovery {
namespace {
// Bounded decoding of untrusted tracker replies. Byte strings remain bytes.
class Decoder {
  QByteArray data_;
  qsizetype p_ = 0;
  int count_ = 0;
  QVariant parse(int depth) {
    if (depth > 32 || ++count_ > 100000 || p_ >= data_.size())
      throw std::runtime_error("invalid bencode");
    char c = data_[p_++];
    if (c == 'd') {
      QVariantMap m;
      while (p_ < data_.size() && data_[p_] != 'e') {
        auto k = parse(depth + 1);
        if (k.metaType().id() != QMetaType::QByteArray)
          throw std::runtime_error("invalid key");
        m.insert(QString::fromLatin1(k.toByteArray()), parse(depth + 1));
      }
      end();
      return m;
    }
    if (c == 'l') {
      QVariantList a;
      while (p_ < data_.size() && data_[p_] != 'e')
        a.push_back(parse(depth + 1));
      end();
      return a;
    }
    if (c == 'i') {
      auto e = data_.indexOf('e', p_);
      if (e < 0)
        throw std::runtime_error("invalid integer");
      bool ok;
      auto n = data_.mid(p_, e - p_).toLongLong(&ok);
      if (!ok)
        throw std::runtime_error("invalid integer");
      p_ = e + 1;
      return n;
    }
    --p_;
    auto colon = data_.indexOf(':', p_);
    if (colon < 0 || colon - p_ > 10)
      throw std::runtime_error("invalid string");
    bool ok;
    auto n = data_.mid(p_, colon - p_).toLongLong(&ok);
    if (!ok || n < 0 || n > data_.size() - colon - 1)
      throw std::runtime_error("invalid string length");
    p_ = colon + 1;
    auto s = data_.mid(p_, n);
    p_ += n;
    return s;
  }
  void end() {
    if (p_ >= data_.size() || data_[p_++] != 'e')
      throw std::runtime_error("missing end");
  }

public:
  explicit Decoder(QByteArray d) : data_(std::move(d)) {}
  QVariantMap decode() {
    auto v = parse(0);
    if (p_ != data_.size() || v.metaType().id() != QMetaType::QVariantMap)
      throw std::runtime_error("invalid response");
    return v.toMap();
  }
};
QByteArray number32(quint32 n) {
  QByteArray b(4, 0);
  qToBigEndian(n, b.data());
  return b;
}
QString endpoint(const QHostAddress &a, quint16 p) {
  return (a.protocol() == QAbstractSocket::IPv6Protocol
              ? "[" + a.toString() + "]"
              : a.toString()) +
         ":" + QString::number(p);
}
} // namespace
struct TrackerNetwork::State {
  TrackerNetwork *owner;
  QString url;
  QByteArray hash, trackerId;
  QNetworkAccessManager manager;
  QTimer interval;
  bool closed = false, started = false;
  State(TrackerNetwork *o, QString u, QString h)
      : owner(o), url(std::move(u)), hash(QByteArray::fromHex(h.toLatin1())),
        manager(o) {
    if (url.endsWith('/'))
      url.chop(1);
    if (h.size() != 40 || hash.size() != 20)
      hash.clear();
    interval.setInterval(1800000);
    QObject::connect(&interval, &QTimer::timeout, owner,
                     [this] { announce(false); });
  }
  bool warning(QString text) {
    if (closed)
      return false;
    const QPointer<TrackerNetwork> guard(owner);
    const auto callback = owner->onWarning;
    if (callback)
      callback(text.toStdString());
    return guard && !closed;
  }
  bool deliver(QString value) {
    if (closed)
      return false;
    const QPointer<TrackerNetwork> guard(owner);
    const auto callback = owner->onPeer;
    if (callback)
      callback(value.toStdString());
    return guard && !closed;
  }
  void setInterval(qint64 seconds) {
    if (seconds > 0 && seconds <= std::numeric_limits<int>::max() / 1000)
      interval.start(int(seconds * 1000));
  }
  bool compact(const QByteArray &bytes, int size) {
    if (bytes.size() % size) {
      return warning("invalid compact peer list");
    }
    for (qsizetype i = 0; i < bytes.size(); i += size) {
      QHostAddress a;
      if (size == 6)
        a.setAddress(qFromBigEndian<quint32>(bytes.constData() + i));
      else {
        // The pinned ipaddr.js toString compresses the first zero run,
        // even one group, rather than Qt's RFC5952 longest-run form.
        QStringList groups;
        int firstZero = -1, afterZero = -1;
        for (int group = 0; group < 8; ++group) {
          const auto value =
              qFromBigEndian<quint16>(bytes.constData() + i + group * 2);
          groups.push_back(QString::number(value, 16));
          if (!value && firstZero < 0)
            firstZero = group;
          if (firstZero >= 0 && afterZero < 0 && value)
            afterZero = group;
        }
        QString ip = groups.join(':');
        if (firstZero >= 0) {
          if (afterZero < 0)
            afterZero = 8;
          ip = groups.mid(0, firstZero).join(':') +
               "::" + groups.mid(afterZero).join(':');
        }
        if (!deliver("[" + ip + "]:" +
                     QString::number(
                         qFromBigEndian<quint16>(bytes.constData() + i + 16))))
          return false;
        continue;
      }
      if (!deliver(endpoint(
              a, qFromBigEndian<quint16>(bytes.constData() + i + size - 2))))
        return false;
    }
    return true;
  }
  bool peers(const QVariant &v, int size) {
    if (v.metaType().id() == QMetaType::QByteArray)
      return compact(v.toByteArray(), size);
    else if (v.metaType().id() == QMetaType::QVariantList)
      for (auto p : v.toList()) {
        auto m = p.toMap();
        QString ip = QString::fromLatin1(m.value("ip").toByteArray());
        if (size == 18 && ip.contains(':') && !ip.startsWith('['))
          ip = "[" + ip + "]";
        if (!deliver(ip + ":" + m.value("port").toString()))
          return false;
      }
    return true;
  }
  void announce(bool first) {
    if (closed)
      return;
    if (hash.size() != 20) {
      warning("invalid info hash");
      return;
    }
    QUrl target(url);
    if (target.scheme() == "udp") {
      udp(target, first);
      return;
    }
    if (target.scheme() != "http" && target.scheme() != "https") {
      warning("unsupported tracker protocol");
      return;
    }
    QByteArray raw = target.toEncoded();
    raw += (target.hasQuery() ? "&" : "?");
    raw += "numwant=50&uploaded=0&downloaded=0&event=";
    if (first)
      raw += "started";
    raw += "&compact=1&info_hash=" + hash.toPercentEncoding() +
           "&peer_id=01234567890123456789&port=6881";
    if (!trackerId.isEmpty())
      raw += "&trackerid=" + trackerId.toPercentEncoding();
    QNetworkRequest request(QUrl::fromEncoded(raw));
    request.setMaximumRedirectsAllowed(10);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *r = manager.get(request);
    r->setReadBufferSize(1024 * 1024 + 1);
    QObject::connect(r, &QNetworkReply::readyRead, owner, [this, r] {
      if (r->bytesAvailable() > 1024 * 1024) {
        if (!warning("tracker response too large"))
          return;
        r->abort();
      }
    });
    QObject::connect(r, &QNetworkReply::finished, owner, [this, r] {
      auto body = r->readAll();
      auto status =
          r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      auto error = r->error();
      r->deleteLater();
      if (closed)
        return;
      if (error != QNetworkReply::NoError || status != 200) {
        warning("tracker HTTP request failed");
        return;
      }
      try {
        auto m = Decoder(body).decode();
        if (m.contains("failure reason")) {
          warning(QString::fromLatin1(m["failure reason"].toByteArray()));
          return;
        }
        if (m.contains("warning message") &&
            !warning(QString::fromLatin1(m["warning message"].toByteArray())))
          return;
        auto seconds = m.value("interval").toLongLong();
        if (!seconds)
          seconds = m.value("min interval").toLongLong();
        setInterval(seconds);
        if (m.contains("tracker id"))
          trackerId = m.value("tracker id").toByteArray();
        if (!peers(m.value("peers"), 6))
          return;
        peers(m.value("peers6"), 18);
      } catch (const std::exception &e) {
        warning(e.what());
      }
    });
  }
  void udp(const QUrl &target, bool first) {
    auto *context = new QObject(owner);
    auto *socket = new QUdpSocket(context);
    socket->setProxy(QNetworkProxy::NoProxy);
    auto *timeout = new QTimer(context);
    timeout->setSingleShot(true);
    timeout->start(15000);
    struct Request {
      quint32 transaction = QRandomGenerator::global()->generate();
      bool announced = false;
      QHostAddress address;
    };
    auto state = std::make_shared<Request>();
    auto finish = [context] { context->deleteLater(); };
    QObject::connect(timeout, &QTimer::timeout, context, [this, finish] {
      finish();
      warning("tracker request timed out");
    });
    const auto port = quint16(target.port(80));
    QObject::connect(
        socket, &QUdpSocket::readyRead, context,
        [this, socket, state, first, port, timeout, finish] {
          const QPointer<TrackerNetwork> guard(owner);
          while (socket->hasPendingDatagrams()) {
            QByteArray b;
            b.resize(socket->pendingDatagramSize());
            QHostAddress from;
            quint16 fromPort;
            socket->readDatagram(b.data(), b.size(), &from, &fromPort);
            if (from != state->address || fromPort != port)
              continue;
            if (b.size() < 8 || qFromBigEndian<quint32>(b.constData() + 4) !=
                                    state->transaction) {
              timeout->stop();
              finish();
              warning("tracker sent invalid transaction id");
              return;
            }
            auto action = qFromBigEndian<quint32>(b.constData());
            if (action == 0 && !state->announced && b.size() >= 16) {
              state->announced = true;
              state->transaction = QRandomGenerator::global()->generate();
              QByteArray packet = b.mid(8, 8) + number32(1) +
                                  number32(state->transaction) + hash +
                                  "01234567890123456789" + QByteArray(8, 0) +
                                  QByteArray(8, char(255)) + QByteArray(8, 0) +
                                  number32(first ? 2 : 0) + number32(0) +
                                  number32(0) + number32(50);
              packet += QByteArray::fromHex("1ae1");
              socket->writeDatagram(packet, state->address, port);
              if (!guard || closed)
                return;
            } else if (action == 1 && state->announced && b.size() >= 20) {
              timeout->stop();
              setInterval(qFromBigEndian<quint32>(b.constData() + 8));
              finish();
              compact(b.mid(20), 6);
              return;
            } else {
              timeout->stop();
              finish();
              warning(action == 3 ? QString::fromUtf8(b.mid(8))
                                  : "invalid UDP tracker response");
              return;
            }
          }
        });
    QObject::connect(socket, &QUdpSocket::errorOccurred, context,
                     [this, socket, timeout, finish](auto) {
                       auto message = socket->errorString();
                       timeout->stop();
                       finish();
                       warning("UDP tracker socket error: " + message);
                     });
    auto send = [socket, state, port] {
      const QPointer<QUdpSocket> guard(socket);
      if (!socket->bind(QHostAddress::AnyIPv4, 0) || !guard)
        return;
      socket->writeDatagram(QByteArray::fromHex("0000041727101980") +
                                number32(0) + number32(state->transaction),
                            state->address, port);
    };
    QHostAddress numeric(target.host());
    if (!numeric.isNull()) {
      state->address = numeric;
      send();
    } else
      QHostInfo::lookupHost(target.host(), context,
                            [this, state, send, finish](const QHostInfo &i) {
                              for (const auto &a : i.addresses())
                                if (a.protocol() ==
                                    QAbstractSocket::IPv4Protocol) {
                                  state->address = a;
                                  send();
                                  return;
                                }
                              finish();
                              warning("tracker DNS failed");
                            });
  }
};
TrackerNetwork::TrackerNetwork(QString u, QString h, QObject *p)
    : QObject(p), state_(new State(this, std::move(u), std::move(h))) {}
TrackerNetwork::~TrackerNetwork() { close(); }
void TrackerNetwork::start() {
  if (state_->closed || state_->started)
    return;
  state_->started = true;
  state_->interval.start();
  state_->announce(true);
}
void TrackerNetwork::close() {
  if (state_->closed)
    return;
  state_->closed = true;
  state_->interval.stop();
  for (auto *r : state_->manager.findChildren<QNetworkReply *>()) {
    r->disconnect(this);
    r->abort();
    r->deleteLater();
  }
  for (auto *c : children())
    if (c != &state_->manager)
      delete c;
}
} // namespace server1::discovery
