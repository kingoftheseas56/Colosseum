#include "server1/media/HlsPlaylist.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace server1::media {
namespace {
QString number(double x) { return QString::number(x, 'g', 16); }
QString jsString(const QJsonValue &v) {
  if (v.isString())
    return v.toString();
  if (v.isDouble())
    return number(v.toDouble());
  if (v.isUndefined())
    return "undefined";
  if (v.isNull())
    return "null";
  if (v.isBool())
    return v.toBool() ? "true" : "false";
  return "[object Object]";
}
double duration(const QJsonObject &s) {
  double d = s["duration"].toDouble();
  for (auto e : s["extraSegments"].toArray())
    d += e.toObject()["duration"].toDouble();
  return d;
}
} // namespace
QString HlsPlaylist::fixed(double value, int digits) {
  if (digits < 0 || digits > 100)
    throw std::invalid_argument("toFixed digits out of range");
  if (!std::isfinite(value))
    return std::isnan(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity";
  if (std::abs(value) >= 1e21)
    return number(value);
  const bool negative = value < 0;
  value = std::abs(value);
  quint64 bits;
  std::memcpy(&bits, &value, sizeof bits);
  int exponent = int((bits >> 52) & 2047);
  quint64 mantissa = bits & ((quint64(1) << 52) - 1);
  if (exponent)
    mantissa |= quint64(1) << 52;
  exponent = (exponent ? exponent - 1023 : 1 - 1023) - 52;
  using boost::multiprecision::cpp_int;
  cpp_int n = mantissa, denominator = 1;
  for (int i = 0; i < digits; ++i)
    n *= 10;
  if (exponent >= 0)
    n <<= exponent;
  else
    denominator <<= -exponent;
  cpp_int rounded = n / denominator;
  if ((n % denominator) * 2 >= denominator)
    ++rounded;
  QString out = QString::fromStdString(rounded.convert_to<std::string>());
  if (digits) {
    while (out.size() <= digits)
      out.prepend('0');
    out.insert(out.size() - digits, '.');
  }
  if (negative)
    out.prepend('-');
  return out;
}
QString HlsPlaylist::master(const QJsonArray &streams, const QString &query) {
  bool video = false;
  QJsonArray audio, subs;
  for (auto x : streams) {
    auto s = x.toObject();
    auto t = s["track"].toString();
    if (t == "video")
      video = true;
    else if (t == "audio")
      audio.append(s);
    else if (t == "subtitle")
      subs.append(s);
  }
  QStringList lines{"#EXTM3U", "#EXT-X-VERSION:7"};
  if (video)
    lines << "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"video\",NAME=\"Video\","
             "AUTOSELECT=YES,DEFAULT=YES";
  auto label = [](QJsonObject s) {
    return jsString(s["title"].isString()      ? s["title"]
                    : s["language"].isString() ? s["language"]
                                               : s["id"]);
  };
  for (auto x : audio) {
    auto s = x.toObject();
    QString l =
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"" + label(s) + "\"";
    if (s["language"].isString())
      l += ",LANGUAGE=\"" + s["language"].toString() + "\"";
    l += ",AUTOSELECT=YES,DEFAULT=";
    l += s["id"].isDouble() && s["id"].toDouble() == 0 ? "YES" : "NO";
    if (video)
      l += ",URI=\"audio" + jsString(s["id"]) + ".m3u8?" + query + "\"";
    lines << l;
  }
  for (auto x : subs) {
    auto s = x.toObject();
    QString l = "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subtitles\",NAME=\"" +
                label(s) + "\"";
    if (s["language"].isString())
      l += ",LANGUAGE=\"" + s["language"].toString() + "\"";
    l += ",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\"subtitle" +
         jsString(s["id"]) + ".m3u8?" + query + "\"";
    lines << l;
  }
  QString info = "#EXT-X-STREAM-INF:BANDWIDTH=164000";
  if (video)
    info += ",VIDEO=\"video\"";
  if (!audio.isEmpty())
    info += ",AUDIO=\"audio\"";
  if (!subs.isEmpty())
    info += ",SUBTITLES=\"subtitles\"";
  lines << info + ",NAME=\"Main\"";
  if (video)
    lines << "video0.m3u8?" + query;
  else if (!audio.isEmpty())
    lines << "audio0.m3u8?" + query;
  return lines.join('\n');
}
QString HlsPlaylist::media(const QJsonArray &segments, double timescale,
                           const QString &track, const QString &query) {
  if (!std::isfinite(timescale) || timescale <= 0)
    throw std::invalid_argument("Invalid media timescale");
  bool av = QRegularExpression("^(?:video|audio).+$").match(track).hasMatch();
  double target = 1;
  for (auto s : segments)
    target = std::max(target, std::ceil(duration(s.toObject()) / timescale));
  QStringList lines{"#EXTM3U", "#EXT-X-VERSION:7",
                    "#EXT-X-TARGETDURATION:" + number(target),
                    "#EXT-X-MEDIA-SEQUENCE:1", "#EXT-X-PLAYLIST-TYPE:VOD"};
  if (av)
    lines << "#EXT-X-MAP:URI=\"" + track + "/init.mp4?" + query + "\"";
  for (qsizetype i = 0; i < segments.size(); ++i) {
    lines << "#EXTINF:" +
                 fixed(duration(segments[i].toObject()) / timescale, 6) + ",";
    lines << track + "/segment" + QString::number(i + 1) +
                 (av ? ".m4s?" : ".vtt?") + query;
  }
  lines << "#EXT-X-ENDLIST";
  return lines.join('\n');
}
} // namespace server1::media
