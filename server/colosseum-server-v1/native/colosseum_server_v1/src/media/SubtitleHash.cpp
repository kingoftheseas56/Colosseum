#include "server1/media/SubtitleHash.h"
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <QtEndian>
namespace server1::media {
static quint64 checksum(QByteArray b) {
  b = b.left(65536);
  if (b.size() < 65536)
    b.append(QByteArray(65536 - b.size(), 0));
  quint64 sum = 0;
  for (int i = 0; i < 65536; i += 8)
    sum += qFromLittleEndian<quint64>((const uchar *)b.constData() + i);
  return sum;
}
static QString hash(qint64 size, const QByteArray &first,
                    const QByteArray &last) {
  return QString::number(quint64(size) + checksum(first) + checksum(last), 16)
      .rightJustified(16, '0');
}
QString subtitleHashBytes(const QByteArray &bytes) {
  return hash(bytes.size(), bytes.left(65536),
              bytes.size() >= 65536 ? bytes.right(65536) : QByteArray());
}
QObject *calculateSubtitleHash(const QString &url,
                               std::function<void(HashResult)> cb,
                               QObject *parent) {
  auto *job = new QObject(parent);
  QPointer<QObject> safe(job);
  auto finish = [=](HashResult r) {
    cb(r);
    if (safe)
      safe->deleteLater();
  };
  if (url.startsWith("file:")) {
    QTimer::singleShot(0, job, [=] {
      QFile f(QUrl(url).toLocalFile());
      if (!f.open(QIODevice::ReadOnly)) {
        finish({{}, f.errorString()});
        return;
      }
      auto size = f.size();
      auto first = f.read(131072);
      if (size < 65535) {
        finish({{},
                "The value of \"position\" is out of range. It must be >= -1. "
                "Received " +
                    QString::number(size - 65536)});
        return;
      }
      // Node fs.read position -1 uses the current descriptor position (zero
      // here).
      if (!f.seek(size == 65535 ? 0 : size - 65536)) {
        finish({{}, f.errorString()});
        return;
      }
      finish({{{"size", double(size)},
               {"hash", hash(size, first, f.read(131072))}},
              {}});
    });
    return job;
  }
  if (!url.startsWith("http:") && !url.startsWith("https:")) {
    QTimer::singleShot(0, job, [=] {
      finish({{},
              url.isNull() ? "url required"
                           : "args.url must begin with http or file"});
    });
    return job;
  }
  mediaRequest(
      job, url, "HEAD", {},
      [=](ByteResult head) {
        if (!head.error.isEmpty()) {
          finish({{}, head.error});
          return;
        }
        if (head.status < 200 || head.status >= 300) {
          finish({{}, "Failed to fetch Media URL: " + head.statusText});
          return;
        }
        bool ok = false;
        auto size = head.headers.value("content-length").toLongLong(&ok);
        if (!ok) {
          finish({{}, "Failed to get content length from response headers"});
          return;
        }
        auto range = [=](qint64 start, qint64 end, ByteCallback c) {
          mediaRequest(
              job, head.url.toString(), "GET",
              {{"range", "bytes=" + QByteArray::number(start) + "-" +
                             QByteArray::number(end)},
               {"enginefs-prio", "10"}},
              [=](ByteResult r) {
                if (r.error.isEmpty() && (r.status < 200 || r.status >= 300))
                  r.error = "Failed to fetch chunk data for range: " +
                            QString::number(start) + "-" +
                            QString::number(end) + " - " + r.statusText;
                else if (r.error.isEmpty() && r.data.size() != 65536)
                  r.error =
                      "Response for calculating movie hash has wrong length";
                c(r);
              },
              20);
        };
        range(0, 65535, [=](ByteResult first) {
          if (!first.error.isEmpty()) {
            finish({{}, first.error});
            return;
          }
          range(size - 65536, size - 1, [=](ByteResult last) {
            if (!last.error.isEmpty()) {
              finish({{}, last.error});
              return;
            }
            finish({{{"size", double(size)},
                     {"hash", hash(size, first.data, last.data)}},
                    {}});
          });
        });
      },
      20);
  return job;
}
} // namespace server1::media
