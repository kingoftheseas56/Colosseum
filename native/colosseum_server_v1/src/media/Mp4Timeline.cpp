#include "server1/media/Mp4Timeline.h"
#include <QVector>
#include <QtEndian>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace server1::media {
namespace {
struct Box {
  qsizetype offset, size, header;
  QByteArray type;
  qsizetype payload() const { return offset + header; }
  qsizetype end() const { return offset + size; }
};
void need(const QByteArray &b, qsizetype p, qsizetype n, qsizetype end = -1) {
  if (end < 0)
    end = b.size();
  if (p < 0 || n < 0 || p > end || n > end - p)
    throw std::runtime_error("Truncated MP4 box");
}
quint32 u32(const QByteArray &b, qsizetype p, qsizetype end = -1) {
  need(b, p, 4, end);
  return qFromBigEndian<quint32>(b.constData() + p);
}
quint64 u64(const QByteArray &b, qsizetype p, qsizetype end = -1) {
  need(b, p, 8, end);
  return qFromBigEndian<quint64>(b.constData() + p);
}
QVector<Box> boxes(const QByteArray &b, qsizetype begin = 0,
                   qsizetype end = -1) {
  if (end < 0)
    end = b.size();
  QVector<Box> out;
  for (qsizetype p = begin; p < end;) {
    need(b, p, 8, end);
    quint64 size = u32(b, p, end);
    qsizetype header = 8;
    if (size == 1) {
      size = u64(b, p + 8, end);
      header = 16;
    } else if (size == 0)
      size = end - p;
    if (size < quint64(header) || size > quint64(end - p))
      throw std::runtime_error("Invalid or truncated MP4 box size");
    out.append({p, qsizetype(size), header, b.mid(p + 4, 4)});
    p += qsizetype(size);
  }
  return out;
}
} // namespace
std::optional<QByteArray> Mp4Timeline::takeUntil(QByteArray &pending,
                                                 const QByteArray &type,
                                                 bool finish) {
  qsizetype p = 0;
  while (p < pending.size()) {
    if (pending.size() - p < 8)
      break;
    quint64 size = u32(pending, p);
    qsizetype header = 8;
    if (size == 1) {
      if (pending.size() - p < 16)
        break;
      size = u64(pending, p + 8);
      header = 16;
    } else if (size == 0) {
      if (!finish)
        break;
      size = pending.size() - p;
    }
    if (size < quint64(header) ||
        size > quint64(std::numeric_limits<qsizetype>::max()))
      throw std::runtime_error("Invalid MP4 box size");
    if (size > quint64(pending.size() - p))
      break;
    if (pending.mid(p + 4, 4) == type) {
      auto result = pending.left(p + qsizetype(size));
      pending.remove(0, p + qsizetype(size));
      return result;
    }
    p += qsizetype(size);
  }
  if (finish)
    throw std::runtime_error("Truncated MP4 stream or missing target box");
  return std::nullopt;
}
Mp4SegmentTiming Mp4Timeline::sidx(const QByteArray &bytes) {
  for (const auto &b : boxes(bytes))
    if (b.type == "sidx") {
      const auto p = b.payload();
      need(bytes, p, 12, b.end());
      const auto version = quint8(bytes[p]);
      if (version > 1)
        throw std::runtime_error("Unsupported sidx version");
      Mp4SegmentTiming t;
      t.timescale = u32(bytes, p + 8, b.end());
      if (!t.timescale)
        throw std::runtime_error("Invalid sidx timescale");
      t.earliestPresentationTime =
          version ? u64(bytes, p + 12, b.end()) : u32(bytes, p + 12, b.end());
      qsizetype cursor = p + (version ? 28 : 20);
      need(bytes, cursor, 4, b.end());
      quint16 count = qFromBigEndian<quint16>(bytes.constData() + cursor + 2);
      cursor += 4;
      need(bytes, cursor, qsizetype(count) * 12, b.end());
      for (int i = 0; i < count; ++i) {
        const auto d = u32(bytes, cursor + 4, b.end());
        if (t.duration > std::numeric_limits<quint64>::max() - d)
          throw std::overflow_error("sidx duration overflow");
        t.duration += d;
        cursor += 12;
      }
      return t;
    }
  throw std::runtime_error("Missing sidx timing");
}
QMap<quint32, quint32> Mp4Timeline::timescales(const QByteArray &bytes) {
  QMap<quint32, quint32> result;
  for (const auto &moov : boxes(bytes))
    if (moov.type == "moov")
      for (const auto &trak : boxes(bytes, moov.payload(), moov.end()))
        if (trak.type == "trak") {
          std::optional<quint32> track, scale;
          for (const auto &b : boxes(bytes, trak.payload(), trak.end())) {
            if (b.type == "tkhd") {
              need(bytes, b.payload(), 1, b.end());
              auto v = quint8(bytes[b.payload()]);
              if (v > 1)
                throw std::runtime_error("Unsupported tkhd version");
              track = u32(bytes, b.payload() + (v ? 20 : 12), b.end());
            }
            if (b.type == "mdia")
              for (const auto &m : boxes(bytes, b.payload(), b.end()))
                if (m.type == "mdhd") {
                  need(bytes, m.payload(), 1, m.end());
                  auto v = quint8(bytes[m.payload()]);
                  if (v > 1)
                    throw std::runtime_error("Unsupported mdhd version");
                  scale = u32(bytes, m.payload() + (v ? 20 : 12), m.end());
                }
          }
          if (track && scale) {
            if (!*scale)
              throw std::runtime_error("Invalid MP4 timescale");
            result[*track] = *scale;
          }
        }
  return result;
}
bool Mp4Timeline::updateBaseMediaDecodeTime(QByteArray &bytes, double value,
                                            quint32 trackId) {
  for (const auto &moof : boxes(bytes))
    if (moof.type == "moof") {
      for (const auto &traf : boxes(bytes, moof.payload(), moof.end()))
        if (traf.type == "traf") {
          const auto children = boxes(bytes, traf.payload(), traf.end());
          bool matches = false;
          for (const auto &b : children)
            if (b.type == "tfhd" &&
                u32(bytes, b.payload() + 4, b.end()) == trackId)
              matches = true;
          if (!matches)
            continue;
          for (const auto &b : children)
            if (b.type == "tfdt") {
              // M922 writes the version before converting/writing the
              // timestamp. Bounds are against the containing Buffer, not the
              // logical box: this intentionally preserves its version-0
              // overwrite behavior.
              need(bytes, b.payload(), 1);
              bytes[b.payload()] = 1;
              value = value < 0 ? 0 : value;
              if (!std::isfinite(value) || std::trunc(value) != value ||
                  value >= 18446744073709551616.0)
                throw std::invalid_argument("Invalid base media decode time");
              need(bytes, b.payload() + 4, 8);
              qToBigEndian<quint64>(quint64(value),
                                    bytes.data() + b.payload() + 4);
              return true;
            }
          return false;
        }
      return false;
    }
  return false;
}
} // namespace server1::media
