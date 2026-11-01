#include "server1/media/SubtitleRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/SubtitleFetch.h"
#include "server1/media/SubtitleHash.h"
#include "server1/media/TrackExtraction.h"
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
namespace server1::media {
namespace {
struct Config {
  int kind;
  QPointer<QObject> owner;
};
struct Pending {
  QPointer<QObject> life;
  void *deferred = nullptr;
  ~Pending() {
    delete life.data();
    server1_http_deferred_destroy(deferred);
  }
};
QString query(const void *req, const char *key) {
  const char *p = server1_http_request_query_value(req, key);
  return p ? QString::fromUtf8(p) : QString();
}
void finish(Pending *p, int status, const QByteArray &body, bool json) {
  if (auto *res = server1_http_deferred_response(p->deferred)) {
    server1_http_response_set_status(res, status);
    if (json) {
      server1_http_response_set_header(res, "Content-Type", "application/json");
      server1_http_response_set_header(
          res, "Content-Length", QByteArray::number(body.size()).constData());
    }
    server1_http_response_set_body(res, body.constData(), body.size());
    server1_http_deferred_complete(p->deferred);
  }
}
int handler(const void *req, void *response, server1_http_next_fn, void *,
            void *context) {
  auto *cfg = static_cast<Config *>(context);
  auto *p = new Pending{new QObject(cfg->owner),
                        server1_http_response_defer(response)};
  if (!p->deferred ||
      !server1_http_response_set_lifetime(
          response, [](void *p) { delete static_cast<Pending *>(p); }, p)) {
    delete p;
    return 0;
  }
  if (cfg->kind == 0) {
    auto url = query(req, "subsUrl");
    retrieveSubtitle(
        url, {},
        [=](SubtitleResult r) {
          QJsonObject obj;
          if (r.error.isEmpty()) {
            obj["error"] = QJsonValue();
            obj["result"] = QJsonObject{{"url", url}, {"tracks", r.tracks}};
          } else
            obj["error"] = QJsonObject{};
          finish(p, r.error.isEmpty() ? 200 : 500,
                 QJsonDocument(obj).toJson(QJsonDocument::Compact), true);
        },
        p->life);
  } else if (cfg->kind == 1) {
    calculateSubtitleHash(
        query(req, "videoUrl"),
        [=](HashResult r) {
          QJsonObject obj{{"error", r.error.isEmpty() ? QJsonValue()
                                                      : QJsonValue(r.error)}};
          if (r.error.isEmpty())
            obj["result"] = r.value;
          finish(p, r.error.isEmpty() ? 200 : 500,
                 QJsonDocument(obj).toJson(QJsonDocument::Compact), true);
        },
        p->life);
  } else if (cfg->kind == 2) {
    QString target = QString::fromUtf8(server1_http_request_target(req));
    bool vtt = target.startsWith("/subtitles.vtt");
    auto m =
        QRegularExpression("^[\\s]*([+-]?[0-9]+)").match(query(req, "offset"));
    qint64 offset = m.hasMatch() ? m.captured(1).toLongLong() : 0;
    retrieveSubtitle(
        query(req, "from"), {},
        [=](SubtitleResult r) {
          bool good = r.error.isEmpty() && !r.tracks.isEmpty();
          finish(p, good ? 200 : 500,
                 good ? renderSubtitles(r.tracks, vtt, offset) : QByteArray{},
                 false);
        },
        p->life);
  } else {
    QString path = QString::fromUtf8(server1_http_request_param(req, "url"));
    extractMediaTracks(
        path,
        [=](TrackResult r) {
          finish(p, 200,
                 QJsonDocument(r.error.isEmpty() ? r.tracks : QJsonArray{})
                     .toJson(QJsonDocument::Compact),
                 true);
        },
        p->life, 26214400);
  }
  return 1;
}
} // namespace
bool installSubtitleRoutes(void *router, QObject *owner) {
  const char *paths[] = {"/subtitlesTracks", "/opensubHash", "/subtitles.:ext",
                         "/tracks/:url"};
  for (int i = 0; i < 4; ++i) {
    auto *cfg = new Config{i, owner};
    if (!server1_http_router_add_handler(
            router, 0, 0, "GET", paths[i], handler, cfg,
            [](void *p) { delete static_cast<Config *>(p); })) {
      delete cfg;
      return false;
    }
  }
  return true;
}
} // namespace server1::media
