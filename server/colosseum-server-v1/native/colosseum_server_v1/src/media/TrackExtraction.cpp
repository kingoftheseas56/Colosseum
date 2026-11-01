#include "server1/media/TrackExtraction.h"
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QtEndian>
#include <limits>
#include <memory>
#include <stdexcept>
namespace server1::media {
namespace {
struct Element {
  QByteArray name, data;
  quint64 offset = 0, size = 0, dataSize = 0;
};
quint64 be(const QByteArray &b, int pos, int n) {
  if (pos < 0 || pos + n > b.size())
    throw std::runtime_error("truncated field");
  quint64 v = 0;
  for (int i = 0; i < n; ++i)
    v = (v << 8) | quint8(b[pos + i]);
  return v;
}
QVector<Element> boxes(const QByteArray &b) {
  QVector<Element> out;
  quint64 pos = 0;
  while (pos < quint64(b.size())) {
    if (pos + 8 > quint64(b.size()))
      throw std::runtime_error("truncated box");
    auto size = be(b, pos, 4);
    int header = 8;
    if (size == 1) {
      size = be(b, pos + 8, 8);
      header = 16;
    }
    if (size < quint64(header) ||
        size > quint64(std::numeric_limits<qint64>::max()) - pos)
      throw std::runtime_error("invalid box size");
    out.push_back({b.mid(pos + 4, 4), b.mid(pos + header, size - header), pos,
                   size, size - 8});
    pos += size;
  }
  return out;
}
Element find(const QVector<Element> &e, const QByteArray &name) {
  for (auto x : e)
    if (x.name == name)
      return x;
  return {};
}
QPair<quint64, int> vint(const QByteArray &b, int p) {
  if (p >= b.size())
    throw std::runtime_error("truncated vint");
  quint8 first = b[p];
  int n = 1;
  while (n <= 8 && !(first & (0x100 >> n)))
    ++n;
  if (n > 8)
    throw std::runtime_error("invalid vint");
  return {be(b, p, n) & ((quint64(1) << (7 * n)) - 1), n};
}
QVector<Element> ebml(const QByteArray &b) {
  QVector<Element> out;
  quint64 p = 0;
  while (p < quint64(b.size())) {
    auto id = vint(b, p), sz = vint(b, p + id.second);
    quint64 h = id.second + sz.second;
    out.push_back({b.mid(p, id.second).toHex().toUpper(),
                   b.mid(p + h, sz.first), p, h + sz.first, sz.first});
    if (!out.back().size)
      throw std::runtime_error("invalid element size");
    p += out.back().size;
  }
  return out;
}
QJsonValue stringOrNull(QByteArray b, bool language = false) {
  return b.isEmpty() || (language && b == "und")
             ? QJsonValue()
             : QJsonValue(QString::fromUtf8(b));
}
QJsonArray mkv(const QByteArray &b) {
  auto seg = find(ebml(b), "18538067");
  if (seg.name.isEmpty())
    throw std::runtime_error("missing Segment");
  auto ts = find(ebml(seg.data), "1654AE6B");
  if (ts.name.isEmpty())
    throw std::runtime_error("missing Tracks");
  QJsonArray out;
  for (auto e : ebml(ts.data)) {
    if (e.name != "AE")
      continue;
    auto fields = ebml(e.data);
    auto num = find(fields, "D7"), type = find(fields, "83");
    auto lang = stringOrNull(find(fields, "22B59C").data, true);
    if (lang.isNull())
      lang = stringOrNull(find(fields, "22B59D").data, true);
    auto codec = stringOrNull(find(fields, "86").data);
    if (codec.isString()) {
      auto s = codec.toString();
      for (auto prefix : {"V_", "A_", "S_"}) {
        auto p = s.indexOf(prefix);
        if (p >= 0)
          s.remove(p, 2);
      }
      codec = s;
    }
    QJsonObject row{
        {"id", num.name.isEmpty()
                   ? QJsonValue()
                   : QJsonValue(double(be(num.data, 0, num.data.size())))},
        {"lang", lang},
        {"label", stringOrNull(find(fields, "536E").data)},
        {"codec", codec}};
    if (type.name.isEmpty())
      row["type"] = QJsonValue();
    else {
      auto t = be(type.data, 0, type.data.size());
      if (t == 1)
        row["type"] = "video";
      else if (t == 2)
        row["type"] = "audio";
      else if (t == 17)
        row["type"] = "text";
    }
    out.append(row);
  }
  return out;
}
QJsonArray mp4(const QByteArray &data) {
  QJsonArray out;
  for (auto e : boxes(data)) {
    if (e.name != "trak")
      continue;
    auto children = boxes(e.data);
    auto tkhd = find(children, "tkhd"), mdia = find(children, "mdia");
    if (tkhd.name.isEmpty() || mdia.name.isEmpty())
      continue;
    auto media = boxes(mdia.data);
    auto mdhd = find(media, "mdhd"), hdlr = find(media, "hdlr"),
         minf = find(media, "minf");
    if (mdhd.name.isEmpty() || hdlr.name.isEmpty() || minf.name.isEmpty())
      continue;
    auto stbl = find(boxes(minf.data), "stbl");
    if (stbl.name.isEmpty())
      continue;
    auto stsd = find(boxes(stbl.data), "stsd");
    if (stsd.name.isEmpty())
      continue;
    auto lang = be(mdhd.data, be(mdhd.data, 0, 1) == 1 ? 32 : 20, 2);
    QString language;
    for (int shift : {10, 5, 0})
      language += QChar(int(((lang >> shift) & 31) + 96));
    auto handler = hdlr.data.mid(8, 4);
    auto name = QString::fromUtf8(
        hdlr.data.mid(24, qMax(qsizetype(0), hdlr.data.size() - 25)));
    if (name == "VideoHandler" || name == "SoundHandler" ||
        name == "SubtitleHandler")
      name.clear();
    auto samples = be(stsd.data, 4, 4);
    if (!samples || stsd.data.size() < 16)
      throw std::runtime_error("missing sample entry");
    QString codec = QString::fromUtf8(stsd.data.mid(12, 4));
    auto hyphen = codec.indexOf('-');
    if (hyphen >= 0)
      codec.remove(hyphen, 1);
    QJsonValue type;
    if (handler == "vide")
      type = "video";
    else if (handler == "soun")
      type = "audio";
    else if (handler == "text" || handler == "sbtl")
      type = "text";
    out.append(QJsonObject{
        {"id", double(be(tkhd.data, 12, 4))},
        {"type", type},
        {"lang", language == "und" || language == "```" ? QJsonValue()
                                                        : QJsonValue(language)},
        {"label", name.isEmpty() ? QJsonValue() : QJsonValue(name)},
        {"codec", codec.toUpper()}});
  }
  return out;
}
} // namespace
TrackResult extractTrackBytes(const QByteArray &data, bool continuation) {
  TrackResult r;
  try {
    if (data.startsWith(QByteArray::fromHex("1a45dfa3")))
      r.tracks = mkv(data);
    else if (continuation || data.mid(4, 4) == "ftyp") {
      auto all = boxes(data);
      auto moov = find(all, "moov");
      if (moov.name.isEmpty()) {
        for (auto e : all)
          if (e.name != "moov")
            r.nextOffset = e.offset + e.size;
        r.incomplete = true;
        r.error = "Incomplete media metadata";
      } else if (quint64(moov.data.size()) != moov.dataSize) {
        r.incomplete = true;
        r.error = "Incomplete media metadata";
        r.nextOffset = moov.offset;
        r.nextSize = moov.size;
      } else
        r.tracks = mp4(moov.data);
    } else
      r.error = "This file type is not supported";
  } catch (const std::exception &) {
    r.error = "Failed to decode buffer";
    r.incomplete = true;
  }
  return r;
}
QObject *extractMediaTracks(const QString &path,
                            std::function<void(TrackResult)> cb,
                            QObject *parent, qint64 limit) {
  auto *job = new QObject(parent);
  QPointer<QObject> safe(job);
  struct State {
    qint64 offset = 0, size = 65536, total = 0, length = 65536;
    bool mp4 = false;
    QString path;
  };
  auto st = std::make_shared<State>();
  st->path = path;
  auto step = std::make_shared<std::function<void()>>();
  std::weak_ptr<std::function<void()>> weak = step;
  *step = [=] {
    auto keep = weak.lock();
    auto receive = [=](ByteResult b) {
      if (!b.error.isEmpty()) {
        cb({{}, b.error});
        if (safe)
          safe->deleteLater();
        return;
      }
      st->total += b.data.size();
      if (limit && st->total >= limit) {
        cb({{}, "Reached maxBytesLimit of " + QString::number(limit), true});
        if (safe)
          safe->deleteLater();
        return;
      }
      auto r = extractTrackBytes(b.data, st->mp4);
      st->mp4 = st->mp4 || b.data.mid(4, 4) == "ftyp";
      if (r.nextOffset >= 0) {
        auto next = st->offset + r.nextOffset;
        if (next >= st->length || b.data.isEmpty() ||
            (next == st->offset && st->size == r.nextSize)) {
          cb(r);
          if (safe)
            safe->deleteLater();
          return;
        }
        st->offset = next;
        st->size = r.nextSize;
        (*keep)();
        return;
      }
      cb(r);
      if (safe)
        safe->deleteLater();
    };
    if (!st->path.startsWith("http://") && !st->path.startsWith("https://")) {
      QTimer::singleShot(0, job, [=] {
        QFile f(st->path);
        ByteResult b;
        if (!f.open(QIODevice::ReadOnly))
          b.error = f.errorString();
        else {
          st->length = f.size();
          f.seek(st->offset);
          b.data = f.read(st->size);
        }
        receive(b);
      });
    } else {
      auto end = qMin(st->length, st->offset + st->size) - 1;
      mediaRequest(
          job, st->path, "GET",
          {{"range", "bytes=" + QByteArray::number(st->offset) + "-" +
                         QByteArray::number(end)}},
          [=](ByteResult b) {
            if (b.error.isEmpty()) {
              auto h = b.headers.value("content-range");
              auto m = QRegularExpression("bytes ([0-9]+)-([0-9]+)/([0-9]+)")
                           .match(QString::fromLatin1(h));
              if (h.isEmpty())
                b.error = "Failed to retrieve Content-Range from headers";
              else if (!m.hasMatch())
                b.error = "Failed to parse range from Content-Range header";
              else {
                st->length = m.captured(3).toLongLong();
                st->path = b.url.toString();
              }
            }
            receive(b);
          },
          21);
    }
  };
  (*step)();
  return job;
}
} // namespace server1::media
