#include "server1/network/ProxyService.h"
#include "server1/http/HttpContract.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <brotli/decode.h>
#include <cstring>
#include <map>
#include <memory>
#include <zlib.h>

namespace server1::network {
namespace {
const QList<QByteArray> requestNames = {
    "accept",     "accept-encoding",   "accept-language",
    "connection", "transfer-encoding", "range",
    "if-range",   "user-agent"};
const QList<QByteArray> responseNames = {"accept-ranges",  "content-type",
                                         "content-length", "content-range",
                                         "connection",     "transfer-encoding",
                                         "last-modified",  "etag",
                                         "server",         "date"};
using Options = QList<QPair<QString, QString>>;
QString decode(QByteArray s) {
  s.replace('+', ' ');
  return QUrl::fromPercentEncoding(s);
}
Options parseOptions(const QString &s) {
  Options raw;
  QStringList keys;
  int count = 0;
  for (const auto &v : s.toUtf8().split('&')) {
    if (++count > 1000)
      break;
    if (v.isEmpty())
      continue;
    auto n = v.indexOf('=');
    auto key = decode(n < 0 ? v : v.left(n));
    if (!keys.contains(key))
      keys << key;
    raw.append({key, decode(n < 0 ? QByteArray() : v.mid(n + 1))});
  }
  // querystring.parse produces an object; stringify groups repeated keys.
  Options result;
  for (const auto &key : keys)
    for (const auto &v : raw)
      if (v.first == key)
        result << v;
  return result;
}
QString encode(const QString &s) {
  return QString::fromLatin1(QUrl::toPercentEncoding(s, "!~*'()"));
}
QString stringify(const Options &o) {
  QStringList parts;
  for (const auto &v : o)
    parts << encode(v.first) + "=" + encode(v.second);
  return parts.join('&');
}
QPair<QByteArray, QByteArray> header(const QString &s) {
  auto n = s.indexOf(':');
  return {(n < 0 ? s : s.left(n)).toLatin1(),
          (n < 0 ? QString() : s.mid(n + 1)).trimmed().toLatin1()};
}
class Job final : public QObject {
public:
  QPointer<QNetworkAccessManager> manager;
  QPointer<QNetworkReply> reply;
  void *deferred = nullptr, *wake = nullptr;
  QUrl destination, initialDestination;
  QByteArray method;
  Options options;
  QMap<QByteArray, QByteArray> headers;
  QString virtualRoot;
  int redirects = 0;
  bool published = false, playlist = false, finished = false, failed = false;
  QByteArray pending, partial,
      eol; // upstream reader is backpressured at 64 KiB
  ~Job() override {
    if (inflateStarted)
      inflateEnd(&inflater);
    if (brotli)
      BrotliDecoderDestroyInstance(brotli);
    if (reply) {
      reply->disconnect(this);
      reply->abort();
      reply->deleteLater();
    }
    if (wake)
      server1_http_stream_wake_destroy(wake);
    if (deferred)
      server1_http_deferred_destroy(deferred);
  }
  void overrides() {
    for (const auto &o : options)
      if (o.first == "h") {
        auto h = header(o.second);
        headers[h.first.toLower()] = h.second;
      }
  }
  void start() {
    finished = false;
    if (!manager) {
      error("Network service stopped");
      return;
    }
    if (destination.scheme() == "http") {
      error("Protocol \"http:\" not supported. Expected \"https:\"");
      return;
    }
    if (destination.scheme() != "https" || destination.host().isEmpty()) {
      error(destination.scheme().isEmpty() || destination.host().isEmpty()
                ? "Only absolute URLs are supported"
                : "Only HTTP(S) protocols are supported");
      return;
    }
    QNetworkRequest req(destination);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
                     QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    for (auto i = headers.cbegin(); i != headers.cend(); ++i)
      req.setRawHeader(i.key(), i.value());
    if (!headers.contains("accept"))
      req.setRawHeader("accept", "*/*");
    if (!headers.contains("user-agent"))
      req.setRawHeader(
          "user-agent",
          "node-fetch/1.0 (+https://github.com/bitinn/node-fetch)");
    if (!headers.contains("accept-encoding"))
      req.setRawHeader("accept-encoding", "gzip,deflate");
    if (method == "POST" || method == "PUT")
      req.setRawHeader("content-length", "0");
    reply = manager->sendCustomRequest(req, method);
    reply->setReadBufferSize(65536);
    connect(reply, &QNetworkReply::metaDataChanged, this,
            [this] { metadata(); });
    connect(reply, &QNetworkReply::readyRead, this, [this] {
      QPointer<Job> guard(this);
      if (!published)
        metadata();
      if (!guard)
        return;
      if (wake)
        server1_http_stream_wake_notify(wake);
    });
    connect(reply, &QNetworkReply::finished, this, [this] {
      finished = true;
      QPointer<Job> guard(this);
      if (!published)
        metadata();
      if (!guard || !reply)
        return;
      finished = true;
      if (!published) {
        if (reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >
                0)
          return;
        error(fetchError());
        return;
      }
      failed = reply->error() != QNetworkReply::NoError &&
               reply->error() < QNetworkReply::ContentAccessDenied;
      if (wake)
        server1_http_stream_wake_notify(wake);
    });
  }
  QByteArray fetchError() const {
    QByteArray reason =
        reply ? reply->errorString().toUtf8() : QByteArray("socket hang up");
    if (reply) {
      switch (reply->error()) {
      case QNetworkReply::ConnectionRefusedError:
        reason = "connect ECONNREFUSED " + destination.host().toUtf8() + ":" +
                 QByteArray::number(destination.port(443));
        break;
      case QNetworkReply::HostNotFoundError:
        reason = "getaddrinfo ENOTFOUND " + destination.host().toUtf8();
        break;
      case QNetworkReply::RemoteHostClosedError:
        reason = "socket hang up";
        break;
      default:
        break;
      }
    }
    return "request to " + destination.toEncoded() +
           " failed, reason: " + reason;
  }
  void error(const QByteArray &message) {
    auto r = server1_http_deferred_response(deferred);
    if (!r)
      return;
    server1_http_response_set_status(r, 500);
    server1_http_response_set_body(r, message.constData(), message.size());
    server1_http_deferred_complete(deferred);
  }
  void metadata() {
    if (published || !reply)
      return;
    int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!status)
      return;
    if (status >= 300 && status < 400 && reply->hasRawHeader("location")) {
      auto location = QString::fromUtf8(reply->rawHeader("location"));
      auto old = reply;
      reply = nullptr;
      old->disconnect(this);
      old->abort();
      old->deleteLater();
      if (++redirects >= 5) {
        error("Too many redirects");
        return;
      }
      // Bundled node-fetch resolves Location against its request URL before
      // M803 sees it.
      destination = destination.resolved(QUrl(location));
      for (auto i = headers.begin(); i != headers.end();)
        if (!requestNames.contains(i.key()))
          i = headers.erase(i);
        else
          ++i;
      headers["host"] = destination.authority(QUrl::FullyEncoded).toLatin1();
      overrides();
      start();
      return;
    }
    auto r = server1_http_deferred_response(deferred);
    if (!r)
      return;
    auto coding = reply->rawHeader("content-encoding");
    if (method != "HEAD" && status != 204 && status != 304) {
      if (coding == "gzip" || coding == "x-gzip")
        compression = 1;
      else if (coding == "deflate" || coding == "x-deflate")
        compression = 2;
      else if (coding == "br")
        compression = 3;
    }
    QMap<QByteArray, QByteArray> out;
    for (const auto &h : responseNames)
      if (reply->hasRawHeader(h))
        out[h] = reply->rawHeader(h);
    for (const auto &o : options)
      if (o.first == "r") {
        auto h = header(o.second);
        out[h.first] = h.second;
      }
    playlist = initialDestination.path().endsWith(".m3u") ||
               initialDestination.path().endsWith(".m3u8") ||
               out.value("content-type").toLower().contains("mpegurl");
    if (playlist) {
      out.remove("content-length");
      out["accept-ranges"] = "none";
      if (!out.value("transfer-encoding").toLower().contains("chunked")) {
        if (!out.value("transfer-encoding").isEmpty())
          out["transfer-encoding"] += ", ";
        out["transfer-encoding"] += "chunked";
      }
    }
    // Node writeHead is lazy: a zero-output decoder failure must not expose
    // headers. Prime bounded transformed output before releasing metadata.
    if (method != "HEAD" && (compression || playlist)) {
      pump();
      if (pending.isEmpty() && outputRead == outputSpill.size()) {
        if (codecStalled)
          return;
        if (inputAvailable()) {
          QTimer::singleShot(0, this, [this] { metadata(); });
          return;
        }
        if (!finished && !decodeDone)
          return;
      }
    }
    server1_http_response_set_status(r, status);
    for (auto i = out.cbegin(); i != out.cend(); ++i)
      server1_http_response_set_header(r, i.key().constData(),
                                       i.value().constData());
    bool ok = false;
    auto length = out.value("content-length").toULongLong(&ok);
    auto size = ok && !playlist ? std::size_t(length)
                                : SERVER1_HTTP_UNKNOWN_CONTENT_LENGTH;
    server1_http_response_set_async_stream(
        r,
        [](void *p, void *b, std::size_t n) {
          return static_cast<Job *>(p)->read(b, n);
        },
        nullptr, this, size);
    wake = server1_http_response_stream_wake_handle(r);
    published = true;
    server1_http_deferred_complete(deferred);
  }
  QByteArray rewriteUrl(const QByteArray &line) {
    QString s = QString::fromUtf8(line), result;
    if (s.startsWith("http://") || s.startsWith("https://")) {
      QUrl u(s);
      // Legacy Node URL keeps UTF-8 path text and null search is
      // string-concatenated.
      auto match =
          QRegularExpression("^https?://[^/?#]*([^?#]*)(\\?[^#]*)?").match(s);
      QString path = match.captured(1);
      if (path.isEmpty())
        path = "/";
      QString query = match.capturedStart(2) < 0 ? "null" : match.captured(2);
      for (const QChar ch : QString("<>\"` \r\n\t{}|\\^'")) {
        path.replace(ch,
                     QString::fromLatin1(QUrl::toPercentEncoding(QString(ch))));
        query.replace(
            ch, QString::fromLatin1(QUrl::toPercentEncoding(QString(ch))));
      }
      if (u.scheme() != initialDestination.scheme() ||
          u.host() != initialDestination.host() ||
          u.port() != initialDestination.port()) {
        // The source intentionally appends the explicit port twice (host
        // includes it).
        QString origin =
            u.scheme() + "://" + u.authority(QUrl::FullyEncoded) +
            (u.port() >= 0 ? ":" + QString::number(u.port()) : QString());
        Options o{{"d", origin}};
        for (const auto &v : options)
          if (v.first == "h")
            o << v;
        return ("/proxy/" + stringify(o) + path + query).toUtf8();
      }
      result = virtualRoot + "/" + path;
      result.replace(QRegularExpression("/+"), "/");
      return (result + query).toUtf8();
    }
    if (s.startsWith('/')) {
      result = virtualRoot + "/" + s;
      result.replace(QRegularExpression("/+"), "/");
      return result.toUtf8();
    }
    return line;
  }
  QByteArray rewrite(const QByteArray &line) {
    if (!line.startsWith('#') && !line.isEmpty())
      return rewriteUrl(line);
    auto match =
        QRegularExpression("URI=\"([^\"]+)\"").match(QString::fromUtf8(line));
    if (!match.hasMatch())
      return line;
    auto uri = match.captured(1).toUtf8();
    auto result = line;
    result.replace(result.indexOf(uri), uri.size(), rewriteUrl(uri));
    return result;
  }
  void enqueue(const QByteArray &bytes) {
    if (outputSpill.size() > outputRead ||
        pending.size() + bytes.size() > 65536)
      output(bytes);
    else
      pending += bytes;
  }

  // Oversized logical lines are spooled, not retained in the network queue.
  // Rewriting uses offset ranges and bounded reads, including a URI which spans
  // arbitrary upstream chunks. Spill output is drained before reading upstream.
  QTemporaryFile lineSpill, outputSpill;
  qint64 outputRead = 0;
  bool spillError = false;
  QByteArray range(qint64 at, qint64 n) {
    lineSpill.seek(at);
    return lineSpill.read(n);
  }
  qint64 find(const QByteArray &needle, qint64 at, qint64 end) {
    QByteArray tail;
    while (at < end) {
      auto b = range(at, qMin<qint64>(16384, end - at));
      if (b.isEmpty())
        break;
      auto data = tail + b;
      auto p = data.indexOf(needle);
      if (p >= 0)
        return at - tail.size() + p;
      tail = data.right(needle.size() - 1);
      at += b.size();
    }
    return -1;
  }
  void output(const QByteArray &b) {
    if (!outputSpill.isOpen() && !outputSpill.open()) {
      spillError = true;
      return;
    }
    outputSpill.seek(outputSpill.size());
    if (outputSpill.write(b) != b.size())
      spillError = true;
  }
  void copy(qint64 begin, qint64 end, bool collapse = false,
            bool escape = false) {
    bool slash = false;
    while (begin < end) {
      auto b = range(begin, qMin<qint64>(16384, end - begin));
      if (b.isEmpty()) {
        spillError = true;
        break;
      }
      begin += b.size();
      QByteArray out;
      out.reserve(b.size());
      for (char ch : b) {
        if (collapse && ch == '/' && slash)
          continue;
        slash = ch == '/';
        if (escape && QByteArray("<>\"` \r\n\t{}|\\^'").contains(ch)) {
          out += '%';
          const char *hex = "0123456789ABCDEF";
          out += hex[(static_cast<unsigned char>(ch) >> 4) & 15];
          out += hex[static_cast<unsigned char>(ch) & 15];
        } else
          out += ch;
      }
      output(out);
    }
  }
  void rewriteRange(qint64 begin, qint64 end) {
    auto prefix = range(begin, qMin<qint64>(8, end - begin));
    if (prefix.startsWith("http://") || prefix.startsWith("https://")) {
      qint64 authorityEnd = begin + (prefix.startsWith("https://") ? 8 : 7);
      // A URL authority is short in valid HTTP URLs. Scan without accumulating
      // its path/query, which may legitimately be many megabytes long.
      while (authorityEnd < end) {
        auto b = range(authorityEnd, qMin<qint64>(16384, end - authorityEnd));
        int i = 0;
        while (i < b.size() && b[i] != '/' && b[i] != '?' && b[i] != '#')
          ++i;
        authorityEnd += i;
        if (i < b.size())
          break;
      }
      if (authorityEnd - begin > 65536) {
        spillError = true;
        return;
      } // invalid oversized authority
      auto authority = QString::fromUtf8(range(begin, authorityEnd - begin));
      QUrl u(authority);
      auto fragment = find("#", authorityEnd, end);
      if (fragment >= 0)
        end = fragment;
      auto question = find("?", authorityEnd, end);
      qint64 pathEnd = question < 0 ? end : question;
      bool cross = u.scheme() != initialDestination.scheme() ||
                   u.host() != initialDestination.host() ||
                   u.port() != initialDestination.port();
      if (cross) {
        QString origin =
            u.scheme() + "://" + u.authority(QUrl::FullyEncoded) +
            (u.port() >= 0 ? ":" + QString::number(u.port()) : QString());
        Options o{{"d", origin}};
        for (const auto &v : options)
          if (v.first == "h")
            o << v;
        output(("/proxy/" + stringify(o)).toUtf8());
      } else
        output(virtualRoot.toUtf8());
      if (authorityEnd == pathEnd)
        output("/");
      else
        copy(authorityEnd, pathEnd, !cross, true);
      if (question < 0)
        output("null");
      else
        copy(question, end, false, true);
    } else if (prefix.startsWith('/')) {
      output(virtualRoot.toUtf8());
      copy(begin, end, true);
    } else
      copy(begin, end);
  }
  void finishSpill(const QByteArray &ending) {
    lineSpill.flush();
    qint64 size = lineSpill.size();
    auto prefix = range(0, qMin<qint64>(1, size));
    if (prefix != "#")
      rewriteRange(0, size);
    else {
      auto start = find("URI=\"", 0, size);
      auto end = start < 0 ? -1 : find("\"", start + 5, size);
      while (start >= 0 && end == start + 5) {
        start = find("URI=\"", end + 1, size);
        end = start < 0 ? -1 : find("\"", start + 5, size);
      }
      if (start < 0 || end <= start + 5)
        copy(0, size);
      else {
        start += 5;
        auto length =
            end -
            start; // source replace() targets the first identical substring
        qint64 first = start;
        const auto initial = range(start, qMin<qint64>(64, length));
        qint64 candidate = 0;
        while ((candidate = find(initial, candidate,
                                 start + initial.size() - 1)) >= 0 &&
               candidate < start) {
          bool same = true;
          for (qint64 off = 0; off < length; off += 16384) {
            auto n = qMin<qint64>(16384, length - off);
            if (range(candidate + off, n) != range(start + off, n)) {
              same = false;
              break;
            }
          }
          if (same) {
            first = candidate;
            break;
          }
          ++candidate;
        }
        copy(0, first);
        rewriteRange(start, end);
        copy(first + length, size);
      }
    }
    output(ending);
    lineSpill.resize(0);
    lineSpill.seek(0);
  }
  void appendSpill(const QByteArray &data) {
    if (!lineSpill.isOpen() && !lineSpill.open()) {
      spillError = true;
      return;
    }
    lineSpill.seek(lineSpill.size());
    if (lineSpill.write(data) != data.size())
      spillError = true;
  }

  void transform(const QByteArray &bytes) {
    partial += QString::fromUtf8(bytes).toUtf8();
    if (eol.isEmpty()) {
      int lf = partial.indexOf('\n'), cr = partial.indexOf('\r');
      if (lf >= 0 && cr >= 0)
        eol = cr < lf ? "\r\n" : "\n\r";
      else if (lf >= 0)
        eol = "\n";
      else if (cr >= 0)
        eol = "\r";
    }
    const auto delimiter = eol.isEmpty() ? QByteArray("null") : eol;
    int i;
    while ((i = partial.indexOf(delimiter)) >= 0) {
      if (lineSpill.size() > 0) {
        appendSpill(partial.left(i));
        finishSpill(delimiter);
      } else
        enqueue(rewrite(partial.left(i)) + delimiter);
      partial.remove(0, i + delimiter.size());
    }
    if (partial.size() > 65536 || lineSpill.size() > 0) {
      auto keep = qMin<qsizetype>(delimiter.size() - 1, partial.size());
      appendSpill(partial.left(partial.size() - keep));
      partial = partial.right(keep);
    }
  }
  int compression = 0;
  z_stream inflater{};
  bool inflateStarted = false, decodeDone = false, decodeNeedsDrain = false,
       codecStalled = false;
  BrotliDecoderState *brotli = nullptr;
  QByteArray compressedInput;
  qsizetype compressedOffset = 0;
  bool inputAvailable() const {
    return reply && !decodeDone && !codecStalled &&
           (reply->bytesAvailable() ||
            compressedOffset < compressedInput.size() || decodeNeedsDrain);
  }
  QByteArray decoded() {
    if (!compression)
      return reply->read(16384);
    if (decodeDone || codecStalled)
      return {};
    if (compressedOffset == compressedInput.size()) {
      compressedInput = reply->read(16384);
      compressedOffset = 0;
    }
    if (compressedInput.isEmpty() && !decodeNeedsDrain)
      return {};
    QByteArray out(16384, Qt::Uninitialized);
    if (compression == 3) {
      if (!brotli)
        brotli = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
      if (!brotli) {
        failed = true;
        codecStalled = true;
        return {};
      }
      size_t in = compressedInput.size() - compressedOffset, avail = out.size();
      const auto *src = reinterpret_cast<const uint8_t *>(
          compressedInput.constData() + compressedOffset);
      auto *dst = reinterpret_cast<uint8_t *>(out.data());
      auto result = BrotliDecoderDecompressStream(brotli, &in, &src, &avail,
                                                  &dst, nullptr);
      compressedOffset = compressedInput.size() - in;
      out.resize(out.size() - avail);
      decodeNeedsDrain = result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
      if (result == BROTLI_DECODER_RESULT_SUCCESS)
        decodeDone = true;
      else if (result == BROTLI_DECODER_RESULT_ERROR) {
        failed = true;
        codecStalled = true;
      }
    } else {
      if (!inflateStarted) {
        int bits =
            compression == 1
                ? 31
                : ((static_cast<unsigned char>(compressedInput[0]) & 15) == 8
                       ? 15
                       : -15);
        if (inflateInit2(&inflater, bits) != Z_OK) {
          failed = true;
          decodeDone = true;
          return {};
        }
        inflateStarted = true;
      }
      qsizetype produced = 0;
      do {
        inflater.next_in = reinterpret_cast<Bytef *>(compressedInput.data() +
                                                     compressedOffset);
        inflater.avail_in = compressedInput.size() - compressedOffset;
        inflater.next_out = reinterpret_cast<Bytef *>(out.data() + produced);
        inflater.avail_out = out.size() - produced;
        auto result = inflate(&inflater, Z_SYNC_FLUSH);
        compressedOffset = compressedInput.size() - inflater.avail_in;
        produced = out.size() - inflater.avail_out;
        decodeNeedsDrain = inflater.avail_out == 0;
        if (result == Z_STREAM_END) {
          if (compression == 1) {
            inflateReset2(&inflater, 31);
            decodeNeedsDrain = false;
            if (compressedOffset < compressedInput.size() &&
                compressedInput[compressedOffset] == 0) {
              decodeDone = true;
              break;
            }
            if (compressedOffset < compressedInput.size() &&
                produced < out.size())
              continue;
          } else
            decodeDone = true;
        } else if (result != Z_OK && result != Z_BUF_ERROR) {
          failed = true;
          codecStalled = true;
          // Like Node's zlib callback, don't publish the output of a failed
          // decode batch. Prior successful batches remain already published.
          produced = 0;
        }
        break;
      } while (true);
      out.resize(produced);
    }
    return out;
  }
  void pump() {
    if (pending.isEmpty() && outputRead == outputSpill.size() && reply) {
      if (playlist) {
        if (inputAvailable())
          transform(decoded());
        if (finished && !inputAvailable() &&
            (compression == 2 || compression == 3) && !decodeDone)
          codecStalled = true;
        if ((finished || decodeDone) && !inputAvailable() && !codecStalled) {
          if (lineSpill.size() > 0) {
            appendSpill(partial);
            finishSpill({});
          } else
            enqueue(rewrite(partial));
          partial.clear();
        }
      } else {
        pending = decoded();
        if (finished && !inputAvailable() &&
            (compression == 2 || compression == 3) && !decodeDone)
          codecStalled = true;
      }
    }
  }
  std::ptrdiff_t read(void *buffer, std::size_t capacity) {
    if (!capacity)
      return 0;
    if (pending.isEmpty() && outputRead < outputSpill.size()) {
      outputSpill.seek(outputRead);
      pending = outputSpill.read(qMin<std::size_t>(capacity, 65536));
      outputRead += pending.size();
      if (outputRead == outputSpill.size()) {
        outputSpill.resize(0);
        outputRead = 0;
      }
    }
    pump();
    if (!pending.isEmpty()) {
      auto n = qMin<std::size_t>(capacity, pending.size());
      std::memcpy(buffer, pending.constData(), n);
      pending.remove(0, n);
      return n;
    }
    if (spillError)
      return -1;
    if (outputSpill.size() > outputRead) {
      outputSpill.seek(outputRead);
      auto b = outputSpill.read(qMin<std::size_t>(capacity, 65536));
      outputRead += b.size();
      if (outputRead == outputSpill.size()) {
        outputSpill.resize(0);
        outputRead = 0;
      }
      std::memcpy(buffer, b.constData(), b.size());
      return b.size();
    }
    if (codecStalled)
      return SERVER1_HTTP_WOULD_BLOCK;
    if ((finished || decodeDone) && !inputAvailable())
      return failed ? -1 : 0;
    // A playlist chunk without a complete line must keep consuming available
    // input, rather than waiting for another readyRead which might never occur.
    if (inputAvailable())
      QTimer::singleShot(0, this, [this] {
        if (wake)
          server1_http_stream_wake_notify(wake);
      });
    return SERVER1_HTTP_WOULD_BLOCK;
  }
};
} // namespace
int startProxy(QNetworkAccessManager *manager, const void *request,
               void *response) {
  QByteArray target = server1_http_request_target(request);
  auto q = target.indexOf('?');
  auto rawPath = q < 0 ? target : target.left(q);
  auto tail = rawPath.mid(7);
  auto slash = tail.indexOf('/');
  if (tail.isEmpty())
    return 0;
  auto *job = new Job;
  job->manager = manager;
  QObject::connect(manager, &QObject::destroyed, job, [job] {
    job->finished = true;
    job->failed = true;
    job->codecStalled = false;
    job->decodeDone = true;
    if (!job->published)
      job->error("Network service stopped");
    else if (job->wake)
      server1_http_stream_wake_notify(job->wake);
  });
  job->options = parseOptions(
      QUrl::fromPercentEncoding(slash < 0 ? tail : tail.left(slash)));
  QString d;
  for (const auto &o : job->options)
    if (o.first == "d") {
      d = o.second;
      break;
    }
  job->destination = QUrl(d);
  job->destination.setPath(
      QUrl::fromPercentEncoding(slash < 0 ? QByteArray() : tail.mid(slash)),
      QUrl::TolerantMode);
  job->destination.setQuery(q < 0 ? QString()
                                  : QString::fromUtf8(target.mid(q + 1)));
  job->initialDestination = job->destination;
  job->method = server1_http_request_method(request);
  job->virtualRoot = "/proxy/" + stringify(job->options);
  job->headers["host"] =
      job->destination.authority(QUrl::FullyEncoded).toLatin1();
  for (const auto &h : requestNames) {
    auto count = server1_http_request_header_count(request, h.constData());
    QByteArray value;
    for (std::size_t i = 0; i < count; ++i) {
      if (i)
        value += ", ";
      value += server1_http_request_header_value_at(request, h.constData(), i);
    }
    if (count)
      job->headers[h] = value;
  }
  job->overrides();
  job->deferred = server1_http_response_defer(response);
  server1_http_response_set_lifetime(
      response, [](void *p) { delete static_cast<Job *>(p); }, job);
  QTimer::singleShot(0, job, [job] { job->start(); });
  return 1;
}
} // namespace server1::network
