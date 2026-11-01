#include "server1/http/HlsV2Routes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/HlsV2Service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>
namespace server1::http {
namespace {
using media::HlsV2Service;
struct Config {
  QPointer<HlsV2Service> service;
  std::function<QString()> base;
  bool alias;
};
struct Pending {
  void *handle = nullptr;
  ~Pending() { server1_http_deferred_destroy(handle); }
};
void finish(std::weak_ptr<Pending> weak, QByteArray body, const char *type,
            int code = 200, bool length = true) {
  auto p = weak.lock();
  if (!p)
    return;
  auto res = server1_http_deferred_response(p->handle);
  if (!res)
    return;
  server1_http_response_set_status(res, code);
  if (type)
    server1_http_response_set_header(res, "content-type", type);
  if (length)
    server1_http_response_set_header(
        res, "content-length", QByteArray::number(body.size()).constData());
  server1_http_response_set_body(res, body.constData(), body.size());
  server1_http_deferred_complete(p->handle);
}
void fail(std::weak_ptr<Pending> p, int code, QString prefix, QString error) {
  QJsonObject detail{{"message", prefix + error}};
  if (code >= 0)
    detail["code"] = code;
  finish(p,
         QJsonDocument(QJsonObject{{"error", detail}})
             .toJson(QJsonDocument::Compact),
         "application/json", 500, false);
}
QJsonValue queryValue(const QUrlQuery &q, const QString &key) {
  auto all = q.allQueryItemValues(key, QUrl::FullyDecoded);
  if (all.isEmpty())
    return QJsonValue();
  if (all.size() == 1)
    return all.first();
  QJsonArray a;
  for (auto s : all)
    a.append(s);
  return a;
}
QString jsString(const QJsonValue &v) {
  if (v.isArray()) {
    QStringList s;
    for (auto x : v.toArray())
      s.append(x.toString());
    return s.join(',');
  }
  return v.toString();
}
QJsonValue parseInt(const QJsonValue &v) {
  auto m = QRegularExpression("^\\s*([+-]?[0-9]+)").match(jsString(v));
  if (!m.hasMatch())
    return QJsonValue();
  bool ok = false;
  double n = m.captured(1).toDouble(&ok);
  return ok && std::isfinite(n) ? QJsonValue(n) : QJsonValue();
}
bool truth(const QJsonValue &v) {
  return v.isArray() || (!v.isNull() && !v.toString().isEmpty());
}
QString decoded(QString s) { return QUrl::fromPercentEncoding(s.toUtf8()); }
int route(const void *req, void *res, server1_http_next_fn next,
          void *nextContext, void *ctx) {
  auto &cfg = *static_cast<Config *>(ctx);
  if (!cfg.service)
    return next(nextContext);
  QString method = QString::fromUtf8(server1_http_request_method(req));
  if (method != "GET" && method != "HEAD")
    return next(nextContext);
  QString path = QString::fromUtf8(server1_http_request_path(req));
  QUrlQuery query(QUrl(
      QString::fromUtf8(server1_http_request_target(req)).replace("+", " ")));
  QString id, track, action, ext;
  int sequence = 0;
  QJsonObject options;
  if (cfg.alias) {
    auto m = QRegularExpression(
                 "^/([0-9a-fA-F]{40}|file|url)/([^/]+)/"
                 "((?:hls.m3u8|(?:video|audio|subtitle)[0-9]+(?:.m3u8)?))(?:/"
                 "(init.mp4|segment[0-9]+.(?:m4s|vtt)))?/?$")
                 .match(path);
    if (!m.hasMatch())
      return next(nextContext);
    QString kind = decoded(m.captured(1)), video = decoded(m.captured(2));
    id = kind + "-" + video;
    options = {{"mediaURL", kind == "file" ? "file://" + video
                            : kind == "url"
                                ? video
                                : cfg.base() + "/" + kind + "/" + video},
               {"maxAudioChannels", 2}};
    path = "/hlsv2/alias/" + m.captured(3);
    path.replace("hls.m3u8", "master.m3u8");
    if (!m.captured(4).isEmpty())
      path += "/" + m.captured(4);
  }
  if (path.compare("/hlsv2/probe", Qt::CaseInsensitive) == 0 ||
      path.compare("/hlsv2/probe/", Qt::CaseInsensitive) == 0)
    action = "probe";
  else if (path.compare("/hlsv2/status", Qt::CaseInsensitive) == 0 ||
           path.compare("/hlsv2/status/", Qt::CaseInsensitive) == 0)
    action = "status";
  else {
    auto m = QRegularExpression("^/hlsv2/([^/]+)/([^/]+?)(?:\\.m3u8|/"
                                "(init\\.mp4)|/segment([^/]+)\\.([^/.]+))/?$",
                                QRegularExpression::CaseInsensitiveOption)
                 .match(path);
    if (m.hasMatch()) {
      if (id.isEmpty())
        id = decoded(m.captured(1));
      track = cfg.alias
                  ? decoded(m.captured(2))
                  : QString::fromUtf8(server1_http_request_param(req, "track"));
      if (!m.captured(3).isEmpty())
        action = "init";
      else if (!m.captured(4).isEmpty()) {
        action = "segment";
        auto number =
            parseInt(cfg.alias ? decoded(m.captured(4))
                               : QString::fromUtf8(server1_http_request_param(
                                     req, "sequenceNumber")));
        sequence =
            number.isDouble()
                ? int(std::clamp(number.toDouble(),
                                 double(std::numeric_limits<int>::min()),
                                 double(std::numeric_limits<int>::max())))
                : 0;
        ext = cfg.alias
                  ? decoded(m.captured(5))
                  : QString::fromUtf8(server1_http_request_param(req, "ext"));
      } else
        action = "playlist";
    } else {
      m = QRegularExpression("^/hlsv2/([^/]+)/(burn|destroy)/?$",
                             QRegularExpression::CaseInsensitiveOption)
              .match(path);
      if (!m.hasMatch())
        return next(nextContext);
      if (id.isEmpty())
        id = decoded(m.captured(1));
      action = m.captured(2).toLower();
    }
    if (!cfg.alias) {
      options["mediaURL"] = queryValue(query, "mediaURL");
      options["maxAudioChannels"] =
          parseInt(queryValue(query, "maxAudioChannels"));
      options["forceTranscoding"] =
          truth(queryValue(query, "forceTranscoding"));
      for (auto k : {"profile", "maxWidth"}) {
        auto v = queryValue(query, k);
        options[k] = truth(v) ? v : QJsonValue();
      }
      for (auto k : {"videoCodecs", "audioCodecs"}) {
        auto v = queryValue(query, k);
        options[k] = v.isString()  ? QJsonValue(QJsonArray{v})
                     : v.isArray() ? v
                                   : QJsonValue();
      }
    }
  }
  auto pending = std::make_shared<Pending>();
  pending->handle = server1_http_response_defer(res);
  if (!pending->handle)
    return 0;
  auto *owner = new std::shared_ptr<Pending>(pending);
  if (!server1_http_response_set_lifetime(
          res,
          [](void *p) { delete static_cast<std::shared_ptr<Pending> *>(p); },
          owner)) {
    delete owner;
    return 0;
  }
  std::weak_ptr<Pending> weak = pending;
  if (action == "probe") {
    cfg.service->probe(
        jsString(queryValue(query, "mediaURL")),
        truth(queryValue(query, "samples")),
        [weak](QJsonObject value, QString e) {
          if (!e.isEmpty())
            fail(weak, -1, "Failed to probe media: ", e);
          else
            finish(weak, QJsonDocument(value).toJson(QJsonDocument::Compact),
                   "application/json", 200, false);
        });
    return 1;
  }
  if (action == "status") {
    cfg.service->status([weak](QJsonObject value, QString e) {
      if (!e.isEmpty())
        fail(weak, 200, "Failed to get status: ", e);
      else
        finish(weak, QJsonDocument(value).toJson(QJsonDocument::Compact),
               "application/json", 200, false);
    });
    return 1;
  }
  QString error;
  auto converter = cfg.service->acquire(id, options, &error);
  if (!converter) {
    fail(weak, 1, "Failed to create hls converter: ", error);
    return 1;
  }
  const bool av =
      QRegularExpression("^(?:video|audio).+$").match(track).hasMatch();
  const bool sub = QRegularExpression("^subtitle.+$").match(track).hasMatch();
  if ((action == "init" && !av) ||
      (action == "segment" &&
       ((av && ext != "m4s") || (sub && ext != "vtt")))) {
    server1_http_deferred_complete(pending->handle);
    return next(nextContext);
  }
  if (action == "destroy") {
    converter->destroy();
    finish(weak, {}, nullptr, 200, false);
    return 1;
  }
  int code = action == "playlist"  ? 10
             : action == "init"    ? 20
             : action == "segment" ? 30
                                   : 40;
  QString prefix = action == "playlist"  ? "Failed to read hls playlist: "
                   : action == "init"    ? "Failed to read init segment: "
                   : action == "segment" ? "Failed to read media segment: "
                                         : "Failed to burn subtitles: ";
  auto cb = [weak, converter, action, ext, code,
             prefix](const media::TrackConversionResult &r) {
    if (!r.ok())
      fail(weak, code, prefix, r.error);
    else
      finish(weak, r.data,
             action == "playlist" ? "application/vnd.apple.mpegurl"
             : action == "init" || ext == "m4s" ? "video/mp4"
             : action == "segment"              ? "text/vtt"
                                                : nullptr,
             200, action != "burn");
  };
  if (action == "playlist")
    converter->playlist(track, cb);
  else if (action == "init")
    converter->initSegment(track, cb);
  else if (action == "segment")
    converter->mediaSegment(track, sequence, cb);
  else
    converter->burnSubtitles(
        {{"url", queryValue(query, "url")}, {"id", queryValue(query, "id")}},
        cb);
  return 1;
}
} // namespace
bool installHlsV2Routes(void *router, media::HlsV2Service *service,
                        QString base) {
  return installHlsV2Routes(router, service, [base]{ return base; });
}
bool installHlsV2Routes(void *router, media::HlsV2Service *service,
                        std::function<QString()> base) {
  const QStringList direct = {"/hlsv2/:id/:track.m3u8",
                              "/hlsv2/:id/:track/init.mp4",
                              "/hlsv2/:id/:track/segment:sequenceNumber.:ext",
                              "/hlsv2/:id/burn",
                              "/hlsv2/probe",
                              "/hlsv2/status",
                              "/hlsv2/:id/destroy"};
  for (const auto &pattern : direct) {
    auto *c = new Config{service, base, false};
    if (!server1_http_router_add_handler(
            router, 0, 0, "GET", pattern.toUtf8().constData(), route, c,
            [](void *p) { delete static_cast<Config *>(p); }))
      return false;
  }
  auto *c = new Config{service, base, true};
  if (!server1_http_router_add_handler(
          router, 0, 0, "GET", "/:infoHash/:videoId/:playlist/:HLSSegment?",
          route, c, [](void *p) { delete static_cast<Config *>(p); }))
    return false;
  return true;
}
} // namespace server1::http
