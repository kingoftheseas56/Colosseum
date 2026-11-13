#include "server1/casting/CastingService.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <stdexcept>
using namespace server1::casting;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void waitReady(void *res) {
  QElapsedTimer timer;
  timer.start();
  while (server1_http_response_is_deferred(res) && timer.elapsed() < 15000) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  check(!server1_http_response_is_deferred(res), "deferred timeout");
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    check(argc >= 2, "fixture required");
    QFile file(argv[1]);
    check(file.open(QIODevice::ReadOnly), "fixture missing");
    int count = 0;
    for (auto value : QJsonDocument::fromJson(file.readAll()).array()) {
      auto row = value.toObject();
      QJsonValue result;
      auto kind = row["kind"].toString();
      if (kind == "info")
        result = CastingService::videoInfo(row["input"].toString().toUtf8());
      else if (kind == "hms")
        result = CastingService::hmsToSeconds(row["input"].toString());
      else if (kind == "ssdp")
        result = CastingService::ssdpDevice(row["location"].toString(),
                                            row["xml"].toString().toUtf8());
      else if (kind == "mdns")
        result = CastingService::mdnsDevice(row["input"].toObject());
      else if (kind == "player") {
        auto status = initialPlayerStatus();
        auto call = selectPlayerCall(status, row["query"].toObject(), {});
        result = QJsonObject{{"method", call.method},
                             {"arguments", call.arguments},
                             {"status", status}};
      } else if (kind == "dlna")
        result =
            conformDlnaStatus(initialPlayerStatus(), row["input"].toObject());
      else if (kind == "chromecast")
        result = conformChromecastStatus(initialPlayerStatus(),
                                         row["input"].toObject());
      else if (kind == "args")
        result = QJsonArray::fromStringList(CastingService::arguments(
            row["query"].toObject(), row["info"].toObject(),
            row["subtitleFile"].toString()));
      if (result != row["expected"]) {
        std::cerr << QJsonDocument(QJsonArray{result, row["expected"]})
                         .toJson()
                         .constData();
        throw std::runtime_error("oracle mismatch");
      }
      ++count;
    }
    CastingService service(argc > 2 ? argv[2] : "ffmpeg");
    auto *router = server1_http_router_create();
    check(installCastingRoutes(router, service), "install");
    auto *res = server1_http_router_dispatch(router, "GET", "/casting/", "");
    check(QByteArray(server1_http_response_body(res)) == "[]", "empty list");
    server1_http_response_destroy(res);
    res = server1_http_router_dispatch(router, "GET", "/casting", "");
    check(server1_http_response_status(res) == 200 &&
              QByteArray(server1_http_response_body(res)) == "[]",
          "casting root without trailing slash");
    server1_http_response_destroy(res);
    service.collect({{"id", "fixture"}, {"name", "First"}});
    service.collect({{"id", "fixture"}, {"name", "Second"}});
    check(service.devices().size() == 1 &&
              service.device("fixture")["name"] == "First",
          "first wins");
    for (auto path : {"/casting/transcode.mp4", "/casting/convert.mkv"}) {
      res = server1_http_router_dispatch(router, "GET", path, "");
      check(server1_http_response_status(res) == 400, "missing video");
      server1_http_response_destroy(res);
    }
    res = server1_http_router_dispatch(router, "GET", "/casting/missing/player",
                                       "");
    check(server1_http_response_status(res) == 404, "device missing");
    server1_http_response_destroy(res);
    auto d = CastingService::ssdpDevice(
        "http://127.0.0.1:42/a",
        "<root><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1<"
        "/deviceType><UDN>uuid:abc</UDN><friendlyName>Fixture &amp; "
        "TV</friendlyName></device></root>");
    check(d["id"] == "abc" && d["name"] == "Fixture & TV" && d["type"] == "tv",
          "SSDP normalization");
    QByteArray frames = encodeCastFrame(
        "sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat",
        {{"type", "PING"}});
    auto full = frames;
    QByteArray partial = frames.left(3);
    QJsonArray messages;
    QString error;
    check(decodeCastFrames(partial, messages, error) && messages.isEmpty(),
          "partial cast header");
    partial += frames.mid(3);
    check(decodeCastFrames(partial, messages, error) && partial.isEmpty() &&
              messages.size() == 1 &&
              messages.first().toObject()["message"].toObject()["type"] ==
                  "PING",
          "cast frame roundtrip");
    QByteArray tooLarge = QByteArray::fromHex("7fffffff");
    check(!decodeCastFrames(tooLarge, messages, error), "cast limit");
    QTcpServer fixture;
    check(fixture.listen(QHostAddress::LocalHost, 0), "loopback fixture");
    QByteArray received;
    int requests = 0;
    QObject::connect(&fixture, &QTcpServer::newConnection, &fixture, [&] {
      while (auto *socket = fixture.nextPendingConnection()) {
        auto pending = std::make_shared<QByteArray>();
        QObject::connect(
            socket, &QTcpSocket::readyRead, socket, [&, socket, pending] {
              *pending += socket->readAll();
              auto boundary = pending->indexOf("\r\n\r\n");
              if (boundary < 0)
                return;
              auto header = pending->left(boundary);
              QRegularExpression re("Content-Length: ([0-9]+)",
                                    QRegularExpression::CaseInsensitiveOption);
              auto m = re.match(QString::fromUtf8(header));
              int length = m.hasMatch() ? m.captured(1).toInt() : 0;
              if (pending->size() < boundary + 4 + length)
                return;
              received = *pending;
              ++requests;
              QByteArray data =
                  header.startsWith("GET")
                      ? QByteArray("1\n00:00:00,500 --> 00:00:02,800\nCASTING "
                                   "FIXTURE\n\n")
                      : QByteArray("<?xml version=\"1.0\"?><s:Envelope "
                                   "xmlns:s=\"http://schemas.xmlsoap.org/soap/"
                                   "envelope/\"><s:Body><u:GetVolumeResponse "
                                   "xmlns:u=\"urn:schemas-upnp-org:service:"
                                   "RenderingControl:1\"><CurrentVolume>42</"
                                   "CurrentVolume></u:GetVolumeResponse></"
                                   "s:Body></s:Envelope>");
              socket->write(
                  "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                  QByteArray::number(data.size()) + "\r\n\r\n" + data);
              socket->disconnectFromHost();
            });
        QObject::connect(socket, &QTcpSocket::disconnected, socket,
                         &QObject::deleteLater);
      }
    });
    DlnaSoapClient soap(
        QUrl(QString("http://127.0.0.1:%1/control").arg(fixture.serverPort())));
    bool complete = false;
    soap.call("urn:schemas-upnp-org:service:RenderingControl:1", "GetVolume",
              {{"InstanceID", 0}, {"Channel", "Master"}}, [&](SoapResult r) {
                check(r.error.isEmpty() && r.values["CurrentVolume"] == "42",
                      "SOAP response");
                complete = true;
              });
    QElapsedTimer deadline;
    deadline.start();
    while (!complete && deadline.elapsed() < 10000) {
      QCoreApplication::processEvents();
      QThread::msleep(1);
    }
    check(complete && received.toLower().contains("soapaction") &&
              received.contains("<Channel>Master</Channel>"),
          "SOAP loopback request");
    service.setPlayerInvoker([&](const QJsonObject &, const PlayerCall &call,
                                 const QJsonObject &status,
                                 std::function<void(QJsonValue)> callback) {
      check(call.method == "volume", "player command priority");
      soap.call(
          "urn:schemas-upnp-org:service:RenderingControl:1", "SetVolume",
          {{"InstanceID", 0}, {"Channel", "Master"}, {"DesiredVolume", 42}},
          [callback, status](SoapResult r) mutable {
            auto s = status;
            if (r.error.isEmpty())
              s["volume"] = 0.42;
            callback(s);
          });
    });
    res = server1_http_router_dispatch(router, "POST",
                                       "/casting/fixture/player?volume=0.2",
                                       "{\"volume\":0.42}");
    waitReady(res);
    check(QJsonDocument::fromJson(QByteArray(server1_http_response_body(res)))
                  .object()["volume"] == 0.42,
          "player async route");
    server1_http_response_destroy(res);
    if (argc > 2) {
      QTemporaryDir dir;
      auto input = dir.filePath("input.mkv");
      QProcess ff;
      ff.start(argv[2], {"-v", "error", "-f", "lavfi", "-i",
                         "testsrc2=size=160x120:rate=25", "-f", "lavfi", "-i",
                         "sine=frequency=440", "-t", "6", "-c:v", "mpeg4",
                         "-c:a", "pcm_s16le", "-y", input});
      check(ff.waitForFinished(20000) && ff.exitCode() == 0,
            "generate real media");
      auto target =
          QByteArray("/casting/transcode.mp4?fmp4=1&video=") +
          QUrl::toPercentEncoding(QUrl::fromLocalFile(input).toString());
      res = server1_http_router_dispatch(router, "GET", target.constData(), "");
      waitReady(res);
      check(server1_http_response_has_stream(res), "real stream missing");
      QByteArray output;
      QElapsedTimer timer;
      timer.start();
      char buffer[16384];
      bool eof = false;
      while (timer.elapsed() < 30000) {
        auto n = server1_http_response_stream_read(res, buffer, sizeof(buffer));
        if (n > 0)
          output.append(buffer, n);
        else if (n == 0) {
          eof = true;
          break;
        } else
          check(n == SERVER1_HTTP_WOULD_BLOCK, "stream failure");
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      check(eof && output.size() > 1000, "real output");
      server1_http_response_destroy(res);
      auto outputPath = dir.filePath("output.mp4");
      QFile out(outputPath);
      check(out.open(QIODevice::WriteOnly), "output file");
      out.write(output);
      out.close();
      ff.start(argv[2], {"-v", "error", "-i", outputPath, "-f", "null", "-"});
      check(ff.waitForFinished(20000) && ff.exitCode() == 0,
            "decode real casting output");
      res = server1_http_router_dispatch(router, "GET", target.constData(), "");
      waitReady(res);
      QElapsedTimer pause;
      pause.start();
      while (service.bufferedBytes() < 131072 && pause.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      check(service.bufferedBytes() >= 131072 &&
                service.bufferedBytes() < 196608,
            "producer did not pause at bounded watermark");
      const auto buffered = service.bufferedBytes();
      pause.restart();
      while (pause.elapsed() < 250) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      check(service.bufferedBytes() == buffered, "paused producer buffer grew");
      server1_http_response_destroy(res);
      check(service.bufferedBytes() == 0, "stream buffer leaked");
      timer.restart();
      while (service.processes().activeCount() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      check(service.processes().activeCount() == 0, "cancel leaked process");
      auto subtitled =
          target + "&time=1&subtitlesDelay=0&subtitles=" +
          QUrl::toPercentEncoding(QString("http://127.0.0.1:%1/subs.srt")
                                      .arg(fixture.serverPort()));
      res = server1_http_router_dispatch(router, "GET", subtitled.constData(),
                                         "");
      waitReady(res);
      check(server1_http_response_has_stream(res), "subtitles stream");
      output.clear();
      timer.restart();
      eof = false;
      while (timer.elapsed() < 30000) {
        auto n = server1_http_response_stream_read(res, buffer, sizeof(buffer));
        if (n > 0)
          output.append(buffer, n);
        else if (n == 0) {
          eof = true;
          break;
        } else
          check(n == SERVER1_HTTP_WOULD_BLOCK, "subtitle stream failure");
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      check(eof && output.size() > 1000 && requests >= 3,
            "subtitle real output");
      server1_http_response_destroy(res);
      check(out.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "subtitle output");
      out.write(output);
      out.close();
      ff.start(argv[2], {"-v", "error", "-i", outputPath, "-f", "null", "-"});
      check(ff.waitForFinished(20000) && ff.exitCode() == 0,
            "decode burned subtitles");
      res =
          server1_http_router_dispatch(router, "HEAD", target.constData(), "");
      waitReady(res);
      check(!server1_http_response_has_stream(res), "HEAD launched stream");
      server1_http_response_destroy(res);
    }
    server1_http_router_destroy(router);
    std::cout << "PASS " << count
              << " casting oracle cases, passive discovery/routes"
              << (argc > 2 ? ", real FFmpeg decode/cancel/HEAD" : "") << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return 0;
}
