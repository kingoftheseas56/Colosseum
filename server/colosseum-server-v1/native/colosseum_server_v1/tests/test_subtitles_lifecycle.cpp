#include "server1/http/HttpContract.h"
#include "server1/media/SubtitleFetch.h"
#include "server1/media/SubtitleHash.h"
#include "server1/media/SubtitleRoutes.h"
#include "server1/media/TrackExtraction.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>
using namespace server1::media;
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc < 3)
    return 2;
  QString mode = argv[1], url = argv[2];
  QObject root;
  auto done = [&](QJsonObject o) {
    std::cout << QJsonDocument(o).toJson(QJsonDocument::Compact).constData()
              << std::endl;
    app.quit();
  };
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &app, [&] {
    std::cerr << "timeout\n";
    app.exit(9);
  });
  timeout.start(10000);
  if (mode == "fetch" || mode == "agent") {
    SubtitleOptions opts;
    if (argc > 3)
      opts.charset = argv[3];
    if (mode == "agent") {
      opts.hasAgent = true;
      opts.charset = "invalid-charset";
      opts.agentRequest = [](QObject *ctx, const QString &, ByteCallback cb) {
        QTimer::singleShot(0, ctx, [=] { cb({{}, "agent failure"}); });
      };
    }
    retrieveSubtitle(
        url, opts,
        [&](SubtitleResult r) {
          done({{"text", r.text}, {"tracks", r.tracks}, {"error", r.error}});
        },
        &root);
  } else if (mode == "hash")
    calculateSubtitleHash(
        url,
        [&](HashResult r) { done({{"result", r.value}, {"error", r.error}}); },
        &root);
  else if (mode == "tracks")
    extractMediaTracks(
        url,
        [&](TrackResult r) {
          done({{"tracks", r.tracks},
                {"error", r.error},
                {"incomplete", r.incomplete}});
        },
        &root, argc > 3 ? QString(argv[3]).toLongLong() : 26214400);
  else if (mode == "cancel") {
    auto *job =
        extractMediaTracks(url, [&](TrackResult) { app.exit(6); }, &root);
    delete job;
    QTimer::singleShot(100, &app, [&] { done({{"cancelled", true}}); });
  } else if (mode == "route" || mode == "route-abort") {
    void *router = server1_http_router_create();
    if (!installSubtitleRoutes(router, &root))
      return 3;
    void *response = server1_http_router_dispatch(router, "GET",
                                                  url.toUtf8().constData(), "");
    if (mode == "route-abort") {
      server1_http_response_destroy(response);
      server1_http_router_destroy(router);
      QTimer::singleShot(100, &app, [&] { done({{"cancelled", true}}); });
    } else {
      auto *tick = new QTimer(&root);
      QObject::connect(tick, &QTimer::timeout, &app, [&, response, router] {
        if (server1_http_response_is_deferred(response))
          return;
        auto body = QByteArray(server1_http_response_body(response),
                               server1_http_response_body_size(response));
        QJsonObject result{{"status", server1_http_response_status(response)},
                           {"body", QString::fromUtf8(body)}};
        server1_http_response_destroy(response);
        server1_http_router_destroy(router);
        done(result);
      });
      tick->start(1);
    }
  }
  return app.exec();
}
