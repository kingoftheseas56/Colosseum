#include "server1/casting/CastingService.h"
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>
#include <cmath>
namespace server1::casting {
struct CastingService::Impl {
  QString ffmpeg;
  media::ProcessPort port;
  QJsonArray devices;
  QMap<QString, QJsonObject> statuses;
  PlayerInvoker player;
  qsizetype buffered = 0, peak = 0;
};
CastingService::CastingService(QString ffmpeg, QObject *parent)
    : QObject(parent), impl_(new Impl) {
  impl_->ffmpeg = std::move(ffmpeg);
}
CastingService::~CastingService() = default;
void CastingService::setPlayerInvoker(PlayerInvoker invoker) {
  impl_->player = std::move(invoker);
}
bool CastingService::hasPlayerInvoker() const { return bool(impl_->player); }
void CastingService::invokePlayer(const QString &id, const QJsonObject &query,
                                  const QJsonObject &body,
                                  std::function<void(QJsonValue)> callback) {
  if (!impl_->statuses.contains(id))
    impl_->statuses[id] = initialPlayerStatus();
  auto &status = impl_->statuses[id];
  auto call = selectPlayerCall(status, query, body);
  if (!impl_->player) {
    callback(QJsonObject{});
    return;
  }
  QPointer<CastingService> self(this);
  impl_->player(device(id), call, status,
                [self, id, callback = std::move(callback)](QJsonValue result) {
                  if (!self)
                    return;
                  if (result.isObject())
                    self->impl_->statuses[id] = result.toObject();
                  callback(std::move(result));
                });
}
media::ProcessPort &CastingService::processes() { return impl_->port; }
qsizetype CastingService::bufferedBytes() const { return impl_->buffered; }
qsizetype CastingService::peakBufferedBytes() const { return impl_->peak; }
void CastingService::adjustBufferedBytes(qsizetype delta) {
  impl_->buffered += delta;
  impl_->peak = std::max(impl_->peak, impl_->buffered);
}
QString CastingService::executable() const { return impl_->ffmpeg; }
void CastingService::collect(const QJsonObject &d) {
  const auto id = d.value("id").toString();
  if (id.isEmpty() || !device(id).isEmpty())
    return;
  impl_->devices.append(d);
}
QJsonArray CastingService::devices() const { return impl_->devices; }
QJsonObject CastingService::device(const QString &id) const {
  for (const auto &v : impl_->devices)
    if (v.toObject().value("id").toString() == id)
      return v.toObject();
  return {};
}
namespace {
QJsonObject common() {
  return {{"playerUIRoles",
           QJsonArray{"playpause", "seek", "dub", "subtitles", "volume"}},
          {"usePlayerUI", true},
          {"onlyHtml5Formats", false}};
}
bool truth(const QJsonValue &v) {
  return !v.isUndefined() && !v.isNull() && !(v.isBool() && !v.toBool()) &&
         !(v.isString() && v.toString().isEmpty()) &&
         !(v.isDouble() && v.toDouble() == 0);
}
int integer(const QJsonValue &v) {
  static QRegularExpression r("^\\s*([+-]?\\d+)");
  auto m = r.match(v.isString() ? v.toString() : QString::number(v.toDouble()));
  return m.hasMatch() ? m.captured(1).toInt() : 0;
}
QString text(const QJsonValue &v) {
  return v.isString()   ? v.toString()
         : v.isDouble() ? QString::number(v.toDouble(), 'g', 16)
                        : QString{};
}
} // namespace
QJsonObject CastingService::ssdpDevice(const QString &location,
                                       const QByteArray &xml) {
  QXmlStreamReader reader(xml);
  QString type, id, name;
  int depth = 0, deviceDepth = -1;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      ++depth;
      if (reader.name() == u"device" && deviceDepth < 0)
        deviceDepth = depth;
      if (deviceDepth >= 0 && depth == deviceDepth + 1) {
        auto n = reader.name().toString();
        if (n == "deviceType" || n == "UDN" || n == "friendlyName") {
          auto s = reader.readElementText();
          --depth;
          if (n == "deviceType")
            type = s;
          else if (n == "UDN")
            id = s;
          else
            name = s;
        }
      }
    } else if (reader.isEndElement()) {
      if (depth == deviceDepth)
        break;
      --depth;
    }
  }
  if (reader.hasError() || id.isEmpty())
    return {};
  type = type == "urn:dial-multiscreen-org:device:dial:1"        ? "chromecast"
         : type == "urn:schemas-upnp-org:device:MediaRenderer:1" ? "tv"
                                                                 : "";
  if (type.isEmpty())
    return {};
  auto d = common();
  d["facility"] = "SSDP";
  d["id"] = id.mid(5);
  d["name"] = name;
  d["host"] = QUrl(location).host();
  d["location"] = location;
  d["type"] = type;
  d["icon"] = type;
  return d;
}
QJsonObject CastingService::mdnsDevice(const QJsonObject &response) {
  auto answers = response.value("answers").toArray();
  if (answers.isEmpty() ||
      answers.first().toObject().value("name") != "_googlecast._tcp.local")
    return {};
  QJsonObject records;
  auto add = [&](const QJsonArray &a) {
    for (auto v : a) {
      auto r = v.toObject();
      records[r.value("type").toString()] = r.value("data");
    }
  };
  add(response.value("additionals").toArray());
  add(answers);
  if (!records.contains("TXT") || !records.contains("SRV"))
    return {};
  QString name;
  for (auto v : records.value("TXT").toArray()) {
    auto s = v.toString();
    if (s.startsWith("fn="))
      name = s.mid(3);
  }
  if (name.isEmpty())
    name =
        records.value("PTR").toString().replace("._googlecast._tcp.local", "");
  auto id = records.value("SRV").toObject().value("target").toString();
  auto pos = id.indexOf(".local");
  if (pos >= 0)
    id.truncate(pos);
  auto d = common();
  d["facility"] = "MDNS";
  d["id"] = id;
  d["name"] = name;
  if (records.contains("A")) {
    d["host"] = records["A"];
    d["location"] = records["A"];
  }
  d["type"] = "chromecast";
  d["icon"] = "chromecast";
  return d;
}
double CastingService::hmsToSeconds(const QString &time) {
  static QRegularExpression r("(\\d+):(\\d+):(\\d+)(?:.(\\d+))?",
                              QRegularExpression::CaseInsensitiveOption);
  auto m = r.match(time);
  return m.hasMatch()
             ? 3600 * m.captured(1).toDouble() + 60 * m.captured(2).toDouble() +
                   m.captured(3).toDouble() + m.captured(4).toDouble() / 100
             : -1;
}
QJsonObject CastingService::videoInfo(const QByteArray &data) {
  const auto text = QString::fromUtf8(data);
  static QRegularExpression dur("duration: (\\d+:\\d+:\\d+.\\d+)",
                                QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression stream(
      "#(\\d+:\\d+)(?:\\((\\w{3})\\)|):\\s(\\w+):\\s(\\w+)(?:\\s([^,]+),\\s("
      "\\w+\\([^)]+\\)|[^,]+),\\s([^,]+),\\s(.*?)(?:\\s(\\(default\\)))?$)?",
      QRegularExpression::MultilineOption);
  QJsonArray streams;
  for (const auto &line : text.split('\n')) {
    if (!line.trimmed().startsWith("Stream"))
      continue;
    auto m = stream.match(line);
    if (!m.hasMatch())
      continue;
    QJsonObject s{{"id", m.captured(1)},
                  {"lang", m.captured(2).isEmpty() ? "und" : m.captured(2)},
                  {"type", m.captured(3)},
                  {"codec", m.captured(4)},
                  {"default", !m.captured(9).isEmpty()}};
    if (m.capturedStart(8) >= 0)
      s["misc"] = m.captured(8);
    if (s["type"] == "Video") {
      if (m.capturedStart(6) >= 0)
        s["pixfmt"] = m.captured(6);
      if (m.capturedStart(7) >= 0)
        s["vidfmt"] = m.captured(7);
    } else if (s["type"] == "Audio") {
      if (m.capturedStart(6) >= 0)
        s["frequency"] = m.captured(6);
      if (m.capturedStart(7) >= 0)
        s["channels"] = m.captured(7);
    }
    streams.append(s);
  }
  auto m = dur.match(text);
  return {{"duration", m.hasMatch() ? hmsToSeconds(m.captured(1)) : 0},
          {"streams", streams}};
}
QStringList CastingService::arguments(const QJsonObject &q,
                                      const QJsonObject &info,
                                      const QString &subs) {
  const auto streams = info.value("streams").toArray();
  auto find = [&](const QString &type, auto predicate) {
    for (const auto &s : streams) {
      auto o = s.toObject();
      if (o.value("type") == type && predicate(o))
        return o;
    }
    return QJsonObject{};
  };
  auto any = [](const QJsonObject &) { return true; };
  bool copyVideo = subs.isEmpty(), copyAudio = true;
  auto video = find(
      "Video", [](const QJsonObject &s) { return s.value("codec") == "h264"; });
  if (video.isEmpty()) {
    copyVideo = false;
    video = find("Video", any);
  }
  const int offset = integer(q.value("time"));
  double duration = info.value("duration").toDouble();
  if (duration)
    duration -= offset;
  auto audio = find("Audio", [&](const QJsonObject &s) {
    return truth(q.value("audioTrack"))
               ? text(s.value("id")) == text(q.value("audioTrack"))
               : s.value("default").toBool();
  });
  if (!audio.isEmpty())
    copyAudio =
        audio.value("codec") == "aac" && audio.value("channels") == "stereo";
  if (audio.isEmpty() || duration == 0) {
    copyAudio = false;
    audio = find("Audio", any);
  }
  QStringList args;
  if (truth(q.value("flagRe")))
    args << "-re";
  auto source = q.value("video").toString();
  if (source.startsWith("file:"))
    source = QUrl(source).toLocalFile();
  args << "-copyts" << "-ss" << QString::number(duration > 0 ? offset : 0)
       << "-i" << source;
  if (duration > 0)
    args << "-t" << QString::number(duration + offset, 'g', 16);
  if (!video.isEmpty())
    args << "-map" << video.value("id").toString();
  if (!audio.isEmpty())
    args << "-map" << audio.value("id").toString();
  if (copyVideo && copyAudio)
    args << "-c" << "copy";
  else {
    if (!video.isEmpty()) {
      if (copyVideo)
        args << "-c:v" << "copy";
      else {
        args << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-tune"
             << "zerolatency" << "-pix_fmt" << "yuv420p";
        if (!subs.isEmpty())
          args << "-vf" << ("subtitles=" + subs);
      }
    }
    if (!audio.isEmpty()) {
      if (copyAudio)
        args << "-c:a" << "copy";
      else
        args << "-strict" << "-2" << "-c:a" << "aac" << "-ac" << "2";
    }
  }
  const bool mp4 = truth(q.value("fmp4"));
  if (mp4)
    args << "-movflags" << "frag_keyframe+empty_moov";
  args << "-f" << (mp4 ? "mp4" : "matroska") << "-threads" << "0" << "pipe:1";
  return args;
}
} // namespace server1::casting
