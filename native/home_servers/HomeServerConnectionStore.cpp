#include "HomeServerConnectionStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <utility>

namespace Colosseum::HomeServers {
namespace {
constexpr auto kPrefix = "Brotherhood.Colosseum.HomeServer.v1.";
constexpr qsizetype kMaxWindowsCredentialBytes = 2560;

QString hashId(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool validRemoteId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}

QUrl normalizedUrl(ServerKind kind, const QUrl &url)
{
    JellyfinEmbyClient validator(kind, url, QStringLiteral("colosseum-home-server"));
    return validator.isValid() ? validator.apiBaseUrl() : QUrl();
}
}

HomeServerConnectionStore::HomeServerConnectionStore(
    QString profileId, std::shared_ptr<AccountSecretBackend> backend)
    : m_profileId(std::move(profileId)), m_backend(std::move(backend))
{
    if (!validProfileId(m_profileId))
        return;
    m_prefix = QString::fromLatin1(kPrefix);
    const QString tag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG").trimmed();
    if (!tag.isEmpty())
        m_prefix += QStringLiteral("Tagged.") + hashId(tag) + QLatin1Char('.');
    m_prefix += hashId(m_profileId) + QLatin1Char('.');
}

bool HomeServerConnectionStore::validProfileId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_.:-]{1,128}$"));
    return pattern.match(id).hasMatch();
}

bool HomeServerConnectionStore::validConnectionId(const QString &id)
{
    const QUuid parsed(id);
    return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == id;
}

bool HomeServerConnectionStore::validSession(const ServerSession &session)
{
    if (!validRemoteId(session.userId) || !validRemoteId(session.serverId)
        || session.accessToken.isEmpty() || session.accessToken.size() > 4096)
        return false;
    return std::all_of(session.accessToken.cbegin(), session.accessToken.cend(),
                       [](unsigned char c) { return c >= 0x21 && c <= 0x7e; });
}

bool HomeServerConnectionStore::validName(const QString &name)
{
    if (name.isEmpty() || name.size() > 96 || name != name.trimmed())
        return false;
    return std::all_of(name.cbegin(), name.cend(),
                       [](QChar c) { return c.isPrint(); });
}

bool HomeServerConnectionStore::available() const
{
    return !m_prefix.isEmpty() && m_backend && m_backend->isAvailable();
}

QString HomeServerConnectionStore::target(const QString &id) const
{
    return m_prefix + id;
}

std::optional<HomeServerConnectionStore::Stored>
HomeServerConnectionStore::load(const QString &connectionId) const
{
    if (!available() || !validConnectionId(connectionId))
        return std::nullopt;
    const auto blob = m_backend->read(target(connectionId));
    if (!blob || blob->isEmpty() || blob->size() > kMaxWindowsCredentialBytes)
        return std::nullopt;
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(*blob, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject json = doc.object();
    if (json.value(QStringLiteral("version")).toInt() != 1
        || json.value(QStringLiteral("profile")).toString() != m_profileId
        || json.value(QStringLiteral("connection")).toString() != connectionId
        || !json.value(QStringLiteral("enabled")).isBool()
        || !json.value(QStringLiteral("added_at")).isDouble())
        return std::nullopt;
    const int kindId = json.value(QStringLiteral("kind")).toInt(-1);
    if (kindId != 0 && kindId != 1)
        return std::nullopt;
    const auto kind = kindId == 0 ? ServerKind::Jellyfin : ServerKind::Emby;
    const QUrl base(json.value(QStringLiteral("base")).toString());
    const QString name = json.value(QStringLiteral("name")).toString();
    const auto decoded = QByteArray::fromBase64Encoding(
        json.value(QStringLiteral("token")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || !validName(name))
        return std::nullopt;
    const ServerSession session{
        json.value(QStringLiteral("user")).toString(),
        json.value(QStringLiteral("server")).toString(),
        decoded.decoded};
    const QUrl normalized = normalizedUrl(kind, base);
    if (!validSession(session) || !normalized.isValid() || normalized != base)
        return std::nullopt;
    const qint64 added = json.value(QStringLiteral("added_at")).toInteger(-1);
    if (added < 0)
        return std::nullopt;
    Stored result;
    result.connection = {connectionId, name, kind, normalized,
                         session.userId, session.serverId,
                         json.value(QStringLiteral("enabled")).toBool(), added};
    result.session = session;
    return result;
}

bool HomeServerConnectionStore::persist(const Stored &value)
{
    if (!available() || !validConnectionId(value.connection.id)
        || !validName(value.connection.displayName)
        || !validSession(value.session)
        || value.connection.userId != value.session.userId
        || value.connection.serverId != value.session.serverId)
        return false;
    const QUrl normalized = normalizedUrl(value.connection.kind, value.connection.apiBase);
    if (!normalized.isValid() || normalized != value.connection.apiBase)
        return false;
    const QJsonObject json{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profile"), m_profileId},
        {QStringLiteral("connection"), value.connection.id},
        {QStringLiteral("kind"), value.connection.kind == ServerKind::Jellyfin ? 0 : 1},
        {QStringLiteral("base"), normalized.toString()},
        {QStringLiteral("name"), value.connection.displayName},
        {QStringLiteral("user"), value.session.userId},
        {QStringLiteral("server"), value.session.serverId},
        {QStringLiteral("token"), QString::fromLatin1(value.session.accessToken.toBase64())},
        {QStringLiteral("enabled"), value.connection.enabled},
        {QStringLiteral("added_at"), value.connection.addedAtUtcMs}};
    const QByteArray blob = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (blob.size() > kMaxWindowsCredentialBytes)
        return false;
    const QString key = target(value.connection.id);
    const auto previous = m_backend->read(key);
    if (m_backend->write(key, blob) && m_backend->read(key) == blob)
        return true;
    // A failed/partial backend write must not be reported as a saved connection.
    if (previous)
        m_backend->write(key, *previous);
    else
        m_backend->remove(key);
    return false;
}

QList<HomeServerConnection> HomeServerConnectionStore::list() const
{
    QList<HomeServerConnection> results;
    if (!available())
        return results;
    for (const QString &key : m_backend->targets(m_prefix)) {
        if (!key.startsWith(m_prefix))
            continue;
        const QString id = key.mid(m_prefix.size());
        if (const auto entry = load(id))
            results.append(entry->connection);
    }
    std::sort(results.begin(), results.end(), [](const auto &a, const auto &b) {
        return a.addedAtUtcMs == b.addedAtUtcMs
            ? a.id < b.id : a.addedAtUtcMs < b.addedAtUtcMs;
    });
    return results;
}

std::optional<QString> HomeServerConnectionStore::add(
    ServerKind kind, const QString &displayName, const QUrl &apiBase,
    const ServerSession &session)
{
    const QUrl normalized = normalizedUrl(kind, apiBase);
    const QString name = displayName.trimmed();
    if (!available() || !validName(name) || !normalized.isValid()
        || !validSession(session))
        return std::nullopt;
    for (const auto &current : list()) {
        if (current.kind == kind && current.apiBase == normalized
            && current.userId == session.userId && current.serverId == session.serverId)
            return std::nullopt;
    }
    Stored entry;
    entry.connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.connection.displayName = name;
    entry.connection.kind = kind;
    entry.connection.apiBase = normalized;
    entry.connection.userId = session.userId;
    entry.connection.serverId = session.serverId;
    entry.connection.addedAtUtcMs = QDateTime::currentMSecsSinceEpoch();
    entry.session = session;
    return persist(entry) ? std::optional<QString>(entry.connection.id) : std::nullopt;
}

std::optional<ServerSession>
HomeServerConnectionStore::activeSession(const QString &connectionId) const
{
    const auto record = load(connectionId);
    return record && record->connection.enabled
        ? std::optional<ServerSession>(record->session) : std::nullopt;
}

bool HomeServerConnectionStore::replaceSession(
    const QString &connectionId, const ServerSession &session)
{
    auto record = load(connectionId);
    if (!record || !validSession(session)
        || record->session.userId != session.userId
        || record->session.serverId != session.serverId)
        return false;
    record->session = session;
    return persist(*record);
}

bool HomeServerConnectionStore::setEnabled(const QString &connectionId, bool enabled)
{
    auto record = load(connectionId);
    if (!record)
        return false;
    if (record->connection.enabled == enabled)
        return true;
    record->connection.enabled = enabled;
    return persist(*record);
}

bool HomeServerConnectionStore::remove(const QString &connectionId)
{
    if (!available() || !validConnectionId(connectionId))
        return false;
    const QString key = target(connectionId);
    return m_backend->remove(key) && !m_backend->read(key).has_value();
}

QList<QUrl> HomeServerConnectionStore::candidateApiBases(
    ServerKind kind, const QUrl &configured)
{
    QList<QUrl> candidates;
    const QUrl first = normalizedUrl(kind, configured);
    if (!first.isValid())
        return candidates;
    candidates.append(first);
    if (kind == ServerKind::Emby
        && !first.path().endsWith(QLatin1String("/emby"), Qt::CaseInsensitive)) {
        QUrl alternative = first;
        alternative.setPath(first.path() + QStringLiteral("/emby"));
        const QUrl second = normalizedUrl(kind, alternative);
        if (second.isValid() && second != first)
            candidates.append(second);
    }
    return candidates;
}

} // namespace Colosseum::HomeServers
