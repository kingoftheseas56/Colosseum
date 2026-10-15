#include "MalListStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

constexpr int kSchemaVersion = 1;
constexpr auto kFileName = "mal-list-state.json";
constexpr int kMaximumItems = 50000;

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString kindKey(MalMediaKind kind)
{
    return kind == MalMediaKind::Anime
        ? QStringLiteral("anime") : QStringLiteral("manga");
}

std::optional<MalMediaKind> kindFromKey(const QString &key)
{
    if (key == QLatin1String("anime"))
        return MalMediaKind::Anime;
    if (key == QLatin1String("manga"))
        return MalMediaKind::Manga;
    return std::nullopt;
}

bool safeText(const QString &value, int maximum)
{
    if (value.size() > maximum || value.contains(QChar::Null))
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar c) {
        return c.category() == QChar::Other_Control
            && c != QLatin1Char('\t')
            && c != QLatin1Char('\n');
    });
}

bool validAccountId(const QString &value)
{
    bool ok = false;
    const qulonglong numeric = value.toULongLong(&ok);
    return ok && numeric > 0 && value == QString::number(numeric);
}

bool validItem(const MalListItem &item)
{
    bool idOk = false;
    const qulonglong id = item.malId.toULongLong(&idOk);
    return idOk && id > 0
        && item.malId == QString::number(id)
        && safeText(item.title, 500)
        && safeText(item.status, 32)
        && item.progress >= 0
        && item.totalUnits >= 0
        && item.updatedAtMs >= 0;
}

QJsonObject toJson(const MalListItem &item)
{
    return {
        {QStringLiteral("kind"), kindKey(item.kind)},
        {QStringLiteral("malId"), item.malId},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("status"), item.status},
        {QStringLiteral("progress"), item.progress},
        {QStringLiteral("totalUnits"), item.totalUnits},
        {QStringLiteral("updatedAtMs"),
         QString::number(item.updatedAtMs)}};
}

std::optional<MalListItem> fromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    const auto kind =
        kindFromKey(object.value(QStringLiteral("kind")).toString());
    bool updatedOk = false;
    const qint64 updated = object.value(
        QStringLiteral("updatedAtMs")).toString().toLongLong(&updatedOk);
    if (!kind || !updatedOk)
        return std::nullopt;
    MalListItem item;
    item.kind = *kind;
    item.malId = object.value(QStringLiteral("malId")).toString();
    item.title = object.value(QStringLiteral("title")).toString();
    item.status = object.value(QStringLiteral("status")).toString();
    item.progress = object.value(QStringLiteral("progress")).toInt(-1);
    item.totalUnits =
        object.value(QStringLiteral("totalUnits")).toInt(-1);
    item.updatedAtMs = updated;
    return validItem(item)
        ? std::optional<MalListItem>(item) : std::nullopt;
}

} // namespace

MalListStateStore::MalListStateStore(const ProfilePaths &profile)
    : m_path(storagePath(profile))
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral(
            "MyAnimeList list state is unavailable for this profile.");
        return;
    }
    load();
}

QString MalListStateStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal
        || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(
        profile.profileRoot() + QLatin1Char('/')
        + QLatin1String(kFileName));
}

bool MalListStateStore::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_error;
    return m_healthy;
}

std::optional<MalListItem> MalListStateStore::item(
    MalMediaKind kind, const QString &malId) const
{
    const auto found = std::find_if(
        m_items.cbegin(), m_items.cend(),
        [kind, &malId](const MalListItem &candidate) {
            return candidate.kind == kind
                && candidate.malId == malId;
        });
    return found == m_items.cend()
        ? std::nullopt
        : std::optional<MalListItem>(*found);
}

bool MalListStateStore::replaceAll(
    const QString &remoteAccountId,
    const QString &snapshotId,
    const QList<MalListItem> &items,
    QString *error)
{
    if (!m_healthy || !validAccountId(remoteAccountId)
        || snapshotId.trimmed().isEmpty()
        || snapshotId.size() > 128
        || items.size() > kMaximumItems) {
        return setError(error,
            QStringLiteral("Invalid MyAnimeList list-state snapshot."));
    }
    for (const MalListItem &item : items) {
        if (!validItem(item))
            return setError(error,
                QStringLiteral("Invalid MyAnimeList list-state item."));
    }
    if (!persist(remoteAccountId, snapshotId, items, error))
        return false;
    m_remoteAccountId = remoteAccountId;
    m_snapshotId = snapshotId;
    m_items = items;
    return true;
}

bool MalListStateStore::clear(QString *error)
{
    if (!m_healthy)
        return setError(error, m_error);
    if (QFile::exists(m_path) && !QFile::remove(m_path))
        return setError(error,
            QStringLiteral("MyAnimeList list state could not be removed."));
    m_remoteAccountId.clear();
    m_snapshotId.clear();
    m_items.clear();
    return true;
}

bool MalListStateStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral(
            "MyAnimeList list state could not be read.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError
        || root.value(QStringLiteral("version")).toInt()
            != kSchemaVersion) {
        m_healthy = false;
        m_error = QStringLiteral(
            "MyAnimeList list state is malformed.");
        return false;
    }
    const QString account =
        root.value(QStringLiteral("remoteAccountId")).toString();
    const QString snapshot =
        root.value(QStringLiteral("snapshotId")).toString();
    if (!validAccountId(account) || snapshot.isEmpty()
        || snapshot.size() > 128) {
        m_healthy = false;
        m_error = QStringLiteral(
            "MyAnimeList list state has invalid identity.");
        return false;
    }

    QList<MalListItem> loaded;
    const QJsonArray array =
        root.value(QStringLiteral("items")).toArray();
    if (array.size() > kMaximumItems) {
        m_healthy = false;
        m_error = QStringLiteral(
            "MyAnimeList list state is too large.");
        return false;
    }
    for (const QJsonValue &value : array) {
        const auto parsed = fromJson(value);
        if (!parsed) {
            m_healthy = false;
            m_error = QStringLiteral(
                "MyAnimeList list state contains an invalid item.");
            return false;
        }
        loaded.append(*parsed);
    }
    m_remoteAccountId = account;
    m_snapshotId = snapshot;
    m_items = loaded;
    return true;
}

bool MalListStateStore::persist(
    const QString &remoteAccountId,
    const QString &snapshotId,
    const QList<MalListItem> &items,
    QString *error) const
{
    QJsonArray array;
    for (const MalListItem &item : items)
        array.append(toJson(item));
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), kSchemaVersion},
        {QStringLiteral("remoteAccountId"), remoteAccountId},
        {QStringLiteral("snapshotId"), snapshotId},
        {QStringLiteral("items"), array}});

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(document.toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        return setError(error,
            QStringLiteral("MyAnimeList list state could not be persisted."));
    }
    return true;
}
