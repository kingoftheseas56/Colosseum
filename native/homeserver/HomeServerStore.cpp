#include "HomeServerStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <cmath>
namespace {
constexpr qint64 MaxBytes = 64 * 1024 * 1024;
constexpr qsizetype MaxItems = 250000;
bool uuid(const QString &s) {
  return !QUuid(s).isNull() && QUuid(s).toString(QUuid::WithoutBraces) == s;
}
QString origin(const QString &raw) {
  QUrl u(raw, QUrl::StrictMode);
  if (raw.size() > 4096 || !u.isValid() ||
      (u.scheme() != "https" && u.scheme() != "http") || u.host().isEmpty() ||
      !u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment())
    return {};
  return u.adjusted(QUrl::NormalizePathSegments).toString(QUrl::FullyEncoded);
}
QJsonObject metadata(const QJsonObject &in, const QString &id,
                     bool loading = false) {
  QJsonObject out;
  const auto provider = in.value("provider").toString();
  const auto url = origin(in.value("origin").toString());
  if (!QStringList{"jellyfin", "emby", "plex"}.contains(provider) ||
      url.isEmpty())
    return {};
  for (const auto &key : {"name", "userId"}) {
    auto s = in.value(key).toString();
    if (s.trimmed().isEmpty() || s.size() > 1024)
      return {};
  }
  out.insert("id", id);
  out.insert("provider", provider);
  out.insert("origin", url);
  for (const auto &key : {"name", "userId", "userName", "serverId", "createdAt",
                          "lastRefreshAt"}) {
    auto v = in.value(key);
    if (v.isUndefined())
      continue;
    if (!v.isString() || v.toString().size() > 1024)
      return {};
    out.insert(key, v);
  }
  if (loading &&
      (!in.value("enabled").isBool() || !in.value("createdAt").isString()))
    return {};
  out.insert("enabled", in.value("enabled").isBool()
                            ? in.value("enabled").toBool()
                            : true);
  return out;
}
bool writeJson(const QString &path, const QJsonObject &value) {
  const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
  if (bytes.size() > MaxBytes)
    return false;
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return file.write(bytes) == bytes.size() && file.commit();
}
bool readJson(const QString &path, QJsonObject &out) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > MaxBytes)
    return false;
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(file.read(MaxBytes + 1), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return false;
  out = doc.object();
  return true;
}
void strings(const QJsonObject &in, QJsonObject &out,
             std::initializer_list<const char *> keys) {
  for (auto key : keys) {
    auto v = in.value(key);
    if (v.isString() && v.toString().size() <= 4096)
      out.insert(key, v);
  }
}
void numbers(const QJsonObject &in, QJsonObject &out,
             std::initializer_list<const char *> keys) {
  for (auto key : keys) {
    auto v = in.value(key);
    auto n = v.toDouble(-1);
    if (v.isDouble() && std::isfinite(n) && n >= 0 && n <= 9007199254740991.0 &&
        std::floor(n) == n)
      out.insert(key, v);
  }
}
bool cleanItems(const QJsonArray &rows, QJsonArray &out) {
  if (rows.size() > MaxItems)
    return false;
  QSet<QString> ids;
  for (auto value : rows) {
    if (!value.isObject())
      return false;
    const auto in = value.toObject();
    auto id = in.value("id").toString();
    if (id.isEmpty() || id.size() > 1024 || ids.contains(id))
      return false;
    ids.insert(id);
    QJsonObject item;
    strings(in, item,
            {
                "id",
                "libraryId",
                "libraryName",
                "parentId",
                "kind",
                "title",
            });
    numbers(in, item, {"year", "addedAt", "updatedAt"});
    if (in.value("identity").isObject()) {
      QJsonObject obj;
      const auto src = in.value("identity").toObject();
      strings(src, obj, {"imdbId"});
      numbers(src, obj, {"tmdbId", "tvdbId", "season", "episode"});
      item.insert("identity", obj);
    }
    if (in.value("progress").isObject()) {
      QJsonObject obj;
      const auto src = in.value("progress").toObject();

      numbers(src, obj, {"positionMs", "durationMs", "updatedAt"});
      if (src.value("played").isBool())
        obj.insert("played", src.value("played"));
      item.insert("progress", obj);
    }
    if (in.value("versions").isArray()) {
      QJsonArray versions;
      auto source = in.value("versions").toArray();
      if (source.size() > 256)
        return false;
      for (auto v : source) {
        if (!v.isObject())
          return false;
        const auto src = v.toObject();
        QJsonObject obj;
        strings(src, obj,
                {"id", "name", "container", "videoCodec", "audioCodec"});
        numbers(src, obj,
                {"width", "height", "bitrateKbps", "runtimeMs", "mediaIndex",
                 "partIndex"});
        if (src.value("directPlayable").isBool())
          obj.insert("directPlayable", src.value("directPlayable"));
        if (src.value("subtitles").isArray()) {
          auto subs = src.value("subtitles").toArray();
          if (subs.size() > 256)
            return false;
          QJsonArray arr;
          for (auto s : subs) {
            if (!s.isObject())
              return false;
            QJsonObject sub;
            strings(s.toObject(), sub, {"id", "codec", "language", "title"});
            numbers(s.toObject(), sub, {"index"});
            arr.append(sub);
          }
          obj.insert("subtitles", arr);
        }
        versions.append(obj);
      }
      item.insert("versions", versions);
    }
    // Artwork paths and subtitle URLs are intentionally omitted until a native
    // resolver owns them.
    out.append(item);
  }
  return true;
}
bool cleanLibraries(const QJsonArray &rows, const QJsonArray &items,
                    QJsonArray &out) {
  if (rows.size() > MaxItems) return false;
  QSet<QString> itemIds, libraryIds, covered;
  for (const auto &value : items) itemIds.insert(value.toObject().value("id").toString());
  const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]"));
  for (const auto &value : rows) {
    if (!value.isObject()) return false;
    const auto in = value.toObject();
    const auto id = in.value("id").toString();
    const auto name = in.value("name");
    const auto kind = in.value("kind").toString();
    if (id.isEmpty() || id.size() > 1024 || controls.match(id).hasMatch() ||
        libraryIds.contains(id) || !name.isString() || name.toString().size() > 4096 ||
        !QStringList{"movie", "series", "mixed"}.contains(kind) ||
        !in.value("itemIds").isArray()) return false;
    libraryIds.insert(id);
    QSet<QString> seen;
    QJsonArray members;
    const auto source = in.value("itemIds").toArray();
    if (source.size() > MaxItems) return false;
    for (const auto &member : source) {
      if (!member.isString() || !itemIds.contains(member.toString()) ||
          seen.contains(member.toString())) return false;
      seen.insert(member.toString()); covered.insert(member.toString()); members.append(member);
    }
    out.append(QJsonObject{{"id", id}, {"name", name}, {"kind", kind}, {"itemIds", members}});
  }
  return covered.size() == itemIds.size();
}
} // namespace
HomeServerStore::HomeServerStore(std::shared_ptr<AccountSecretBackend> secrets)
    : m_secrets(std::move(secrets)) {}
void HomeServerStore::deactivate() {
  m_active = false;
  m_profileId.clear();
  m_directory.clear();
  m_secretPrefix.clear();
  m_connections = {};
  m_error.clear();
}
bool HomeServerStore::available() const {
  return m_active && m_secrets && m_secrets->isAvailable();
}
QString HomeServerStore::error() const { return m_error; }
QString HomeServerStore::target(const QString &id) const {
  return m_secretPrefix + id;
}
QString HomeServerStore::indexPath(const QString &id) const {
  return m_directory + "/index-" + id + ".json";
}
bool HomeServerStore::activate(QString profileId, QString profileRoot) {
  deactivate();
  if (profileId.trimmed().isEmpty() || profileId.size() > 1024 || !m_secrets ||
      !m_secrets->isAvailable()) {
    m_error = "Secure profile storage is unavailable.";
    return false;
  }
  QFileInfo root(profileRoot);
  auto canonical = root.canonicalFilePath();
  if (canonical.isEmpty() || !root.isDir()) {
    m_error = "Profile storage is unavailable.";
    return false;
  }
  m_profileId = profileId;
  m_directory = canonical + "/home-servers";
  // Reject redirected profile-local storage rather than following an unexpected
  // link.
  if (QFileInfo(m_directory).isSymLink()) {
    deactivate();
    m_error = "Profile storage is invalid.";
    return false;
  }
  auto bytes = canonical.toUtf8();
  bytes.append('\0');
  bytes.append(profileId.toUtf8());
  m_secretPrefix =
      "Colosseum/HomeServers/v1/" +
      QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
      "/";
  const auto path = m_directory + "/connections-v1.json";
  if (QFileInfo::exists(path)) {
    QJsonObject envelope;
    QJsonArray rows;
    QSet<QString> ids;
    bool valid = !QFileInfo(path).isSymLink() && readJson(path, envelope) &&
                 envelope.value("version") == 1 &&
                 envelope.value("profileId") == profileId &&
                 envelope.value("connections").isArray();
    if (valid) {
      auto source = envelope.value("connections").toArray();
      valid = source.size() <= 10000;
      for (auto v : source) {
        auto obj = v.toObject();
        auto id = obj.value("id").toString();
        auto clean = metadata(obj, id, true);
        if (!v.isObject() || !uuid(id) || ids.contains(id) || clean.isEmpty()) {
          valid = false;
          break;
        }
        ids.insert(id);
        rows.append(clean);
      }
    }
    if (!valid) {
      deactivate();
      m_error = "Saved home-server connections are invalid.";
      return false;
    }
    m_connections = rows;
  }
  if (!QDir().mkpath(m_directory)) {
    deactivate();
    m_error = "Profile storage is unavailable.";
    return false;
  }
  m_active = true;
  return true;
}
QJsonArray HomeServerStore::connections() const {
  return available() ? m_connections : QJsonArray{};
}
QJsonObject HomeServerStore::connection(QString id) const {
  if (!available() || !uuid(id))
    return {};
  for (auto v : m_connections)
    if (v.toObject().value("id") == id)
      return v.toObject();
  return {};
}
QString HomeServerStore::token(QString id) const {
  if (connection(id).isEmpty())
    return {};
  auto blob = m_secrets->read(target(id));
  return blob ? QString::fromUtf8(*blob) : QString{};
}
bool HomeServerStore::saveConnections(const QJsonArray &rows) {
  if (!writeJson(m_directory + "/connections-v1.json",
                 {{"version", 1},
                  {"profileId", m_profileId},
                  {"connections", rows}})) {
    m_error = "Could not save home-server connections.";
    return false;
  }
  m_connections = rows;
  m_error.clear();
  return true;
}
QString HomeServerStore::add(QJsonObject input, QString secret) {
  if (!available()) {
    m_error = "Secure profile storage is unavailable.";
    return {};
  }
  auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto row = metadata(input, id);
  if (row.isEmpty() || secret.isEmpty() || secret.toUtf8().size() > 65536 ||
      m_connections.size() >= 10000) {
    m_error = "Home-server connection is invalid.";
    return {};
  }
  row.insert("createdAt",
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  row.remove("lastRefreshAt");
  auto key = target(id);
  auto blob = secret.toUtf8();
  if (!m_secrets->write(key, blob)) {
    m_error = "Could not securely save the connection.";
    return {};
  }
  if (m_secrets->read(key) != std::optional<QByteArray>(blob)) {
    bool rolledBack = m_secrets->remove(key);
    m_error = rolledBack ? "Could not verify secure connection storage."
                         : "Secure connection storage cleanup failed.";
    return {};
  }
  auto rows = m_connections;
  rows.append(row);
  if (!saveConnections(rows)) {
    if (!m_secrets->remove(key))
      m_error = "Connection save and secure storage cleanup failed.";
    return {};
  }
  return id;
}
bool HomeServerStore::setEnabled(QString id, bool enabled) {
  if (connection(id).isEmpty()) {
    m_error = "Unknown home-server connection.";
    return false;
  }
  auto rows = m_connections;
  for (qsizetype i = 0; i < rows.size(); ++i) {
    auto row = rows.at(i).toObject();
    if (row.value("id") == id) {
      row.insert("enabled", enabled);
      rows.replace(i, row);
      break;
    }
  }
  return saveConnections(rows);
}
bool HomeServerStore::remove(QString id) {
  if (connection(id).isEmpty()) {
    m_error = "Unknown home-server connection.";
    return false;
  }
  auto key = target(id);
  auto blob = m_secrets->read(key);
  if (!m_secrets->remove(key)) {
    m_error = "Could not remove secure connection storage.";
    return false;
  }
  QJsonArray rows;
  for (auto v : m_connections)
    if (v.toObject().value("id") != id)
      rows.append(v);
  if (!saveConnections(rows)) {
    if (blob && (!m_secrets->write(key, *blob) || m_secrets->read(key) != blob))
      m_error = "Connection removal rollback failed.";
    return false;
  }
  const auto path = indexPath(id);
  if (QFileInfo::exists(path) && !QFile::remove(path)) {
    m_error = "Connection removed; cached index cleanup failed.";
    return false;
  }
  return true;
}
QJsonObject HomeServerStore::catalog(QString id) const {
  if (connection(id).isEmpty())
    return {};
  const auto path = indexPath(id);
  if (!QFileInfo::exists(path))
    return {};
  QJsonObject envelope;
  QJsonArray clean;
  if (QFileInfo(path).isSymLink() || !readJson(path, envelope) ||
      envelope.value("version") != 1 ||
      envelope.value("profileId") != m_profileId ||
      envelope.value("connectionId") != id ||
      !envelope.value("items").isArray() ||
      !cleanItems(envelope.value("items").toArray(), clean)) {
    m_error = "Saved home-server index is invalid.";
    return {};
  }
  QJsonObject result{{"items", clean}, {"manifestKnown", envelope.contains("libraries")}};
  if (envelope.contains("libraries")) {
    QJsonArray libraries;
    if (!envelope.value("libraries").isArray() ||
        !cleanLibraries(envelope.value("libraries").toArray(), clean, libraries)) {
      m_error = "Saved home-server library manifest is invalid.";
      return {};
    }
    result.insert("libraries", libraries);
  }
  return result;
}
QJsonArray HomeServerStore::items(QString id) const {
  return catalog(id).value("items").toArray();
}
bool HomeServerStore::replaceItems(QString id, QJsonArray rows) {
  if (connection(id).isEmpty()) {
    m_error = "Unknown home-server connection.";
    return false;
  }
  QJsonArray clean;
  if (!cleanItems(rows, clean)) {
    m_error = "Home-server index is invalid.";
    return false;
  }
  if (!writeJson(indexPath(id), {{"version", 1},
                                 {"profileId", m_profileId},
                                 {"connectionId", id},
                                 {"items", clean}})) {
    m_error = "Could not save home-server index.";
    return false;
  }
  m_error.clear();
  return true;
}
bool HomeServerStore::replaceCatalog(QString id, QJsonArray libraries, QJsonArray rows) {
  if (connection(id).isEmpty()) {
    m_error = "Unknown home-server connection.";
    return false;
  }
  QJsonArray clean, manifest;
  if (!cleanItems(rows, clean) || !cleanLibraries(libraries, clean, manifest)) {
    m_error = "Home-server catalog is invalid.";
    return false;
  }
  if (!writeJson(indexPath(id), {{"version", 1}, {"profileId", m_profileId},
                               {"connectionId", id}, {"items", clean}, {"libraries", manifest}})) {
    m_error = "Could not save home-server catalog.";
    return false;
  }
  m_error.clear();
  return true;
}
