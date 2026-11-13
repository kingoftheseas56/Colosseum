#include "server1/casting/CastingService.h"
#include "server1/http/HttpContract.h"
#include "server1/media/SubtitleFetch.h"
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cstring>
namespace server1::casting {
namespace {
void body(void *res, int status, const QByteArray &text,
          const char *type = "text/plain; charset=utf8") {
  server1_http_response_set_status(res, status);
  server1_http_response_set_header(res, "Content-Type", type);
  server1_http_response_set_body(res, text.data(), text.size());
}
struct Pending {
  QPointer<CastingService> service;
  void *deferred = nullptr;
  quint64 probe = 0;
  QPointer<QObject> life;
  QTemporaryDir temporary;
  QString subtitleFile;
  bool completed = false;
  ~Pending() {
    delete life.data();
    if (service && probe)
      (void)service->processes().cancel(probe);
    server1_http_deferred_destroy(deferred);
  }
};
struct Stream {
  QPointer<CastingService> service;
  quint64 process = 0;
  QByteArray bytes;
  bool done = false, failed = false;
  void *wake = nullptr;
  ~Stream() {
    if (service)
      service->adjustBufferedBytes(-bytes.size());
    if (service && process)
      (void)service->processes().cancel(process);
    server1_http_stream_wake_destroy(wake);
  }
  std::ptrdiff_t read(void *out, size_t capacity) {
    if (!capacity)
      return SERVER1_HTTP_WOULD_BLOCK;
    if (!bytes.isEmpty()) {
      auto n = std::min<size_t>(capacity, bytes.size());
      std::memcpy(out, bytes.constData(), n);
      bytes.remove(0, n);
      if (service)
        service->adjustBufferedBytes(-qsizetype(n));
      if (service && bytes.size() < 65536)
        (void)service->processes().resumeOutput(process);
      return n;
    }
    return !service || failed ? -1 : done ? 0 : SERVER1_HTTP_WOULD_BLOCK;
  }
};
struct Route {
  QPointer<CastingService> service;
  int kind;
};
int handle(const void *req, void *res, server1_http_next_fn, void *,
           void *ctx) {
  const auto route = *static_cast<Route *>(ctx);
  if (!route.service) {
    body(res, 503, "Casting unavailable");
    return 1;
  }
  auto &service = *route.service;
  if (route.kind == 0) {
    body(res, 200,
         QJsonDocument(service.devices()).toJson(QJsonDocument::Compact),
         "application/json; charset=utf8");
    return 1;
  }
  if (route.kind >= 2) {
    const auto id = server1_http_request_param(req, "devID");
    auto device = service.device(QString::fromUtf8(id ? id : ""));
    if (device.isEmpty())
      body(res, 404, "Device not found");
    else if (route.kind == 3) {
      if (!service.hasPlayerInvoker()) {
        body(res, 501, "Active device control is not enabled");
        return 1;
      }
      QJsonObject query;
      QUrl url(QString::fromUtf8(server1_http_request_target(req)));
      QUrlQuery parsed(url.query(QUrl::FullyEncoded).replace('+', ' '));
      auto add = [](QJsonObject &object, const QString &key,
                    const QString &value) {
        if (!object.contains(key))
          object[key] = value;
        else {
          auto values = object.value(key).isArray()
                            ? object.value(key).toArray()
                            : QJsonArray{object.value(key)};
          values.append(value);
          object[key] = values;
        }
      };
      for (const auto &item : parsed.queryItems(QUrl::FullyDecoded))
        add(query, item.first, item.second);
      auto input = QByteArray(server1_http_request_body(req),
                              server1_http_request_body_size(req));
      auto form = QJsonDocument::fromJson(input).object();
      if (!input.isEmpty() && form.isEmpty()) {
        QUrlQuery fields(QString::fromUtf8(input).replace('+', ' '));
        for (const auto &item : fields.queryItems(QUrl::FullyDecoded))
          add(form, item.first, item.second);
      }
      struct Deferred {
        void *handle;
        ~Deferred() { server1_http_deferred_destroy(handle); }
      };
      auto deferred = std::make_shared<Deferred>();
      deferred->handle = server1_http_response_defer(res);
      service.invokePlayer(
          QString::fromUtf8(id), query, form, [deferred](QJsonValue result) {
            if (auto *r = server1_http_deferred_response(deferred->handle)) {
              QByteArray bytes;
              if (result.isUndefined() || result.isNull() ||
                  result == QJsonValue(false) || result == QJsonValue(0) ||
                  result == QJsonValue(""))
                result = QJsonObject{};
              if (result.isObject())
                bytes = QJsonDocument(result.toObject())
                            .toJson(QJsonDocument::Compact);
              else if (result.isArray())
                bytes = QJsonDocument(result.toArray())
                            .toJson(QJsonDocument::Compact);
              else {
                bytes = QJsonDocument(QJsonArray{result})
                            .toJson(QJsonDocument::Compact);
                bytes = bytes.mid(1, bytes.size() - 2);
              }
              body(r, 200, bytes, "application/json; charset=utf8");
              server1_http_deferred_complete(deferred->handle);
            }
          });
    } else
      body(res, 200, QJsonDocument(device).toJson(QJsonDocument::Compact),
           "application/json; charset=utf8");
    return 1;
  }
  QJsonObject q;
  for (const auto *key : {"video", "fmp4", "audioTrack", "time", "subtitles",
                          "subtitlesDelay", "flagRe"}) {
    auto value = server1_http_request_query_value(req, key);
    if (value)
      q[key] = QString::fromUtf8(value);
  }
  if (q.value("video").toString().isEmpty()) {
    body(res, 400, "provide ?video");
    return 1;
  }
  auto url = QUrl(q.value("video").toString());
  if (!QStringList{"file", "http", "https"}.contains(url.scheme())) {
    body(res, 400, "Unsupported video URL scheme");
    return 1;
  }
  auto pending = std::make_shared<Pending>();
  pending->service = &service;
  pending->life = new QObject(&service);
  pending->deferred = server1_http_response_defer(res);
  auto *owner = new std::shared_ptr<Pending>(pending);
  if (!server1_http_response_set_lifetime(
          res,
          [](void *p) { delete static_cast<std::shared_ptr<Pending> *>(p); },
          owner)) {
    delete owner;
    body(res, 500, "Casting lifetime unavailable");
    server1_http_deferred_complete(pending->deferred);
    return 1;
  }
  const bool head = QByteArray(server1_http_request_method(req)) == "HEAD";
  const auto *mediaHeader =
      server1_http_request_header_value_at(req, "getmediainfo.sec", 0);
  const bool mediaInfo = mediaHeader && *mediaHeader;
  std::weak_ptr<Pending> weak = pending;
  media::ProcessCallbacks cb;
  cb.onFinished = [weak, q, head,
                   mediaInfo](const media::ProcessResult &result) {
    auto p = weak.lock();
    if (!p || !p->service || p->completed)
      return;
    p->probe = 0;
    p->completed = true;
    auto *response = server1_http_deferred_response(p->deferred);
    if (!response)
      return;
    if (result.stderrData.isEmpty() || !result.started) {
      body(response, 200,
           result.errorString.isEmpty() ? QByteArray("No data")
                                        : result.errorString.toUtf8());
      server1_http_deferred_complete(p->deferred);
      return;
    }
    auto info = CastingService::videoInfo(result.stderrData);
    const bool mp4 = !q.value("fmp4").toString().isEmpty();
    server1_http_response_set_header(response, "Content-Type",
                                     mp4 ? "video/mp4" : "video/x-mkv");
    server1_http_response_set_header(response, "Transfer-Encoding", "chunked");
    server1_http_response_set_header(response, "Accept-Ranges", "none");
    server1_http_response_set_header(response, "Connection", "Close");
    server1_http_response_set_close(response, 1);
    server1_http_response_set_header(response, "transferMode.dlna.org",
                                     "Streaming");
    server1_http_response_set_header(response, "contentFeatures.dlna.org",
                                     "DLNA.ORG_OP=01;DLNA.ORG_CI=1;DLNA.ORG_"
                                     "FLAGS=01300000000000000000000000000000");
    if (mediaInfo) {
      static QRegularExpression re("^\\s*([+-]?\\d+)");
      auto m = re.match(q.value("time").toString());
      double duration = info.value("duration").toDouble();
      if (duration)
        duration -= m.hasMatch() ? m.captured(1).toInt() : 0;
      auto value =
          "SEC_Duration=" + QByteArray::number(duration * 1000, 'g', 16) + ";";
      server1_http_response_set_header(response, "MediaInfo.sec",
                                       value.constData());
    }
    if (!head) {
      auto stream = std::make_shared<Stream>();
      stream->service = p->service;
      auto *holder = new std::shared_ptr<Stream>(stream);
      if (!server1_http_response_set_async_stream(
              response,
              [](void *c, void *b, size_t n) {
                return (*static_cast<std::shared_ptr<Stream> *>(c))->read(b, n);
              },
              [](void *c) { delete static_cast<std::shared_ptr<Stream> *>(c); },
              holder, SERVER1_HTTP_UNKNOWN_CONTENT_LENGTH)) {
        delete holder;
        body(response, 500, "Casting stream unavailable");
        server1_http_deferred_complete(p->deferred);
        return;
      }
      stream->wake = server1_http_response_stream_wake_handle(response);
      std::weak_ptr<Stream> weakStream = stream;
      media::ProcessCallbacks callbacks;
      callbacks.onStdout = [weakStream](const QByteArray &bytes) {
        auto s = weakStream.lock();
        if (!s)
          return;
        s->bytes += bytes;
        if (s->service)
          s->service->adjustBufferedBytes(bytes.size());
        if (s->service && s->bytes.size() >= 131072)
          (void)s->service->processes().pauseOutput(s->process);
        server1_http_stream_wake_notify(s->wake);
      };
      callbacks.onFinished = [weakStream](const media::ProcessResult &r) {
        if (auto s = weakStream.lock()) {
          s->done = true;
          s->failed = !r.started || r.exitCode != 0;
          s->process = 0;
          server1_http_stream_wake_notify(s->wake);
        }
      };
      media::ProcessSpec spec(
          p->service->executable(),
          CastingService::arguments(q, info, p->subtitleFile));
      spec.backpressuredOutput = true;
      stream->process =
          p->service->processes().start(spec, std::move(callbacks));
    }
    server1_http_deferred_complete(p->deferred);
  };
  QString input =
      url.isLocalFile() ? url.toLocalFile() : q.value("video").toString();
  auto startProbe = [weak, input, cb]() mutable {
    if (auto p = weak.lock()) {
      if (p->service && !p->completed)
        p->probe = p->service->processes().start(
            {p->service->executable(), {"-i", input}}, std::move(cb));
    }
  };
  const auto subs = q.value("subtitles").toString();
  if (subs.isEmpty())
    startProbe();
  else
    media::retrieveSubtitle(
        subs, {},
        [weak, q, startProbe](media::SubtitleResult result) mutable {
          auto p = weak.lock();
          if (!p || !p->service || p->completed)
            return;
          // M942 swallows subtitle fetch/alignment errors and continues without
          // burning.
          if (!result.error.isEmpty() || result.tracks.isEmpty() ||
              !p->temporary.isValid()) {
            startProbe();
            return;
          }
          auto path = p->temporary.filePath("source.srt");
          QFile file(path);
          if (!file.open(QIODevice::WriteOnly)) {
            startProbe();
            return;
          }
          const auto bytes =
              media::renderSubtitles(result.tracks, false).replace("\r", "");
          if (file.write(bytes) != bytes.size()) {
            file.close();
            startProbe();
            return;
          }
          file.close();
          static QRegularExpression integer("^\\s*([+-]?\\d+)");
          auto time = integer.match(q.value("time").toString()),
               delay = integer.match(q.value("subtitlesDelay").toString());
          const int offset = std::max(
              0, (time.hasMatch() ? time.captured(1).toInt() : 0) -
                     (delay.hasMatch() ? delay.captured(1).toInt() : 0));
          if (!offset) {
            p->subtitleFile = path;
            startProbe();
            return;
          }
          const auto aligned = p->temporary.filePath("aligned.srt");
          media::ProcessCallbacks align;
          align.onFinished = [weak, aligned, startProbe](
                                 const media::ProcessResult &r) mutable {
            if (auto p = weak.lock()) {
              p->probe = 0;
              if (r.started && r.exitCode == 0)
                p->subtitleFile = aligned;
              startProbe();
            }
          };
          p->probe = p->service->processes().start(
              {p->service->executable(),
               {"-ss", QString::number(offset), "-i", path, "-y", aligned}},
              std::move(align));
        },
        pending->life);

  QTimer::singleShot(120000, &service, [weak] {
    if (auto p = weak.lock()) {
      if (p->completed)
        return;
      p->completed = true;
      if (p->service && p->probe)
        (void)p->service->processes().cancel(p->probe);
      p->probe = 0;
      if (auto *r = server1_http_deferred_response(p->deferred)) {
        body(r, 504, "Casting probe timed out");
        server1_http_deferred_complete(p->deferred);
      }
    }
  });
  return 1;
}
} // namespace
bool installCastingRoutes(void *router, CastingService &service) {
  const std::pair<const char *, int> paths[] = {{"/casting", 0},
                                                {"/casting/transcode:ext?", 1},
                                                {"/casting/convert:ext?", 1},
                                                {"/casting/:devID", 2},
                                                {"/casting/:devID/player", 3}};
  for (auto [path, kind] : paths) {
    auto *route = new Route{&service, kind};
    if (!server1_http_router_add_handler(
            router, 0, 0, kind == 3 ? "ALL" : "GET", path, handle, route,
            [](void *p) { delete static_cast<Route *>(p); })) {
      delete route;
      return false;
    }
  }
  return true;
}
} // namespace server1::casting
