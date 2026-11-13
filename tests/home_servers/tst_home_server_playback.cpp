#include "HomeServerPlayback.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

class PlaybackReplay : public QTcpServer {
public:
  struct Reply {
    int status = 200;
    QJsonObject body;
    int delay = 0;
  };
  QList<Reply> replies;
  QList<QByteArray> requests;
  PlaybackReplay() {
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = nextPendingConnection()) {
        auto data = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, data] {
          *data += socket->readAll();
          auto end = data->indexOf("\r\n\r\n");
          if (end < 0 || socket->property("answered").toBool())
            return;
          qsizetype length = 0;
          for (auto line : data->left(end).split('\n'))
            if (line.toLower().startsWith("content-length:"))
              length = line.mid(15).trimmed().toLongLong();
          if (data->size() < end + 4 + length)
            return;
          socket->setProperty("answered", true);
          requests << *data;
          const auto reply = replies.isEmpty() ? Reply{} : replies.takeFirst();
          QTimer::singleShot(reply.delay, socket, [socket, reply] {
            auto bytes =
                QJsonDocument(reply.body).toJson(QJsonDocument::Compact);
            socket->write(
                "HTTP/1.1 " + QByteArray::number(reply.status) +
                " Replay\r\nContent-Type: application/json\r\nConnection: "
                "close\r\nContent-Length: " +
                QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes);
            socket->disconnectFromHost();
          });
        });
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
      }
    });
  }
  QUrl origin() const {
    return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + "/proxy");
  }
  static QJsonObject fixture(const char *name) {
    QFile f(QStringLiteral(HOME_SERVER_FIXTURE_DIR) + "/playback/" +
            QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly))
      return {};
    auto bytes = f.readAll();
    if (bytes.startsWith("\xef\xbb\xbf"))
      bytes.remove(0, 3);
    return QJsonDocument::fromJson(bytes).object();
  }
};
class PlaybackTest : public QObject {
  Q_OBJECT
  static QJsonObject connection(PlaybackReplay &server,
                                QString provider = "jellyfin") {
    return {{"provider", provider},
            {"origin", server.origin().toString()},
            {"userId", "user"},
            {"serverMajor", 12}};
  }
  static QJsonObject item(QString version = "version-a", int mi = -1,
                          int pi = -1) {
    QJsonObject v{{"id", version}};
    if (mi >= 0)
      v.insert("mediaIndex", mi);
    if (pi >= 0)
      v.insert("partIndex", pi);
    return {{"id", "movie"}, {"versions", QJsonArray{v}}};
  }
  static HomeServerPlaybackOptions android(QString mime = "video/avc",
                                           QString profile = "high",
                                           double level = 4.1) {
    HomeServerPlaybackOptions options;
    options.android = true;
    options.videoDecodeCapabilities =
        QJsonObject{
            {"available", true},
            {"complete", true},
            {"decoders",
             QJsonArray{QJsonObject{
                 {"mime", mime},
                 {"name", "replay-decoder"},
                 {"profilesComplete", true},
                 {"profiles", QJsonArray{QJsonObject{{"profile", profile},
                                                     {"level", level}}}}}}}}
            .toVariantMap();
    return options;
  }
  static QJsonObject source(QJsonObject body) {
    return body.value("MediaSources").toArray().first().toObject();
  }
  static QJsonObject withSource(QJsonObject body, QJsonObject value) {
    body.insert("MediaSources", QJsonArray{value});
    return body;
  }
private slots:
  void admission() {
    const QUrl base("https://example.test/proxy");
    QCOMPARE(HomeServerPlaybackJob::admittedUrl(
                 QUrl("https://example.test/base%20name"), "Videos/file%20name")
                 .toEncoded(),
             QByteArray("https://example.test/base%20name/Videos/file%20name"));
    QCOMPARE(HomeServerPlaybackJob::admittedUrl(
                 base, "Videos/a?api_key=secret&Static=true")
                 .toString(),
             QString("https://example.test/proxy/Videos/a?Static=true"));
    QCOMPARE(
        HomeServerPlaybackJob::admittedUrl(base, "/proxy/Videos/a").toString(),
        QString("https://example.test/proxy/Videos/a"));
    QCOMPARE(HomeServerPlaybackJob::admittedUrl(base, "/Videos/a").toString(),
             QString("https://example.test/proxy/Videos/a"));
    QCOMPARE(HomeServerPlaybackJob::admittedUrl(
                 base,
                 "https://example.test/proxy/"
                 "a?ToKeN=s&ACCESS_TOKEN=s&X-Plex-Token=s&api_KEY=s&ok=yes")
                 .toString(),
             QString("https://example.test/proxy/a?ok=yes"));
    for (const auto &path :
         {"https://evil.test/x", "http://example.test/x",
          "https://example.test:8443/x", "https://user@example.test/x",
          "//evil.test/x", "x#fragment", "../private", "%2e%2e/private",
          "%252e%252e/private", "x%5cprivate", "x/./part", "file:///tmp/media"})
      QVERIFY2(HomeServerPlaybackJob::admittedUrl(base, path).isEmpty(), path);
  }
  void deviceProfiles() {
    auto desktop = HomeServerPlaybackJob::deviceProfile({});
    QVERIFY(!desktop.value("DirectPlayProfiles").toArray().isEmpty());
    auto capable = HomeServerPlaybackJob::deviceProfile(android());
    auto dp = capable.value("DirectPlayProfiles").toArray().first().toObject();
    QCOMPARE(dp.value("VideoCodec").toString(), "h264");
    QVERIFY(!dp.value("AudioCodec").toString().contains("dts"));
    QVERIFY(!dp.value("AudioCodec").toString().contains("ac3"));
    HomeServerPlaybackOptions unknown;
    unknown.android = true;
    auto fallback = HomeServerPlaybackJob::deviceProfile(unknown);
    QVERIFY(fallback.value("DirectPlayProfiles").toArray().isEmpty());
    QCOMPARE(fallback.value("TranscodingProfiles")
                 .toArray()
                 .first()
                 .toObject()
                 .value("VideoCodec")
                 .toString(),
             "h264");
    auto incomplete = android();
    incomplete.videoDecodeCapabilities["complete"] = false;
    QVERIFY(HomeServerPlaybackJob::deviceProfile(incomplete)
                .value("DirectPlayProfiles")
                .toArray()
                .isEmpty());
  }
  void jellyfinEmby_data() {
    QTest::addColumn<QString>("provider");
    QTest::addColumn<bool>("transcode");
    QTest::newRow("jellyfin-direct") << QString("jellyfin") << false;
    QTest::newRow("emby-direct") << QString("emby") << false;
    QTest::newRow("jellyfin-transcode") << QString("jellyfin") << true;
    QTest::newRow("emby-transcode") << QString("emby") << true;
  }
  void jellyfinEmby() {
    QFETCH(QString, provider);
    QFETCH(bool, transcode);
    PlaybackReplay server;
    server.replies << PlaybackReplay::Reply{
        200, PlaybackReplay::fixture("jellyfin-negotiated.json")};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    HomeServerPlaybackJob::Result result;
    bool done = false;
    HomeServerPlaybackOptions options;
    options.forceTranscode = transcode;
    job.start(connection(server, provider), item(), "version-a", "REPLAY-TOKEN",
              options, [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.mediaSourceId, QString("version-a"));
    QCOMPARE(result.playSessionId, QString("session-replay"));
    QCOMPARE(result.transcoded, transcode);
    QVERIFY(!result.url.contains("REPLAY"));
    QVERIFY(!result.url.contains("api_key"));
    QVERIFY(!result.url.contains("proxy/proxy"));
    if (provider == "jellyfin") {
      QCOMPARE(result.headers.value("Authorization").toString(),
               QString("MediaBrowser Token=\"REPLAY-TOKEN\""));
      QVERIFY(!result.headers.value("Authorization").toString().contains(','));
    } else
      QCOMPARE(result.headers.value("X-Emby-Token").toString(),
               QString("REPLAY-TOKEN"));
    QCOMPARE(server.requests.size(), 1);
    QVERIFY(server.requests.first().startsWith(
        "POST /proxy/Items/movie/PlaybackInfo?userId=user "));
    auto request = server.requests.first();
    auto body =
        QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4))
            .object();
    QCOMPARE(body.value("StartTimeTicks").toInt(-1), 0);
    QCOMPARE(body.value("MediaSourceId").toString(), QString("version-a"));
    QVERIFY(body.value("AutoOpenLiveStream").toBool());
  }
  void negotiatedErrors_data() {
    QTest::addColumn<QString>("mode");
    for (const auto &mode : {"missing-sources", "different-version",
                             "cross-origin", "unauthorized", "no-decision",
                             "live", "missing-session", "partial-timeline"})
      QTest::newRow(mode) << QString(mode);
  }
  void negotiatedErrors() {
    QFETCH(QString, mode);
    PlaybackReplay server;
    auto body = PlaybackReplay::fixture("jellyfin-negotiated.json");
    auto s = source(body);
    int status = 200;
    if (mode == "missing-sources")
      body.remove("MediaSources");
    if (mode == "different-version")
      s.insert("Id", "different");
    if (mode == "cross-origin")
      s.insert("TranscodingUrl",
               "https://evil.test/stream?api_key=REPLAY-TOKEN");
    if (mode == "unauthorized")
      status = 401;
    if (mode == "no-decision") {
      s.insert("SupportsTranscoding", false);
      s.insert("TranscodingUrl", "");
    }
    if (mode == "live")
      s.insert("RequiresOpening", true);
    if (mode == "missing-session")
      body.remove("PlaySessionId");
    if (mode == "partial-timeline")
      s.insert("TranscodingUrl",
               "Videos/movie/master.m3u8?StartTimeTicks=1000");
    if (mode != "missing-sources")
      body = withSource(body, s);
    server.replies << PlaybackReplay::Reply{status, body};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    HomeServerPlaybackOptions options;
    options.forceTranscode = true;
    job.start(connection(server), item(), "version-a", "REPLAY-TOKEN", options,
              [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY(!result.ok());
    QVERIFY(result.url.isEmpty());
    QVERIFY(result.headers.isEmpty());
    QVERIFY(!result.error.contains("REPLAY"));
    QCOMPARE(result.expired, mode == "unauthorized");
    QCOMPARE(server.requests.size(), 1);
  }
  void exactCachedVersion() {
    PlaybackReplay server;
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    job.start(connection(server), item(), "absent", "REPLAY-TOKEN", {},
              [&](auto r) {
                result = r;
                done = true;
              });
    QVERIFY(done);
    QVERIFY(!result.ok());
    QVERIFY(server.requests.isEmpty());
  }
  void androidDecisions_data() {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("direct");
    QTest::newRow("proven-h264-high-level41") << QString("known") << true;
    QTest::newRow("no-evidence") << QString("unknown") << false;
    QTest::newRow("wrong-profile") << QString("profile") << false;
    QTest::newRow("insufficient-level") << QString("level") << false;
    QTest::newRow("incomplete") << QString("incomplete") << false;
    QTest::newRow("dolby-vision") << QString("dv") << false;
  }
  void androidDecisions() {
    QFETCH(QString, mode);
    QFETCH(bool, direct);
    PlaybackReplay server;
    auto body = PlaybackReplay::fixture("jellyfin-negotiated.json");
    auto options = android();
    if (mode == "unknown")
      options.videoDecodeCapabilities = {};
    if (mode == "profile")
      options = android("video/avc", "baseline");
    if (mode == "level")
      options = android("video/avc", "high", 3.1);
    if (mode == "incomplete")
      options.videoDecodeCapabilities["complete"] = false;
    if (mode == "dv") {
      auto s = source(body);
      auto streams = s.value("MediaStreams").toArray();
      auto video = streams.first().toObject();
      video.insert("DvProfile", 8);
      streams.replace(0, video);
      s.insert("MediaStreams", streams);
      body = withSource(body, s);
    }
    server.replies << PlaybackReplay::Reply{200, body};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    job.start(connection(server), item(), "version-a", "REPLAY-TOKEN", options,
              [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY(result.ok());
    QCOMPARE(result.playMethod,
             direct ? QString("DirectPlay") : QString("Transcode"));
  }
  void directStreamAndLegacySession() {
    PlaybackReplay server;
    auto body = PlaybackReplay::fixture("jellyfin-negotiated.json");
    auto s = source(body);
    s.insert("SupportsDirectPlay", false);
    body = withSource(body, s);
    body.remove("PlaySessionId");
    server.replies << PlaybackReplay::Reply{200, body};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    auto c = connection(server);
    c.insert("serverMajor", 11);
    job.start(c, item(), "version-a", "REPLAY-TOKEN", {}, [&](auto r) {
      result = r;
      done = true;
    });
    QTRY_VERIFY(done);
    QVERIFY(result.ok());
    QCOMPARE(result.playMethod, QString("DirectStream"));
    QVERIFY(!result.playSessionId.isEmpty());
    QVERIFY(!result.url.contains("api_key"));
  }
  void negotiatedQueryConsistency_data() {
    QTest::addColumn<QString>("query");
    QTest::addColumn<bool>("androidDevice");
    QTest::addColumn<bool>("success");
    QTest::newRow("matching-native-context")
        << QString("MediaSourceId=version-a&PlaySessionId=session-replay")
        << false << true;
    QTest::newRow("wrong-source")
        << QString("MediaSourceId=version-other&PlaySessionId=session-replay")
        << false << false;
    QTest::newRow("wrong-session")
        << QString("MediaSourceId=version-a&PlaySessionId=session-other")
        << false << false;
    QTest::newRow("duplicate-source-contradiction")
        << QString("MediaSourceId=version-a&mediasourceid=version-other")
        << false << false;
    QTest::newRow("matching-android-codecs")
        << QString("MediaSourceId=version-a&PlaySessionId=session-replay&"
                   "VideoCodec=h264&AudioCodec=aac")
        << true << true;
    QTest::newRow("normalized-android-codecs")
        << QString("VideoCodec=AVC&AudioCodec=AAC") << true << true;
    QTest::newRow("wrong-android-video")
        << QString("VideoCodec=hevc&AudioCodec=aac") << true << false;
    QTest::newRow("wrong-android-audio")
        << QString("VideoCodec=h264&AudioCodec=dts") << true << false;
    QTest::newRow("android-video-copy")
        << QString("VideoCodec=copy&AudioCodec=aac") << true << false;
    QTest::newRow("android-empty-video")
        << QString("VideoCodec=&AudioCodec=aac") << true << false;
  }
  void negotiatedQueryConsistency() {
    QFETCH(QString, query);
    QFETCH(bool, androidDevice);
    QFETCH(bool, success);
    PlaybackReplay server;
    auto body = PlaybackReplay::fixture("jellyfin-negotiated.json");
    auto mediaSource = source(body);
    mediaSource.insert("TranscodingUrl", "Videos/movie/master.m3u8?" + query);
    server.replies << PlaybackReplay::Reply{200, withSource(body, mediaSource)};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    HomeServerPlaybackOptions options;
    options.android = androidDevice;
    options.forceTranscode = true;
    bool done = false;
    HomeServerPlaybackJob::Result result;
    job.start(connection(server), item(), "version-a", "REPLAY-TOKEN", options,
              [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QCOMPARE(result.ok(), success);
    QCOMPARE(server.requests.size(), 1);
    if (success) {
      QCOMPARE(result.mediaSourceId, QString("version-a"));
      QCOMPARE(result.playSessionId, QString("session-replay"));
      QCOMPARE(QUrlQuery(QUrl(result.url)).queryItemValue("MaxWidth"),
               QString("3840"));
    } else {
      QVERIFY(result.url.isEmpty());
      QVERIFY(result.headers.isEmpty());
      QVERIFY(!result.error.isEmpty());
      QVERIFY(!result.error.contains("REPLAY"));
    }
  }
  void plexSelection_data() {
    QTest::addColumn<bool>("transcode");
    QTest::newRow("direct-third-part") << false;
    QTest::newRow("transcode-third-part") << true;
  }
  void plexAndroid_data() {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("direct");
    QTest::newRow("proven-hevc-main10") << QString("known") << true;
    QTest::newRow("unknown-device") << QString("unknown") << false;
    QTest::newRow("unproven-audio") << QString("dts") << false;
  }
  void plexAndroid() {
    QFETCH(QString, mode);
    QFETCH(bool, direct);
    PlaybackReplay server;
    auto metadata = PlaybackReplay::fixture("plex-versions.json");
    if (mode == "dts") {
      auto container = metadata.value("MediaContainer").toObject();
      auto rows = container.value("Metadata").toArray();
      auto row = rows.first().toObject();
      auto mediaList = row.value("Media").toArray();
      auto media = mediaList.at(1).toObject();
      auto parts = media.value("Part").toArray();
      auto part = parts.at(1).toObject();
      auto streams = part.value("Stream").toArray();
      auto audio = streams.at(1).toObject();
      audio.insert("codec", "dts");
      streams.replace(1, audio);
      part.insert("Stream", streams);
      parts.replace(1, part);
      media.insert("Part", parts);
      mediaList.replace(1, media);
      row.insert("Media", mediaList);
      rows.replace(0, row);
      container.insert("Metadata", rows);
      metadata.insert("MediaContainer", container);
    }
    server.replies << PlaybackReplay::Reply{200, metadata};
    if (!direct)
      server.replies << PlaybackReplay::Reply{
          200, PlaybackReplay::fixture("plex-decision.json")};
    auto options = android("video/hevc", "main10", 0);
    if (mode == "unknown")
      options.videoDecodeCapabilities = {};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    const QString selected = "/library/parts/third/file.mkv";
    bool done = false;
    HomeServerPlaybackJob::Result result;
    job.start(connection(server, "plex"), item(selected, 1, 1), selected,
              "REPLAY-PMS-TOKEN", options, [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.transcoded, !direct);
    QCOMPARE(server.requests.size(), direct ? 1 : 2);
    QVERIFY(!result.url.contains("TOKEN"));
  }
  void plexSelection() {
    QFETCH(bool, transcode);
    PlaybackReplay server;
    server.replies << PlaybackReplay::Reply{
        200, PlaybackReplay::fixture("plex-versions.json")};
    if (transcode)
      server.replies << PlaybackReplay::Reply{
          200, PlaybackReplay::fixture("plex-decision.json")};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    HomeServerPlaybackOptions options;
    options.forceTranscode = transcode;
    const QString selected = "/library/parts/third/file.mkv";
    job.start(connection(server, "plex"), item(selected, 1, 1), selected,
              "REPLAY-PMS-TOKEN", options, [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.mediaSourceId, selected);
    QCOMPARE(result.transcoded, transcode);
    QVERIFY(!result.url.contains("TOKEN"));
    QCOMPARE(result.headers.value("X-Plex-Token").toString(),
             QString("REPLAY-PMS-TOKEN"));
    QVERIFY(
        !result.headers.value("X-Plex-Client-Identifier").toString().isEmpty());
    QCOMPARE(server.requests.size(), transcode ? 2 : 1);
    QVERIFY(server.requests.first().startsWith(
        "GET /proxy/library/metadata/movie?"));
    if (transcode) {
      QUrl url(result.url);
      auto q = QUrlQuery(url);
      QCOMPARE(q.queryItemValue("mediaIndex"), QString("1"));
      QCOMPARE(q.queryItemValue("partIndex"), QString("1"));
      QCOMPARE(q.queryItemValue("offset"), QString("0"));
      QCOMPARE(q.queryItemValue("directPlay"), QString("0"));
      QCOMPARE(q.queryItemValue("maxVideoBitrate"), QString("100000"));
      QVERIFY(q.queryItemValue("X-Plex-Client-Profile-Extra")
                  .contains("videoCodec=h264"));
      QVERIFY(server.requests.last().startsWith(
          "GET /proxy/video/:/transcode/universal/decision?"));
    } else
      QCOMPARE(QUrl(result.url).path(),
               QString("/proxy/library/parts/third/file.mkv"));
  }
  void plexRejectsMalformedDecision_data() {
    QTest::addColumn<QString>("mode");
    for (const auto &mode : {"malformed", "denied", "wrong-version",
                             "wrong-indices", "unauthorized"})
      QTest::newRow(mode) << QString(mode);
  }
  void plexRejectsMalformedDecision() {
    QFETCH(QString, mode);
    PlaybackReplay server;
    auto metadata = PlaybackReplay::fixture("plex-versions.json");
    server.replies << PlaybackReplay::Reply{mode == "unauthorized" ? 401 : 200,
                                            metadata};
    QJsonObject decision =
        mode == "denied"
            ? QJsonObject{{"MediaContainer",
                           QJsonObject{
                               {"generalDecisionCode", 2011},
                               {"generalDecisionText", "REPLAY-DO-NOT-ECHO"}}}}
            : QJsonObject{
                  {"MediaContainer", QJsonObject{{"Metadata", QJsonArray{}}}}};
    server.replies << PlaybackReplay::Reply{200, decision};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    HomeServerPlaybackJob job(&client);
    bool done = false;
    HomeServerPlaybackJob::Result result;
    HomeServerPlaybackOptions options;
    options.forceTranscode = true;
    auto version = mode == "wrong-version"
                       ? QString("/library/parts/missing/file.mkv")
                       : QString("/library/parts/third/file.mkv");
    job.start(connection(server, "plex"),
              item(version, mode == "wrong-indices" ? 0 : 1, 1), version,
              "REPLAY-PMS-TOKEN", options, [&](auto r) {
                result = r;
                done = true;
              });
    QTRY_VERIFY(done);
    QVERIFY(!result.ok());
    QVERIFY(result.url.isEmpty());
    QVERIFY(!result.error.contains("REPLAY"));
    QCOMPARE(result.expired, mode == "unauthorized");
  }
  void cancellationAndLifetime() {
    PlaybackReplay server;
    server.replies << PlaybackReplay::Reply{
        200, PlaybackReplay::fixture("jellyfin-negotiated.json"), 100};
    QTemporaryDir dir;
    HomeServerClient client(nullptr, dir.path());
    bool called = false;
    auto *job = new HomeServerPlaybackJob(&client);
    job->start(connection(server), item(), "version-a", "REPLAY-TOKEN", {},
               [&](auto) { called = true; });
    QTRY_COMPARE(server.requests.size(), 1);
    job->cancel();
    delete job;
    QTest::qWait(150);
    QVERIFY(!called);
    server.replies << PlaybackReplay::Reply{
        200, PlaybackReplay::fixture("jellyfin-negotiated.json"), 100};
    job = new HomeServerPlaybackJob(&client);
    job->start(connection(server), item(), "version-a", "REPLAY-TOKEN", {},
               [&](auto) { called = true; });
    QTRY_COMPARE(server.requests.size(), 2);
    delete job;
    QTest::qWait(150);
    QVERIFY(!called);
  }
};
QTEST_GUILESS_MAIN(PlaybackTest)
#include "tst_home_server_playback.moc"
