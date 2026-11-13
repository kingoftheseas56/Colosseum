#include "server1/casting/CastingProtocol.h"
#include <QJsonDocument>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <cmath>
#include <memory>
namespace server1::casting {
namespace {
bool truth(const QJsonValue &v) {
  return !v.isNull() && !v.isUndefined() &&
         !(v.isString() && v.toString().isEmpty()) &&
         !(v.isBool() && !v.toBool()) && !(v.isDouble() && v.toDouble() == 0);
}
double num(const QJsonValue &v, bool integer = false) {
  auto s = v.isString() ? v.toString() : QString::number(v.toDouble());
  QRegularExpression r(
      integer ? "^\\s*([+-]?\\d+)"
              : "^\\s*([+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)");
  auto m = r.match(s);
  return m.hasMatch() ? m.captured(1).toDouble() : qQNaN();
}
QJsonValue state(const QString &s, bool chromecast) {
  static const QMap<QString, int> states{
      {"IDLE", 0},          {"TRANSITIONING", 2},    {"BUFFERING", 2},
      {"PLAYING", 3},       {"PAUSED", 4},           {"PAUSED_PLAYBACK", 4},
      {"STOPPED", 5},       {"NO_MEDIA_PRESENT", 6}, {"Ready To Cast", 6},
      {"ERROR_OCCURRED", 7}};
  if (!states.contains(s) ||
      (chromecast &&
       (s == "IDLE" || s == "TRANSITIONING" || s == "PAUSED_PLAYBACK")) ||
      (!chromecast &&
       (s == "BUFFERING" || s == "PAUSED" || s == "Ready To Cast")))
    return QJsonValue(QJsonValue::Undefined);
  return states[s];
}
void varint(QByteArray &out, quint64 v) {
  while (v > 127) {
    out.append(char((v & 127) | 128));
    v >>= 7;
  }
  out.append(char(v));
}
void field(QByteArray &out, int key, const QByteArray &v) {
  varint(out, (key << 3) | 2);
  varint(out, v.size());
  out += v;
}
bool readVar(const QByteArray &data, qsizetype &pos, quint64 &v) {
  v = 0;
  for (int shift = 0; shift < 64 && pos < data.size(); shift += 7) {
    auto b = quint8(data[pos++]);
    v |= quint64(b & 127) << shift;
    if (!(b & 128))
      return true;
  }
  return false;
}
} // namespace
QJsonObject initialPlayerStatus() {
  return {{"audio", QJsonArray{}},
          {"audioTrack", QJsonValue::Null},
          {"volume", 100},
          {"time", 0},
          {"paused", false},
          {"state", 5},
          {"length", 0},
          {"source", QJsonValue::Null},
          {"subtitlesSrc", QJsonValue::Null},
          {"subtitlesDelay", 0},
          {"subtitlesSize", 2}};
}
PlayerCall selectPlayerCall(QJsonObject &status, const QJsonObject &query,
                            const QJsonObject &body) {
  auto params = query;
  for (auto i = body.begin(); i != body.end(); ++i)
    params[i.key()] = i.value();
  for (auto i = params.begin(); i != params.end(); ++i)
    status[i.key()] = i.value();
  PlayerCall call{"status", {QJsonValue::Null}};
  auto set = [&](const QString &m, const QJsonValue &v) {
    call = {m, v.isArray() ? v.toArray() : QJsonArray{v}};
    while (call.arguments.size() > 3)
      call.arguments.removeLast();
  };
  if (params.contains("formats"))
    call.method = "protocolsGet";
  if (params.contains("audioTrack"))
    set("audioTrack", params["audioTrack"]);
  if (params.contains("volume"))
    set("volume", params["volume"]);
  if (params.contains("time"))
    set("seek", params["time"]);
  if (params.contains("subtitlesSrc") || params.contains("subtitlesDelay") ||
      params.contains("subtitlesSize")) {
    auto size = num(status["subtitlesSize"], true);
    call = {"subtitles",
            {status["subtitlesSrc"], status["subtitlesDelay"],
             QJsonObject{{"fontSize", std::isfinite(size)
                                          ? QString::number(size + 1) + "vw"
                                          : "NaNvw"}}}};
  }
  if (params.contains("source")) {
    if (truth(params["source"]))
      set("play", params["source"]);
    else
      call = {"close", {QJsonValue::Null}};
  }
  if (params.contains("stop"))
    call = {"stop", {QJsonValue::Null}};
  if (params.contains("paused")) {
    auto p = params["paused"];
    bool resume = p == QJsonValue("") || p == QJsonValue("0") ||
                  p == QJsonValue("false") || p == QJsonValue(0) ||
                  p == QJsonValue(false);
    call = {resume ? "resume" : "pause", {QJsonValue::Null}};
  }
  return call;
}
QJsonObject conformDlnaStatus(QJsonObject s, const QJsonObject &u, int flags,
                              qint64 seek) {
  if (flags & 2)
    return s;
  if (u.contains("TransportState")) {
    s["realStatus"] = u["TransportState"];
    s.insert("state", state(u["TransportState"].toString(), false));
    if (s.value("state") == 6 && flags)
      s["state"] = 5;
    s["paused"] = s.value("state") == 4;
  }
  if (u.contains("CurrentTrackPosition"))
    s["time"] = seek + 1000 * num(u["CurrentTrackPosition"], true);
  if (u.contains("CurrentVolume"))
    s["volume"] = num(u["CurrentVolume"]) / 100;
  return s;
}
QJsonObject conformChromecastStatus(QJsonObject s, QJsonObject u, int flags) {
  if (u.contains("status"))
    u = u["status"].toObject();
  if (u.contains("volume") && !truth(s["volume"]))
    s["volume"] = u["volume"].toObject()["level"];
  auto apps = u["applications"].toArray();
  if (!apps.isEmpty())
    s.insert("state",
             state(apps.first().toObject()["statusText"].toString(), true));
  if (truth(u["playerState"]))
    s.insert("state", state(u["playerState"].toString(), true));
  if (s.value("state") == 6 && flags)
    s["state"] = 5;
  s["paused"] = s.value("state") == 4;
  if (truth(u["currentTime"]))
    s["time"] = 1000 * u["currentTime"].toDouble();
  return s;
}
QByteArray encodeCastFrame(const QString &s, const QString &r,
                           const QString &ns, const QJsonObject &m) {
  QByteArray payload;
  varint(payload, 8);
  varint(payload, 0);
  field(payload, 2, s.toUtf8());
  field(payload, 3, r.toUtf8());
  field(payload, 4, ns.toUtf8());
  varint(payload, 40);
  varint(payload, 0);
  field(payload, 6, QJsonDocument(m).toJson(QJsonDocument::Compact));
  QByteArray out(4, 0);
  qToBigEndian<quint32>(payload.size(), out.data());
  return out + payload;
}
bool decodeCastFrames(QByteArray &pending, QJsonArray &messages,
                      QString &error) {
  while (pending.size() >= 4) {
    auto n = qFromBigEndian<quint32>(pending.constData());
    if (n > 1024 * 1024) {
      error = "Cast frame exceeds limit";
      return false;
    }
    if (pending.size() < 4 + qint64(n))
      return true;
    auto data = pending.mid(4, n);
    pending.remove(0, 4 + n);
    qsizetype pos = 0;
    QJsonObject obj;
    while (pos < data.size()) {
      quint64 tag, value;
      if (!readVar(data, pos, tag)) {
        error = "Invalid Cast protobuf tag";
        return false;
      }
      if ((tag & 7) == 0) {
        if (!readVar(data, pos, value)) {
          error = "Invalid Cast protobuf number";
          return false;
        }
      } else if ((tag & 7) == 2) {
        if (!readVar(data, pos, value) || value > quint64(data.size() - pos)) {
          error = "Invalid Cast protobuf length";
          return false;
        }
        auto bytes = data.mid(pos, value);
        pos += value;
        switch (tag >> 3) {
        case 2:
          obj["sourceId"] = QString::fromUtf8(bytes);
          break;
        case 3:
          obj["destinationId"] = QString::fromUtf8(bytes);
          break;
        case 4:
          obj["namespace"] = QString::fromUtf8(bytes);
          break;
        case 6: {
          QJsonParseError e;
          auto json = QJsonDocument::fromJson(bytes, &e);
          if (e.error != QJsonParseError::NoError || !json.isObject()) {
            error = "Invalid Cast JSON";
            return false;
          }
          obj["message"] = json.object();
          break;
        }
        default:
          break;
        }
      } else {
        error = "Unsupported Cast protobuf wire type";
        return false;
      }
    }
    messages.append(obj);
  }
  return true;
}
DlnaSoapClient::DlnaSoapClient(QUrl endpoint, QObject *parent)
    : QObject(parent), endpoint_(std::move(endpoint)) {}
QByteArray DlnaSoapClient::envelope(const QString &service,
                                    const QString &action,
                                    const QJsonObject &args) {
  QByteArray bytes;
  QXmlStreamWriter xml(&bytes);
  xml.writeStartDocument();
  xml.writeStartElement("s:Envelope");
  xml.writeNamespace("http://schemas.xmlsoap.org/soap/envelope/", "s");
  xml.writeAttribute("s:encodingStyle",
                     "http://schemas.xmlsoap.org/soap/encoding/");
  xml.writeStartElement("s:Body");
  xml.writeStartElement("u:" + action);
  xml.writeNamespace(service, "u");
  for (auto i = args.begin(); i != args.end(); ++i)
    xml.writeTextElement(i.key(),
                         i.value().isString() ? i.value().toString()
                         : i.value().isDouble()
                             ? QString::number(i.value().toDouble(), 'g', 16)
                         : i.value().isNull() ? QString()
                         : i.value().toBool() ? "true"
                                              : "false");
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeEndDocument();
  return bytes;
}
void DlnaSoapClient::call(const QString &service, const QString &action,
                          const QJsonObject &args, Callback callback) {
  static QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_.-]*$");
  if (!identifier.match(action).hasMatch() || service.contains('"') ||
      service.contains('\r') || service.contains('\n') ||
      !QStringList{"http", "https"}.contains(endpoint_.scheme())) {
    callback({{}, "Invalid DLNA endpoint or action"});
    return;
  }
  auto *manager = new QNetworkAccessManager(this);
  QNetworkRequest request(endpoint_);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(5000);
  request.setRawHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  request.setRawHeader("SOAPACTION",
                       ("\"" + service + "#" + action + "\"").toUtf8());
  auto *reply = manager->post(request, envelope(service, action, args));
  reply->setReadBufferSize(1024 * 1024);
  auto bytes = std::make_shared<QByteArray>();
  connect(reply, &QIODevice::readyRead, reply, [reply, bytes] {
    bytes->append(reply->readAll());
    if (bytes->size() > 1024 * 1024)
      reply->abort();
  });
  connect(
      reply, &QNetworkReply::finished, this,
      [reply, manager, bytes, callback = std::move(callback)] {
        bytes->append(reply->readAll());
        SoapResult result;
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300)
          result.error =
              QString("DLNA HTTP %1: %2").arg(status).arg(reply->errorString());
        else {
          QXmlStreamReader xml(*bytes);
          while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
              auto name = xml.name().toString();
              if (name == "Fault")
                result.error = "DLNA SOAP Fault";
              else if (name != "Envelope" && name != "Body" &&
                       !name.endsWith("Response")) {
                auto text =
                    xml.readElementText(QXmlStreamReader::IncludeChildElements);
                result.values[name] = text;
              }
            }
          }
          if (xml.hasError())
            result.error = "Invalid DLNA XML";
        }
        manager->deleteLater();
        callback(std::move(result));
      });
}
} // namespace server1::casting
