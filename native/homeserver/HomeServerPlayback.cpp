#include "HomeServerPlayback.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
QString decoded(QString text) {
  for (int i = 0; i < 16; ++i) {
    auto next = QUrl::fromPercentEncoding(text.toUtf8());
    if (next == text)
      return text;
    text = next;
  }
  return {};
}
bool safePath(QString path) {
  path = decoded(path);
  if (path.isEmpty() ||
      path.contains(QRegularExpression("[\\x00-\\x1f\\x7f\\\\]")))
    return false;
  for (const auto &segment : path.split('/'))
    if (segment == "." || segment == "..")
      return false;
  return true;
}
bool validOrigin(const QUrl &u) {
  return u.isValid() && (u.scheme() == "https" || u.scheme() == "http") &&
         !u.host().isEmpty() && u.userInfo().isEmpty() && !u.hasQuery() &&
         !u.hasFragment() && u.port() != 0 &&
         (u.path().isEmpty() || safePath(u.path(QUrl::FullyEncoded)));
}
int port(const QUrl &u) { return u.port(u.scheme() == "https" ? 443 : 80); }
QString escapedSegment(QString id) {
  return QString::fromLatin1(QUrl::toPercentEncoding(id));
}
bool safeId(const QString &id) {
  return !id.isEmpty() && id.size() <= 4096 &&
         !id.contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
QString codec(QString value) {
  value = value.toLower();
  if (value == "avc" || value == "avc1")
    return "h264";
  if (value == "h265" || value == "hev1" || value == "hvc1")
    return "hevc";
  if (value == "av01")
    return "av1";
  return value;
}
QString mime(QString value) {
  value = codec(value);
  if (value == "h264")
    return "video/avc";
  if (value == "hevc")
    return "video/hevc";
  if (value == "av1")
    return "video/av01";
  return {};
}
QString profile(QString value) {
  value = value.toLower().trimmed();
  value.remove(' ');
  value.remove('-');
  value.remove('_');
  if (value == "constrainedbaseline")
    return "baseline";
  if (value == "constrainedhigh")
    return "high";
  if (value == "mainstillpicture" || value == "mainstill")
    return "main-still";
  return value;
}
QJsonArray decoders(const HomeServerPlaybackOptions &options) {
  const auto cap = QJsonObject::fromVariantMap(options.videoDecodeCapabilities);
  if (!cap.value("available").toBool() || !cap.value("complete").toBool())
    return {};
  return cap.value("decoders").toArray();
}
bool proven(QString video, QString declaredProfile, double level,
            const HomeServerPlaybackOptions &options) {
  const auto type = mime(video);
  if (type.isEmpty() || declaredProfile.isEmpty())
    return false;
  const auto wanted = profile(declaredProfile);
  for (const auto &value : decoders(options)) {
    const auto d = value.toObject();
    if (d.value("mime") != type || !d.value("profilesComplete").toBool())
      continue;
    for (const auto &entry : d.value("profiles").toArray()) {
      const auto p = entry.toObject();
      if (profile(p.value("profile").toString()) != wanted)
        continue;
      // Android enumeration exposes numeric AVC levels only. Do not invent
      // HEVC/AV1 level evidence.
      double maxLevel = p.value("level").toDouble();
      double required = level;
      if (required > 10)
        required /= 10;
      if (level > 0 && (maxLevel <= 0 || required > maxLevel))
        continue;
      return true;
    }
  }
  return false;
}
bool supported(const QJsonObject &media, QJsonArray streams, bool plex,
               const HomeServerPlaybackOptions &options) {
  if (!options.android) {
    if (!plex)
      return true; // PlaybackInfo evaluated the advertised mpv profile.
    return QStringList{"mkv", "matroska", "mp4", "m4v",  "mov", "webm",
                       "ts",  "mpegts",   "avi", "m2ts", "ogg"}
               .contains(media.value("container").toString().toLower()) &&
           QStringList{"h264",  "hevc",       "av1",   "vp8", "vp9",
                       "mpeg4", "mpeg2video", "mpeg2", "vc1", "theora"}
               .contains(codec(media.value("videoCodec").toString())) &&
           QStringList{"aac",  "mp3", "flac", "vorbis", "opus", "ac3",
                       "eac3", "dts", "dca",  "truehd", "pcm"}
               .contains(codec(media.value("audioCodec").toString()));
  }
  const auto container =
      media.value(plex ? "container" : "Container").toString().toLower();
  if (!QStringList{"mkv", "matroska", "mp4", "m4v", "mov", "webm", "ts",
                   "mpegts"}
           .contains(container))
    return false;
  bool hasVideo = false, hasAudio = false;
  for (const auto &value : streams) {
    const auto stream = value.toObject();
    const auto type = plex ? QString::number(stream.value("streamType").toInt())
                           : stream.value("Type").toString().toLower();
    const auto c = codec(stream.value(plex ? "codec" : "Codec").toString());
    if (type == "1" || type == "video") {
      if (hasVideo)
        return false;
      hasVideo = true;
      // Dolby Vision's raw Android profile constants cannot safely be equated
      // with server bitstream profile numbers.
      if (stream.value(plex ? "DOVIPresent" : "DolbyVisionPresent").toBool() ||
          stream.value(plex ? "DOVIProfile" : "DvProfile").toInt() > 0 ||
          stream.value(plex ? "displayTitle" : "VideoRangeType")
              .toString()
              .contains("dolby", Qt::CaseInsensitive) ||
          stream.value("VideoRangeType")
              .toString()
              .startsWith("DOVI", Qt::CaseInsensitive))
        return false;
      if (!proven(c, stream.value(plex ? "profile" : "Profile").toString(),
                  stream.value(plex ? "level" : "Level").toDouble(), options))
        return false;
    } else if (type == "2" || type == "audio") {
      hasAudio = true;
      if (!QStringList{"aac", "mp3", "flac", "vorbis", "opus"}.contains(c))
        return false;
    }
  }
  return hasVideo && hasAudio;
}
QJsonObject firstObject(QJsonArray rows, const QString &key,
                        const QString &id) {
  QJsonObject found;
  for (const auto &v : rows)
    if (v.isObject() && v.toObject().value(key).toVariant().toString() == id) {
      if (!found.isEmpty())
        return {};
      found = v.toObject();
    }
  return found;
}
QString session() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool safeToken(const QString &token) {
  return !token.isEmpty() && token.size() <= 65536 &&
         !token.contains(QRegularExpression("[\\x00-\\x1f\\x7f,]"));
}
} // namespace

QUrl HomeServerPlaybackJob::admittedUrl(QUrl origin, QString path) {
  if (!validOrigin(origin) || path.isEmpty() || path.size() > 32768 ||
      path.contains(QRegularExpression("[\\x00-\\x1f\\x7f\\\\]")))
    return {};
  QUrl candidate(path, QUrl::StrictMode);
  if (!candidate.isValid() || candidate.hasFragment() ||
      !candidate.userInfo().isEmpty() ||
      !safePath(candidate.path(QUrl::FullyEncoded)))
    return {};
  if (candidate.isRelative()) {
    if (!candidate.host().isEmpty() || path.startsWith("//"))
      return {};
    QString base = origin.path(QUrl::FullyEncoded);
    while (base.endsWith('/'))
      base.chop(1);
    QString relative = candidate.path(QUrl::FullyEncoded);
    if (!base.isEmpty() &&
        (relative == base || relative.startsWith(base + '/')))
      origin.setPath(relative, QUrl::StrictMode);
    else {
      while (relative.startsWith('/'))
        relative.remove(0, 1);
      origin.setPath(base + '/' + relative, QUrl::StrictMode);
    }
    origin.setQuery(candidate.query(QUrl::FullyEncoded));
    candidate = origin;
  } else if (candidate.scheme() != origin.scheme() ||
             candidate.host().compare(origin.host(), Qt::CaseInsensitive) !=
                 0 ||
             port(candidate) != port(origin))
    return {};
  if (candidate.scheme() != "http" && candidate.scheme() != "https")
    return {};
  QUrlQuery clean;
  const QSet<QString> credentialKeys{"api_key", "token", "access_token",
                                     "x-plex-token"};
  for (const auto &entry : QUrlQuery(candidate).queryItems(QUrl::FullyDecoded))
    if (!credentialKeys.contains(decoded(entry.first).toLower()))
      clean.addQueryItem(entry.first, entry.second);
  candidate.setQuery(clean);
  return candidate;
}
QJsonObject
HomeServerPlaybackJob::deviceProfile(HomeServerPlaybackOptions options) {
  QJsonArray direct, codecs;
  QStringList videos;
  if (!options.android)
    videos = {"h264",  "hevc",       "av1", "vp8",   "vp9",
              "mpeg4", "mpeg2video", "vc1", "theora"};
  else
    for (const auto &c : {QString("h264"), QString("hevc"), QString("av1")}) {
      QStringList profiles;
      double maxLevel = 0;
      for (const auto &entry : decoders(options)) {
        auto d = entry.toObject();
        if (d.value("mime") != mime(c) || !d.value("profilesComplete").toBool())
          continue;
        for (const auto &p : d.value("profiles").toArray()) {
          auto value = p.toObject();
          auto name = value.value("profile").toString();
          if (name.isEmpty())
            continue;
          profiles << name;
          maxLevel = std::max(maxLevel, value.value("level").toDouble());
        }
      }
      profiles.removeDuplicates();
      if (profiles.isEmpty())
        continue;
      videos << c;
      QJsonArray conditions{QJsonObject{{"Condition", "EqualsAny"},
                                        {"Property", "VideoProfile"},
                                        {"Value", profiles.join('|')},
                                        {"IsRequired", true}}};
      if (c == "h264" && maxLevel > 0)
        conditions.append(
            QJsonObject{{"Condition", "LessThanEqual"},
                        {"Property", "VideoLevel"},
                        {"Value", QString::number(maxLevel * 10, 'f', 0)},
                        {"IsRequired", true}});
      codecs.append(QJsonObject{
          {"Type", "Video"}, {"Codec", c}, {"Conditions", conditions}});
    }
  if (!options.forceTranscode && !videos.isEmpty())
    direct.append(QJsonObject{
        {"Container", "mkv,mp4,m4v,mov,webm,ts,mpegts,avi,m2ts,ogg"},
        {"Type", "Video"},
        {"VideoCodec", videos.join(',')},
        {"AudioCodec",
         options.android
             ? "aac,mp3,flac,vorbis,opus"
             : "aac,mp3,flac,vorbis,opus,ac3,eac3,dts,truehd,pcm"}});
  return {{"Name", "Colosseum"},
          {"MaxStreamingBitrate", 100000000},
          {"MaxStaticBitrate", 100000000},
          {"DirectPlayProfiles", direct},
          {"CodecProfiles", codecs},
          {"TranscodingProfiles",
           QJsonArray{QJsonObject{{"Container", "ts"},
                                  {"Type", "Video"},
                                  {"VideoCodec", "h264"},
                                  {"AudioCodec", "aac"},
                                  {"Protocol", "hls"},
                                  {"Context", "Streaming"},
                                  {"MaxAudioChannels", "2"},
                                  {"MinSegments", 1},
                                  {"BreakOnNonKeyFrames", true}}}},
          {"SubtitleProfiles",
           QJsonArray{QJsonObject{{"Format", "srt"}, {"Method", "External"}},
                      QJsonObject{{"Format", "ass"}, {"Method", "External"}},
                      QJsonObject{{"Format", "vtt"}, {"Method", "External"}}}}};
}
HomeServerPlaybackJob::HomeServerPlaybackJob(HomeServerClient *client,
                                             QObject *parent)
    : QObject(parent), m_client(client) {}
HomeServerPlaybackJob::~HomeServerPlaybackJob() { cancel(); }
void HomeServerPlaybackJob::cancel() {
  m_finished = true;
  m_callback = {};
  m_token.clear();
  m_connection = {};
  m_item = {};
  m_version = {};
  m_origin = {};
  m_session.clear();
  m_options = {};
}
void HomeServerPlaybackJob::finish(Result result) {
  if (m_finished)
    return;
  auto cb = std::move(m_callback);
  cancel();
  if (cb)
    cb(std::move(result));
}
void HomeServerPlaybackJob::fail(QString error, bool expired) {
  Result r;
  r.error = std::move(error);
  r.expired = expired;
  finish(std::move(r));
}
void HomeServerPlaybackJob::start(QJsonObject connection, QJsonObject item,
                                  QString versionId, QString token,
                                  HomeServerPlaybackOptions options,
                                  Callback callback) {
  if (m_started || m_finished)
    return;
  m_started = true;
  m_connection = std::move(connection);
  m_item = std::move(item);
  m_token = std::move(token);
  m_options = std::move(options);
  m_callback = std::move(callback);
  m_origin = QUrl(m_connection.value("origin").toString(), QUrl::StrictMode);
  const auto provider = m_connection.value("provider").toString();
  if (!m_client || !validOrigin(m_origin) || !safeToken(m_token) ||
      !safeId(m_item.value("id").toString()) ||
      !QStringList{"jellyfin", "emby", "plex"}.contains(provider)) {
    fail("Invalid home-server playback request.");
    return;
  }
  const auto versions = m_item.value("versions").toArray();
  if (versionId.isEmpty() && !versions.isEmpty())
    versionId = versions.first().toObject().value("id").toString();
  m_version = firstObject(versions, "id", versionId);
  if (m_version.isEmpty() || !safeId(versionId)) {
    fail("The selected media version is unavailable. Refresh the server "
         "library.");
    return;
  }
  m_session = session();
  if (provider == "plex")
    plex();
  else
    jellyfin();
}
void HomeServerPlaybackJob::request(QString path, QByteArray method,
                                    QJsonObject body,
                                    std::function<void(QJsonObject)> callback) {
  if (m_finished || !m_client)
    return;
  QPointer<HomeServerPlaybackJob> self(this);
  m_client->request(m_connection.value("provider").toString(), m_origin,
                    std::move(path), std::move(method), m_token, body,
                    [self, callback = std::move(callback)](
                        HomeServerClient::Response response) {
                      if (!self || self->m_finished)
                        return;
                      if (!response.ok()) {
                        self->fail(response.expired()
                                       ? "Server sign-in has expired."
                                       : "Could not negotiate server playback.",
                                   response.expired());
                        return;
                      }
                      if (!response.body.isObject()) {
                        self->fail("Invalid server playback response.");
                        return;
                      }
                      callback(response.body.toObject());
                    });
}
QVariantMap HomeServerPlaybackJob::playbackHeaders() const {
  const auto provider = m_connection.value("provider").toString();
  if (provider == "plex")
    return {{"X-Plex-Token", m_token},
            {"X-Plex-Client-Identifier",
             m_client ? m_client->installationId() : QString{}}};
  if (provider == "emby")
    return {{"X-Emby-Token", m_token}};
  auto escaped = m_token;
  escaped.replace('\\', "\\\\");
  escaped.replace('"', "\\\"");
  return {{"Authorization", "MediaBrowser Token=\"" + escaped + '"'}};
}
void HomeServerPlaybackJob::jellyfin() {
  const auto id = m_item.value("id").toString(),
             selected = m_version.value("id").toString(),
             user = m_connection.value("userId").toString();
  if (!safeId(user)) {
    fail("Invalid server user.");
    return;
  }
  QUrlQuery query;
  query.addQueryItem("userId", user);
  QJsonObject body{
      {"UserId", user},
      {"MediaSourceId", selected},
      {"StartTimeTicks", 0},
      {"AutoOpenLiveStream", true},
      {"EnableDirectPlay", !m_options.forceTranscode},
      {"EnableDirectStream", !m_options.forceTranscode},
      {"EnableTranscoding", true},
      {"AllowVideoStreamCopy", !m_options.android && !m_options.forceTranscode},
      {"AllowAudioStreamCopy", !m_options.android && !m_options.forceTranscode},
      {"MaxStreamingBitrate", 100000000},
      {"DeviceProfile", deviceProfile(m_options)}};
  request(
      "Items/" + escapedSegment(id) + "/PlaybackInfo?" +
          query.toString(QUrl::FullyEncoded),
      "POST", body, [this, id, selected](QJsonObject response) {
        if (!response.value("MediaSources").isArray()) {
          fail("The server did not provide media sources.");
          return;
        }
        const auto source = firstObject(
            response.value("MediaSources").toArray(), "Id", selected);
        if (source.isEmpty()) {
          fail("The selected media version changed. Refresh the server "
               "library.");
          return;
        }
        if (source.value("RequiresOpening").toBool() ||
            !source.value("LiveStreamId").toString().isEmpty() ||
            source.value("IsInfiniteStream").toBool()) {
          fail("Live or unopened server streams are not supported.");
          return;
        }
        auto playSession = response.value("PlaySessionId").toString();
        if (response.contains("PlaySessionId") &&
            (!response.value("PlaySessionId").isString() ||
             !QRegularExpression("^[A-Za-z0-9_-]{1,256}$")
                  .match(playSession)
                  .hasMatch())) {
          fail("Invalid server playback session.");
          return;
        }
        if (playSession.isEmpty()) {
          int major = m_connection.value("serverMajor").toInt();
          if (m_connection.value("provider") != "emby" &&
              (major != 10 && major != 11)) {
            fail("The server did not provide a playback session.");
            return;
          }
          playSession = m_session;
        }
        if (!safeId(playSession)) {
          fail("Invalid server playback session.");
          return;
        }
        const bool native = supported(
            source, source.value("MediaStreams").toArray(), false, m_options);
        QString path, method;
        bool transcoded = false;
        if (!m_options.forceTranscode && native &&
            source.value("SupportsDirectPlay").toBool()) {
          QUrlQuery q;
          q.addQueryItem("Static", "true");
          q.addQueryItem("MediaSourceId", selected);
          path = "Videos/" + escapedSegment(id) + "/stream?" +
                 q.toString(QUrl::FullyEncoded);
          method = "DirectPlay";
        } else if (!m_options.forceTranscode && native &&
                   source.value("SupportsDirectStream").toBool() &&
                   !source.value("DirectStreamUrl").toString().isEmpty()) {
          path = source.value("DirectStreamUrl").toString();
          method = "DirectStream";
        } else if (source.value("SupportsTranscoding").toBool() &&
                   !source.value("TranscodingUrl").toString().isEmpty()) {
          path = source.value("TranscodingUrl").toString();
          method = "Transcode";
          transcoded = true;
        } else {
          fail("The server could not provide a supported playback stream.");
          return;
        }
        auto url = admittedUrl(m_origin, path);
        if (url.isEmpty()) {
          fail("The server returned an unsafe playback address.");
          return;
        }
        QUrlQuery q(url);
        for (const auto &pair : q.queryItems(QUrl::FullyDecoded)) {
          const auto name = decoded(pair.first).toLower();
          if (name == "mediasourceid" && pair.second != selected) {
            fail("The server changed the selected playback version.");
            return;
          }
          if (name == "playsessionid" && pair.second != playSession) {
            fail("The server changed the playback session.");
            return;
          }
          if (transcoded && m_options.android &&
              ((name == "videocodec" && codec(pair.second) != "h264") ||
               (name == "audiocodec" && codec(pair.second) != "aac"))) {
            fail(
                "The server returned an unsupported Android transcode format.");
            return;
          }
          if (name == "starttimeticks" || name == "starttime" ||
              name == "offset") {
            bool valid = false;
            const auto value = pair.second.toDouble(&valid);
            if (!valid || value != 0) {
              fail("The server returned a partial playback timeline.");
              return;
            }
          }
        }
        if (transcoded) {
          // Retain the negotiated route; bound its output without inventing
          // a fallback endpoint.
          for (const auto &pair : q.queryItems(QUrl::FullyDecoded))
            if (pair.first.compare("MaxWidth", Qt::CaseInsensitive) == 0 ||
                pair.first.compare("MaxHeight", Qt::CaseInsensitive) == 0)
              q.removeAllQueryItems(pair.first);
          q.addQueryItem("MaxWidth", "3840");
          q.addQueryItem("MaxHeight", "2160");
          url.setQuery(q);
        }
        Result r;
        r.url = url.toString(QUrl::FullyEncoded);
        r.headers = playbackHeaders();
        r.mediaSourceId = selected;
        r.playSessionId = playSession;
        r.playMethod = method;
        r.transcoded = transcoded;
        finish(std::move(r));
      });
}
void HomeServerPlaybackJob::plex() {
  const auto id = m_item.value("id").toString(),
             selected = m_version.value("id").toString();
  request(
      "library/metadata/" + escapedSegment(id) +
          "?includeGuids=1&includeMedia=1&includeExternalMedia=1",
      "GET", {}, [this, id, selected](QJsonObject response) {
        const auto container = response.value("MediaContainer").toObject();
        const auto raw =
            firstObject(container.value("Metadata").toArray(), "ratingKey", id);
        if (raw.isEmpty() || !raw.value("Media").isArray()) {
          fail("The selected Plex item is unavailable.");
          return;
        }
        const auto mediaList = raw.value("Media").toArray();
        QJsonObject media, part;
        int mediaIndex = -1, partIndex = -1;
        for (int mi = 0; mi < mediaList.size(); ++mi) {
          auto m = mediaList.at(mi).toObject();
          auto parts = m.value("Part").toArray();
          for (int pi = 0; pi < parts.size(); ++pi)
            if (parts.at(pi).toObject().value("key") == selected) {
              if (mediaIndex >= 0) {
                fail("Ambiguous Plex media version.");
                return;
              }
              media = m;
              part = parts.at(pi).toObject();
              mediaIndex = mi;
              partIndex = pi;
            }
        }
        if (mediaIndex < 0) {
          fail(
              "The selected Plex version changed. Refresh the server library.");
          return;
        }
        if ((m_version.contains("mediaIndex") &&
             m_version.value("mediaIndex").toInt(-1) != mediaIndex) ||
            (m_version.contains("partIndex") &&
             m_version.value("partIndex").toInt(-1) != partIndex)) {
          fail(
              "The selected Plex version changed. Refresh the server library.");
          return;
        }
        const QUrl partUrl(selected, QUrl::StrictMode);
        if (!partUrl.isRelative() || !partUrl.host().isEmpty() ||
            partUrl.hasQuery() || partUrl.hasFragment() ||
            !selected.startsWith("/library/parts/") ||
            admittedUrl(m_origin, selected).isEmpty()) {
          fail("Invalid Plex media part.");
          return;
        }
        const bool direct =
            !m_options.forceTranscode &&
            part.value("supportsDirectPlay")
                .toBool(media.value("supportsDirectPlay").toBool(true)) &&
            supported(media, part.value("Stream").toArray(), true, m_options);
        if (direct) {
          Result r;
          r.url = admittedUrl(m_origin, selected).toString(QUrl::FullyEncoded);
          r.headers = playbackHeaders();
          r.mediaSourceId = selected;
          r.playSessionId = m_session;
          r.playMethod = "DirectPlay";
          finish(std::move(r));
          return;
        }
        QUrlQuery q;
        for (const auto &pair : QList<QPair<QString, QString>>{
                 {"path", "/library/metadata/" + id},
                 {"mediaIndex", QString::number(mediaIndex)},
                 {"partIndex", QString::number(partIndex)},
                 {"protocol", "hls"},
                 {"directPlay", "0"},
                 {"directStream", "0"},
                 {"hasMDE", "1"},
                 {"fastSeek", "1"},
                 {"subtitles", "none"},
                 {"location", "lan"},
                 {"session", m_session},
                 {"offset", "0"},
                 {"maxVideoBitrate", "100000"},
                 {"videoResolution", "3840x2160"},
                 {"X-Plex-Client-Profile-Name", "Generic"},
                 {"X-Plex-Platform", "Generic"},
                 {"X-Plex-Client-Profile-Extra",
                  "add-transcode-target(type=videoProfile&context=streaming&"
                  "protocol=hls&container=mpegts&videoCodec=h264&audioCodec="
                  "aac&replace=true)"}})
          q.addQueryItem(pair.first, pair.second);
        const auto params = q.toString(QUrl::FullyEncoded);
        request("video/:/transcode/universal/decision?" + params, "GET", {},
                [this, selected, params](QJsonObject decision) {
                  const auto c = decision.value("MediaContainer").toObject();
                  if (c.isEmpty()) {
                    fail("Invalid Plex playback decision.");
                    return;
                  }
                  bool accepted = false;
                  for (const auto &key :
                       {"generalDecisionCode", "transcodeDecisionCode"})
                    if (c.contains(key)) {
                      bool valid = false;
                      const int code =
                          c.value(key).toVariant().toString().toInt(&valid);
                      if (!valid || code < 1000 || code >= 2000) {
                        fail("Plex rejected the playback decision.");
                        return;
                      }
                      accepted = true;
                    }
                  if (!accepted) {
                    fail("Invalid Plex playback decision.");
                    return;
                  }
                  const auto url = admittedUrl(
                      m_origin,
                      "video/:/transcode/universal/start.m3u8?" + params);
                  if (url.isEmpty()) {
                    fail("Invalid Plex playback address.");
                    return;
                  }
                  Result r;
                  r.url = url.toString(QUrl::FullyEncoded);
                  r.headers = playbackHeaders();
                  r.mediaSourceId = selected;
                  r.playSessionId = m_session;
                  r.playMethod = "Transcode";
                  r.transcoded = true;
                  finish(std::move(r));
                });
      });
}
