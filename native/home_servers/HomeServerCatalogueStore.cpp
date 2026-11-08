#include "HomeServerCatalogueStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <utility>

namespace Colosseum::HomeServers {
namespace {
constexpr qint64 kMaxIndexBytes = 64 * 1024 * 1024;
constexpr int kMaxItems = 150000;
constexpr int kMaxLibraries = 1024;

bool validRemoteId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128)
        return false;
    for (QChar c : id) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')
            && c != QLatin1Char('-'))
            return false;
    }
    return true;
}

bool validProfileId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128)
        return false;
    for (QChar c : id) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')
            && c != QLatin1Char('-') && c != QLatin1Char(':')
            && c != QLatin1Char('.'))
            return false;
    }
    return true;
}

QJsonObject encodeItem(const IndexedServerItem &row)
{
    const auto &i = row.item;
    return {
        {QStringLiteral("library"), row.libraryId},
        {QStringLiteral("id"), i.id},
        {QStringLiteral("name"), i.name},
        {QStringLiteral("type"), i.type},
        {QStringLiteral("imdb"), i.imdbId},
        {QStringLiteral("tmdb"), i.tmdbId},
        {QStringLiteral("tvdb"), i.tvdbId},
        {QStringLiteral("series"), i.seriesId},
        {QStringLiteral("parent"), i.parentId},
        {QStringLiteral("season"), i.seasonNumber},
        {QStringLiteral("episode"), i.episodeNumber},
        {QStringLiteral("year"), i.productionYear},
        {QStringLiteral("runtime"), i.runTimeTicks},
        {QStringLiteral("created"), i.dateCreated},
        {QStringLiteral("sources"), i.mediaSources},
        {QStringLiteral("userData"), i.userData}
    };
}

std::optional<IndexedServerItem> decodeItem(const QJsonValue &value,
                                            const ServerCatalogue &owner)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject j = value.toObject();
    IndexedServerItem row;
    row.libraryId = j.value(QStringLiteral("library")).toString();
    auto &i = row.item;
    i.id = j.value(QStringLiteral("id")).toString();
    i.name = j.value(QStringLiteral("name")).toString();
    i.type = j.value(QStringLiteral("type")).toString();
    i.imdbId = j.value(QStringLiteral("imdb")).toString();
    i.tmdbId = j.value(QStringLiteral("tmdb")).toString();
    i.tvdbId = j.value(QStringLiteral("tvdb")).toString();
    i.seriesId = j.value(QStringLiteral("series")).toString();
    i.parentId = j.value(QStringLiteral("parent")).toString();
    i.seasonNumber = j.value(QStringLiteral("season")).toInt(-1);
    i.episodeNumber = j.value(QStringLiteral("episode")).toInt(-1);
    i.productionYear = j.value(QStringLiteral("year")).toInt();
    i.runTimeTicks = j.value(QStringLiteral("runtime")).toInteger();
    i.dateCreated = j.value(QStringLiteral("created")).toString();
    i.mediaSources = j.value(QStringLiteral("sources")).toArray();
    i.userData = j.value(QStringLiteral("userData")).toObject();
    i.userId = owner.userId;
    i.serverId = owner.serverId;
    if (!validRemoteId(row.libraryId) || !validRemoteId(i.id)
        || (i.type != QLatin1String("Movie") && i.type != QLatin1String("Series")
            && i.type != QLatin1String("Season") && i.type != QLatin1String("Episode")))
        return std::nullopt;
    return row;
}

bool validSnapshot(const ServerCatalogue &catalogue)
{
    if (!validProfileId(catalogue.profileId)
        || !HomeServerCatalogueStore::validConnectionId(catalogue.connectionId)
        || !validRemoteId(catalogue.userId) || !validRemoteId(catalogue.serverId)
        || catalogue.syncedAtUtcMs <= 0
        || catalogue.libraries.size() > kMaxLibraries || catalogue.items.size() > kMaxItems)
        return false;
    QSet<QString> libraryIds;
    for (const auto &library : catalogue.libraries) {
        if (!validRemoteId(library.id) || libraryIds.contains(library.id))
            return false;
        libraryIds.insert(library.id);
    }
    QSet<QString> itemIds;
    for (const auto &row : catalogue.items) {
        if (!libraryIds.contains(row.libraryId)
            || !validRemoteId(row.item.id)
            || (row.item.type != QLatin1String("Movie")
                && row.item.type != QLatin1String("Series")
                && row.item.type != QLatin1String("Season")
                && row.item.type != QLatin1String("Episode"))
            || row.item.userId != catalogue.userId
            || row.item.serverId != catalogue.serverId)
            return false;
        const QString key = row.item.id;
        if (itemIds.contains(key))
            return false;
        itemIds.insert(key);
    }
    return true;
}
}

int ServerCatalogue::identifiedCount() const
{
    int count = 0;
    for (const auto &row : items)
        if (row.item.hasExternalId()) ++count;
    return count;
}

HomeServerCatalogueStore::HomeServerCatalogueStore(
    QString profileId, QString profileRoot)
    : m_profileId(std::move(profileId)),
      m_profileRoot(QDir::cleanPath(std::move(profileRoot)))
{
}

bool HomeServerCatalogueStore::validConnectionId(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

bool HomeServerCatalogueStore::available() const
{
    return validProfileId(m_profileId)
        && !m_profileRoot.isEmpty()
        && m_profileRoot != QLatin1String(".")
        && QDir::isAbsolutePath(m_profileRoot);
}

QString HomeServerCatalogueStore::filePath(const QString &connectionId) const
{
    return QDir(m_profileRoot).filePath(
        QStringLiteral("home-servers/index-v1/") + connectionId + QStringLiteral(".json"));
}

std::optional<ServerCatalogue> HomeServerCatalogueStore::load(
    const QString &connectionId, const QString &userId, const QString &serverId) const
{
    if (!available() || !validConnectionId(connectionId)
        || !validRemoteId(userId) || !validRemoteId(serverId))
        return std::nullopt;
    QFile file(filePath(connectionId));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > kMaxIndexBytes)
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1
        || root.value(QStringLiteral("profile")).toString() != m_profileId
        || root.value(QStringLiteral("connection")).toString() != connectionId
        || root.value(QStringLiteral("user")).toString() != userId
        || root.value(QStringLiteral("server")).toString() != serverId
        || !root.value(QStringLiteral("libraries")).isArray()
        || !root.value(QStringLiteral("items")).isArray())
        return std::nullopt;
    ServerCatalogue catalogue;
    catalogue.profileId = m_profileId;
    catalogue.connectionId = connectionId;
    catalogue.userId = userId;
    catalogue.serverId = serverId;
    catalogue.syncedAtUtcMs = root.value(QStringLiteral("synced_at")).toInteger(-1);
    const QJsonArray libraries = root.value(QStringLiteral("libraries")).toArray();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    if (libraries.size() > kMaxLibraries || items.size() > kMaxItems)
        return std::nullopt;
    for (const auto &v : libraries) {
        if (!v.isObject()) return std::nullopt;
        const QJsonObject j = v.toObject();
        catalogue.libraries.append({
            j.value(QStringLiteral("id")).toString(),
            j.value(QStringLiteral("name")).toString(),
            j.value(QStringLiteral("type")).toString()
        });
    }
    for (const auto &v : items) {
        auto parsed = decodeItem(v, catalogue);
        if (!parsed) return std::nullopt;
        catalogue.items.append(std::move(*parsed));
    }
    return validSnapshot(catalogue)
        ? std::optional<ServerCatalogue>(std::move(catalogue)) : std::nullopt;
}

bool HomeServerCatalogueStore::replace(const ServerCatalogue &catalogue)
{
    if (!available() || catalogue.profileId != m_profileId
        || !validSnapshot(catalogue))
        return false;
    QJsonArray libraries;
    for (const auto &v : catalogue.libraries) {
        libraries.append(QJsonObject{
            {QStringLiteral("id"), v.id}, {QStringLiteral("name"), v.name},
            {QStringLiteral("type"), v.collectionType}});
    }
    QJsonArray items;
    for (const auto &i : catalogue.items)
        items.append(encodeItem(i));
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profile"), catalogue.profileId},
        {QStringLiteral("connection"), catalogue.connectionId},
        {QStringLiteral("user"), catalogue.userId},
        {QStringLiteral("server"), catalogue.serverId},
        {QStringLiteral("synced_at"), catalogue.syncedAtUtcMs},
        {QStringLiteral("libraries"), libraries},
        {QStringLiteral("items"), items}
    };
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() <= 0 || data.size() > kMaxIndexBytes)
        return false;
    const QString path = filePath(catalogue.connectionId);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(data) != data.size())
        return false;
    return file.commit();
}

bool HomeServerCatalogueStore::remove(const QString &connectionId)
{
    if (!available() || !validConnectionId(connectionId))
        return false;
    const QString path = filePath(connectionId);
    return !QFile::exists(path) || QFile::remove(path);
}

} // namespace Colosseum::HomeServers
