#include "server1/http/HlsV2Routes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/HlsV2Service.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>
#include <iostream>
using namespace server1::media;
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc < 3) {
    std::cerr << "Required: factory fixture and real media path\n";
    return 2;
  }
  QFile f(argv[1]);
  if (!f.open(QIODevice::ReadOnly))
    return 2;
  for (auto v : QJsonDocument::fromJson(f.readAll()).array()) {
    auto row = v.toObject();
    QString error;
    auto actual = HlsV2Service::queryString(row["options"].toObject(), &error);
    if (error != row["error"].toString() ||
        (error.isEmpty() && actual != row["query"].toString())) {
      std::cerr << "factory mismatch: "
                << QJsonDocument(row).toJson().constData()
                << actual.toStdString() << " / " << error.toStdString();
      return 1;
    }
  }

  QString differential = f.fileName();
  differential.replace("factory", "converters");
  QFile df(differential);
  if (!df.open(QIODevice::ReadOnly))
    return 9;
  for (auto rowValue : QJsonDocument::fromJson(df.readAll()).array()) {
    auto row = rowValue.toObject();
    qputenv(
        "HLSV2_FIXTURE_RAW",
        QJsonDocument(row["input"].toObject()).toJson(QJsonDocument::Compact));
    HlsV2Service fake(
        "/usr/bin/ffmpeg",
        QFileInfo(f.fileName()).dir().filePath("../hlsv2_probe_fixture.py"));
    QString e;
    auto converter = fake.acquire(
        "oracle", {{"mediaURL", row["mediaURL"]}, {"maxAudioChannels", 2}}, &e);
    for (auto expectedValue : row["results"].toArray()) {
      auto expected = expectedValue.toObject();
      QString action = expected["action"].toString();
      QJsonObject actual{{"action", action}};
      bool done = false;
      if (action == "destroy") {
        converter->destroy();
        done = true;
      } else if (action == "status")
        converter->status([&](QJsonObject v, QString error) {
          if (error.isEmpty())
            actual["value"] = v;
          else
            actual["error"] = error;
          done = true;
        });
      else
        converter->playlist(action == "missing" ? "audio999" : "master",
                            [&](const TrackConversionResult &r) {
                              if (r.ok())
                                actual["value"] = QString::fromUtf8(r.data);
                              else
                                actual["error"] = r.error;
                              done = true;
                            });
      QElapsedTimer timer;
      timer.start();
      while (!done && timer.elapsed() < 10000) {
        app.processEvents();
        QThread::msleep(1);
      }
      if (!done || actual != expected) {
        std::cerr << "converter differential mismatch\nactual "
                  << QJsonDocument(actual).toJson().constData() << "expected "
                  << QJsonDocument(expected).toJson().constData();
        return 10;
      }
    }
  }
  HlsV2Service service("/usr/bin/ffmpeg", "/usr/bin/ffprobe");
  qint64 time = 1000;
  service.setClock([&] { return time; });
  service.setConcurrency(2, 2);
  QString error;
  auto a = service.acquire("a", {{"mediaURL", "file:a"}}, &error);
  if (!a || !error.isEmpty())
    return 3;
  if (service.acquire("a", {}, &error) != a)
    return 4;
  service.acquire("b", {{"mediaURL", "file:b"}}, &error);
  time = 1001;
  service.acquire("a", {}, &error);
  service.acquire("c", {{"mediaURL", "file:c"}}, &error);
  if (service.contains("b") || !service.contains("a"))
    return 5;
  service.destroy("a");
  if (!service.contains("a"))
    return 6;
  time = 121001;
  service.cleanup();
  if (!service.contains("a"))
    return 7;
  time++;
  service.cleanup();
  if (service.contains("a"))
    return 8;

  auto check = [](bool ok, const char *why) {
    if (!ok) {
      std::cerr << why << "\n";
      std::exit(20);
    }
  };
  {
    HlsV2Service pending(
        "/usr/bin/ffmpeg",
        QFileInfo(f.fileName()).dir().filePath("../hlsv2_probe_fixture.py"));
    auto converter =
        pending.acquire("pending", {{"mediaURL", "fixture:pending"}}, &error);
    int calls = 0;
    bool correct = true;
    converter->playlist("master", [&](const auto &r) {
      ++calls;
      correct = correct && r.error == "MasterConverter is destroyed";
    });
    auto same = pending.acquire("pending", {}, &error);
    check(same == converter, "pending creates share converter");
    same->playlist("master", [&](const auto &r) {
      ++calls;
      correct = correct && r.error == "MasterConverter is destroyed";
    });
    converter->destroy();
    check(calls == 2 && correct, "pending probe canceled exactly once");
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
      app.processEvents();
      QThread::msleep(1);
    }
    check(calls == 2, "late process callback suppressed");
  }
  {
    HlsV2Service pending(
        "/usr/bin/ffmpeg",
        QFileInfo(f.fileName()).dir().filePath("../hlsv2_probe_fixture.py"));
    pending.setConcurrency(1, 1);
    auto old =
        pending.acquire("old", {{"mediaURL", "fixture:reentrant"}}, &error);
    int calls = 0;
    old->playlist("master", [&](const auto &r) {
      ++calls;
      check(!r.ok(), "eviction cancels waiters");
      pending.acquire("inside", {{"mediaURL", "fixture:inside"}}, &error);
    });
    pending.acquire("new", {{"mediaURL", "fixture:new"}}, &error);
    check(calls == 1, "reentrant eviction completed");
    check(pending.contains("inside") && !pending.contains("new"),
          "reentrant source limit retained");
  }
  auto *router = server1_http_router_create();
  check(server1::http::installHlsV2Routes(router, &service,
                                          "http://127.0.0.1:11470"),
        "install routes");
  auto request = [&](const char *method, QString target) {
    auto *response = server1_http_router_dispatch(
        router, method, target.toUtf8().constData(), "");
    QElapsedTimer timer;
    timer.start();
    while (server1_http_response_is_deferred(response) &&
           timer.elapsed() < 10000) {
      app.processEvents();
      QThread::msleep(1);
    }
    check(!server1_http_response_is_deferred(response), "deferred timeout");
    return response;
  };
  auto json = [](void *r) {
    return QJsonDocument::fromJson(
               QByteArray(server1_http_response_body(r),
                          server1_http_response_body_size(r)))
        .object();
  };
  {
    HlsV2Service oracle(
        "/usr/bin/ffmpeg",
        QFileInfo(f.fileName()).dir().filePath("../hlsv2_probe_fixture.py"));
    oracle.setConcurrency(4, 1);
    auto *saved = router;
    router = server1_http_router_create();
    check(server1::http::installHlsV2Routes(router, &oracle,
                                            "http://127.0.0.1:11470"),
          "oracle routes install");
    QString path = f.fileName();
    path.replace("factory", "routes");
    QFile routes(path);
    check(routes.open(QIODevice::ReadOnly), "route golden open");
    for (auto value : QJsonDocument::fromJson(routes.readAll()).array()) {
      auto expected = value.toObject();
      auto *res = request("GET", expected["target"].toString());
      auto body = server1_http_response_body_size(res) == 0
                      ? QJsonValue()
                      : QJsonValue(json(res));
      if (server1_http_response_status(res) != expected["status"].toInt() ||
          body != expected["body"]) {
        std::cerr << "route oracle mismatch "
                  << QJsonDocument(expected).toJson().constData()
                  << server1_http_response_body(res);
        std::exit(21);
      }
      server1_http_response_destroy(res);
    }
    server1_http_router_destroy(router);
    router = saved;
  }
  auto *r = request("GET", "/hlsv2/missing/master.m3u8");
  check(server1_http_response_status(r) == 500 &&
            json(r)["error"].toObject()["code"] == 1,
        "invalid create");
  server1_http_response_destroy(r);
  r = request("POST", "/hlsv2/post/master.m3u8?mediaURL=file:a");
  check(server1_http_response_status(r) == 404 && !service.contains("post"),
        "method restriction");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/double/master.m3u8?mediaURL=a&mediaURL=b");
  check(json(r)["error"].toObject()["code"] == 1, "duplicate URL rejected");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/dead/destroy?mediaURL=file:a");
  check(server1_http_response_status(r) == 200 && service.contains("dead"),
        "destroy registry retains");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/dead/master.m3u8");
  check(json(r)["error"].toObject()["message"] ==
            "Failed to read hls playlist: MasterConverter is destroyed",
        "destroyed lookup");
  server1_http_response_destroy(r);
  r = request("GET", "/HLSV2/dead/master.M3U8");
  check(json(r)["error"].toObject()["code"] == 10,
        "case insensitive route literal");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/dead/video0/segment1.foo.m4s");
  check(server1_http_response_status(r) == 404,
        "multi-dot extension follows router capture");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/%zz/master.m3u8");
  check(server1_http_response_status(r) == 400,
        "router rejects malformed parameter encoding");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/status");
  check(json(r)["error"].toObject()["code"] == 200, "destroyed status");
  server1_http_response_destroy(r);
  r = request("GET", "/hlsv2/probe?mediaURL=file:/does-not-exist");
  check(server1_http_response_status(r) == 500 &&
            !json(r)["error"].toObject().contains("code"),
        "missing probe error code preserved");
  server1_http_response_destroy(r);
  time += 120001;
  service.cleanup();
  if (argc > 2) {
    QString file = QString::fromLocal8Bit(argv[2]),
            url = QString::fromLatin1(QUrl::toPercentEncoding(file));
    r = request("GET", "/hlsv2/real/master.m3u8?mediaURL=" + url);
    check(server1_http_response_status(r) == 200, "real master playlist");
    check(QByteArray(server1_http_response_body(r)).startsWith("#EXTM3U"),
          "playlist header");
    server1_http_response_destroy(r);
    r = request("GET", "/file/" + url + "/hls.m3u8?maxAudioChannels=9");
    check(server1_http_response_status(r) == 200, "real alias");
    check(QByteArray(server1_http_response_body(r))
              .contains("maxAudioChannels=2"),
          "alias forced channel cap");
    server1_http_response_destroy(r);
    r = request("GET", "/hlsv2/realvideo/video0.m3u8?mediaURL=" + url);
    if (server1_http_response_status(r) != 200)
      std::cerr << server1_http_response_body(r) << "\n";
    check(server1_http_response_status(r) == 200, "real video media playlist");
    server1_http_response_destroy(r);
    r = request("GET", "/hlsv2/realvideo/video0/init.mp4");
    check(server1_http_response_status(r) == 200 &&
              server1_http_response_body_size(r) > 0,
          "real video init");
    server1_http_response_destroy(r);
    r = request("GET", "/hlsv2/realvideo/video0/segment1.m4s");
    if (server1_http_response_status(r) != 200)
      std::cerr << server1_http_response_body(r) << "\n";
    check(server1_http_response_status(r) == 200 &&
              server1_http_response_body_size(r) > 0,
          "real video media segment");
    server1_http_response_destroy(r);
    service.setConcurrency(2, 1);
    r = request("GET", "/hlsv2/realvideo/audio0.m3u8");
    check(server1_http_response_status(r) == 200, "real audio playlist");
    server1_http_response_destroy(r);
    r = request("GET", "/hlsv2/realvideo/audio999.m3u8");
    check(server1_http_response_status(r) == 500,
          "invalid track after eviction");
    server1_http_response_destroy(r);
    r = request("GET", "/hlsv2/status");
    auto converters = json(r)["realvideo"]
                          .toObject()["status"]
                          .toObject()["converters"]
                          .toObject();
    check(converters.contains("video0") && converters.contains("audio999") &&
              !converters.contains("audio0"),
          "per-type track eviction");
    server1_http_response_destroy(r);
    auto *abandoned = server1_http_router_dispatch(
        router, "GET",
        ("/hlsv2/abandoned/master.m3u8?mediaURL=" + url).toUtf8().constData(),
        "");
    server1_http_response_destroy(abandoned);
    service.destroy("abandoned");
    app.processEvents();
    auto *server = server1_http_server_create(router, 65536);
    check(server1_http_server_listen(server, 0), "real HTTP listen");
    QNetworkAccessManager manager;
    auto *reply = manager.get(QNetworkRequest(
        QUrl(QString("http://127.0.0.1:%1/hlsv2/probe?mediaURL=%2")
                 .arg(server1_http_server_port(server))
                 .arg(url))));
    QElapsedTimer timer;
    timer.start();
    while (!reply->isFinished() && timer.elapsed() < 10000) {
      app.processEvents();
      QThread::msleep(1);
    }
    check(reply->isFinished() &&
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                      .toInt() == 200,
          "real HTTP/process probe");
    check(
        QJsonDocument::fromJson(reply->readAll()).object()["streams"].isArray(),
        "real probe streams");
    delete reply;
    server1_http_server_destroy(server);
    app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  server1_http_router_destroy(router);
  std::cout << "factory, registry, routes, HTTP/process passed\n";
}
