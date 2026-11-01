#include "server1/media/SubtitleFetch.h"
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTimer>
#include <QtEndian>
#include <cerrno>
#include <cmath>
#include <iconv.h>
#include <limits>
#include <zlib.h>
namespace server1::media {
QString detectSubtitleCharset(const QByteArray &);
struct DecodeTable {
  const char *name;
  const ushort *map;
};
#include "SubtitleDecodeTables.inc"
void mediaRequest(QObject *life, const QString &path, const QByteArray &method,
                  const QMap<QByteArray, QByteArray> &headers, ByteCallback cb,
                  int redirects) {
  QPointer<QObject> guard(life);
  if (!path.startsWith("http")) {
    QTimer::singleShot(0, life, [=] {
      ByteResult r;
      r.url = QUrl::fromLocalFile(path);
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
        r.error = f.errorString();
      else {
        r.status = 200;
        r.headers["content-length"] = QByteArray::number(f.size());
        if (method != "HEAD")
          r.data = f.readAll();
      }
      if (guard)
        cb(r);
    });
    return;
  }
  auto *manager = new QNetworkAccessManager(life);
  QNetworkRequest req{QUrl(path)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::ManualRedirectPolicy);
  for (auto it = headers.begin(); it != headers.end(); ++it)
    req.setRawHeader(it.key(), it.value());
  auto *reply = method == "HEAD" ? manager->head(req) : manager->get(req);
  QObject::connect(reply, &QNetworkReply::finished, life, [=] {
    ByteResult r;
    r.url = reply->url();
    r.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.statusText =
        reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    for (auto h : reply->rawHeaderPairs())
      r.headers[h.first.toLower()] = h.second;
    r.data = reply->readAll();
    auto redirect =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!redirect.isEmpty() && redirects > 0) {
      auto url = r.url.resolved(redirect).toString();
      manager->deleteLater();
      mediaRequest(life, url, method, headers, cb, redirects - 1);
      return;
    }
    if (!redirect.isEmpty())
      r.error = "Too many redirects";
    else if (reply->error() != QNetworkReply::NoError && !r.status)
      r.error = reply->errorString();
    manager->deleteLater();
    if (guard)
      cb(r);
  });
}
static double integer(QString s) {
  static QRegularExpression rx("^[\\s]*([+-]?[0-9]+)");
  auto m = rx.match(s);
  return m.hasMatch() ? m.captured(1).toDouble()
                      : std::numeric_limits<double>::quiet_NaN();
}
static double time(QString s) {
  if (s.isEmpty())
    return INFINITY;
  auto chunks = s.split(':');
  if (chunks.size() != 3)
    return INFINITY;
  auto sec = chunks[2].split(QRegularExpression("[,\\.]"));
  return 3600000 * integer(chunks[0]) + 60000 * integer(chunks[1]) +
         1000 * integer(sec.value(0)) + integer(sec.value(1));
}
QJsonArray parseSrt(const QString &input) {
  QString text = input;
  text.remove('\r');
  QJsonArray out;
  for (auto block : text.split("\n\n")) {
    auto lines = block.split('\n');
    while (lines.size() > 2 && !lines[0].contains(" --> "))
      lines.removeFirst();
    if (lines.size() < 2)
      continue;
    auto times = lines.takeFirst().split(' ');
    double a = time(times.value(0)), b = time(times.value(2));
    auto caption = lines.join('\n');
    if (a == INFINITY || b == INFINITY || caption.isEmpty())
      continue;
    out.append(QJsonObject{
        {"number", out.size()},
        {"startTime", std::isfinite(a) ? QJsonValue(a) : QJsonValue()},
        {"endTime", std::isfinite(b) ? QJsonValue(b) : QJsonValue()},
        {"text", caption}});
  }
  return out;
}
static QByteArray inflateBytes(const QByteArray &data, int bits,
                               QString *error) {
  z_stream z{};
  if (inflateInit2(&z, bits) != Z_OK) {
    *error = "inflate init failed";
    return {};
  }
  z.next_in = (Bytef *)data.constData();
  z.avail_in = data.size();
  QByteArray out;
  char buf[32768];
  int rc;
  do {
    z.next_out = (Bytef *)buf;
    z.avail_out = sizeof(buf);
    rc = inflate(&z, Z_NO_FLUSH);
    out.append(buf, sizeof(buf) - z.avail_out);
    if (rc == Z_STREAM_END && bits == 47 && z.avail_in > 0) {
      // Node createUnzip consumes concatenated gzip members.
      auto next = z.next_in;
      auto remaining = z.avail_in;
      rc = inflateReset2(&z, bits);
      z.next_in = next;
      z.avail_in = remaining;
    }
  } while (rc == Z_OK);
  if (rc != Z_STREAM_END)
    *error = z.msg ? QString::fromLatin1(z.msg) : "unexpected end of file";
  inflateEnd(&z);
  return out;
}
QByteArray unwrapSubtitle(const QString &path, const QByteArray &data,
                          QString *error) {
  if (path.endsWith("gz"))
    return inflateBytes(data, 47, error);
  if (!path.endsWith("zip"))
    return data;
  // Central-directory order matches node-native-zip Reader.forEach, not
  // filename order.
  auto u16 = [&](qsizetype p) {
    return qFromLittleEndian<quint16>((const uchar *)data.constData() + p);
  };
  auto u32 = [&](qsizetype p) {
    return qFromLittleEndian<quint32>((const uchar *)data.constData() + p);
  };
  qsizetype end = data.lastIndexOf(QByteArray("PK\5\6", 4));
  if (end < 0 || end + 22 > data.size()) {
    *error = "Invalid ZIP archive";
    return {};
  }
  quint32 pos = u32(end + 16);
  int count = u16(end + 10);
  for (int i = 0; i < count; ++i) {
    if (quint64(pos) + 46 > quint64(data.size()) ||
        data.mid(pos, 4) != QByteArray("PK\1\2", 4)) {
      *error = "Invalid ZIP directory";
      return {};
    }
    auto size = u32(pos + 20), local = u32(pos + 42);
    auto nameLen = u16(pos + 28), extra = u16(pos + 30),
         comment = u16(pos + 32), method = u16(pos + 10);
    auto name = data.mid(pos + 46, nameLen);
    pos += 46 + nameLen + extra + comment;
    if (!name.endsWith("srt"))
      continue;
    if (quint64(local) + 30 > quint64(data.size())) {
      *error = "Invalid ZIP entry";
      return {};
    }
    quint64 start = quint64(local) + 30 + u16(local + 26) + u16(local + 28);
    if (start + size > quint64(data.size())) {
      *error = "Invalid ZIP entry";
      return {};
    }
    auto compressed = data.mid(start, size);
    if (method == 0)
      return compressed;
    if (method == 8)
      return inflateBytes(compressed, -15, error);
    *error = "Unsupported ZIP compression";
    return {};
  }
  *error = "no srt found in zip file " + path;
  return {};
}
QString decodeSubtitle(const QByteArray &data, const QString &requested,
                       QString *error) {
  auto charset = requested;
  if (charset.isEmpty() || charset == "auto")
    charset = detectSubtitleCharset(data);
  if (charset.isEmpty()) {
    *error = "Cannot read properties of undefined (reading 'charsetName')";
    return {};
  }
  if (charset == "ISO-8859-8-I")
    charset = "ISO-8859-8";
  QString canonical =
      charset.toLower().remove(QRegularExpression(":\\d{4}$|[^0-9a-z]"));
  if (canonical.startsWith("cp125"))
    canonical.replace(0, 2, "windows");
  for (auto table : decodeTables)
    if (canonical == table.name) {
      QString text;
      text.reserve(data.size());
      for (auto c : data)
        text += QChar(table.map[quint8(c)]);
      return text;
    }
  if (canonical == "utf8") {
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QChar(0xfeff)))
      text.remove(0, 1);
    return text;
  }
  if (canonical == "gb18030" || canonical == "big5" ||
      canonical == "shiftjis" || canonical == "eucjp" || canonical == "euckr") {
    auto codec = iconv_open("UTF-8", canonical == "shiftjis"
                                         ? "CP932"
                                         : charset.toLatin1().constData());
    if (codec != (iconv_t)-1) {
      QByteArray out;
      char *input = const_cast<char *>(data.constData());
      size_t left = data.size();
      while (left) {
        char buffer[4096];
        char *dest = buffer;
        size_t available = sizeof(buffer);
        auto rc = iconv(codec, &input, &left, &dest, &available);
        out.append(buffer, dest - buffer);
        if (rc == size_t(-1) && errno != E2BIG) {
          out.append("\xef\xbf\xbd", 3);
          ++input;
          --left;
          iconv(codec, nullptr, nullptr, nullptr, nullptr);
        }
      }
      iconv_close(codec);
      return QString::fromUtf8(out);
    }
  }
  QStringDecoder decoder(charset.toLatin1().constData(),
                         canonical.startsWith("utf16")
                             ? QStringConverter::Flag::Default
                             : QStringConverter::Flag::Stateless);
  if (!decoder.isValid() || charset.toLower().remove('-').startsWith("utf32")) {
    *error =
        "Encoding not recognized: '" + charset + "' (searched as: '" +
        charset.toLower().remove(QRegularExpression(":\\d{4}$|[^0-9a-z]")) +
        "')";
    return {};
  }
  QString text = decoder.decode(data);
  if (text.startsWith(QChar(0xfeff)))
    text.remove(0, 1);
  return text;
}
QObject *retrieveSubtitle(const QString &path, SubtitleOptions options,
                          std::function<void(SubtitleResult)> cb,
                          QObject *parent) {
  auto *job = new QObject(parent);
  QPointer<QObject> safe(job);
  if (path.isEmpty()) {
    QTimer::singleShot(0, job, [=] {
      cb({{}, {}, "pass .url"});
      if (safe)
        safe->deleteLater();
    });
    return job;
  }
  auto attempt = std::make_shared<std::function<void(SubtitleOptions)>>();
  std::weak_ptr<std::function<void(SubtitleOptions)>> weak = attempt;
  *attempt = [=](SubtitleOptions opt) {
    auto keep = weak.lock();
    auto receive = [=](ByteResult r) {
      SubtitleResult result;
      if (!r.error.isEmpty())
        result.error = r.error;
      else {
        auto bytes = unwrapSubtitle(path, r.data, &result.error);
        if (result.error.isEmpty())
          result.text = decodeSubtitle(bytes, opt.charset, &result.error);
        if (result.error.isEmpty())
          result.tracks = parseSrt(result.text);
      }
      if (!result.error.isEmpty() && opt.hasAgent) {
        (*keep)(SubtitleOptions{});
        return;
      }
      cb(result);
      if (safe)
        safe->deleteLater();
    };
    if (opt.hasAgent && opt.agentRequest)
      opt.agentRequest(job, path, receive);
    else
      mediaRequest(job, path, "GET", {}, receive);
  };
  (*attempt)(options);
  return job;
}
QByteArray renderSubtitles(const QJsonArray &tracks, bool vtt, qint64 offset) {
  QString out = vtt ? "WEBVTT\n\n" : "";
  int i = 0;
  for (auto v : tracks) {
    auto t = v.toObject();
    auto format = [&](QJsonValue n) {
      if (n.isNull() || !std::isfinite(n.toDouble() + offset) ||
          std::abs(n.toDouble() + offset) > 8640000000000000.0)
        return QString("Invalid date");
      return QDateTime::fromMSecsSinceEpoch(n.toDouble() + offset, Qt::UTC)
          .toString(vtt ? "HH:mm:ss.zzz" : "HH:mm:ss,zzz");
    };
    out += QString::number(i++) + '\n' + format(t["startTime"]) + " --> " +
           format(t["endTime"]) + '\n' +
           t["text"].toString().replace('&', "&amp;") + "\n\n";
  }
  return out.toUtf8();
}
} // namespace server1::media
