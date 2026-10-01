#include "MalProtocol.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

namespace {

bool isHttps(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https")
        && !url.host().isEmpty() && url.userName().isEmpty()
        && url.password().isEmpty() && url.fragment().isEmpty();
}

bool isLoopbackRedirect(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("http")
        || url.port() <= 0 || url.port() > 65535
        || url.path().isEmpty() || !url.query().isEmpty()
        || !url.fragment().isEmpty() || !url.userName().isEmpty()
        || !url.password().isEmpty()) {
        return false;
    }
    const QString host = url.host().toLower();
    return host == QLatin1String("127.0.0.1")
        || host == QLatin1String("localhost")
        || host == QLatin1String("::1");
}

bool positiveNumericId(const QString &value)
{
    bool ok = false;
    const qulonglong number = value.toULongLong(&ok);
    return ok && number > 0 && value == QString::number(number);
}

QString mediaPrefix(MalMediaKind kind)
{
    return kind == MalMediaKind::Anime ? QStringLiteral("anime")
                                        : QStringLiteral("manga");
}

QString responseProgressField(MalMediaKind kind)
{
    return kind == MalMediaKind::Anime
        ? QStringLiteral("num_episodes_watched")
        : QStringLiteral("num_chapters_read");
}

QString mutationProgressField(MalMediaKind kind)
{
    return kind == MalMediaKind::Anime
        ? QStringLiteral("num_watched_episodes")
        : QStringLiteral("num_chapters_read");
}

QString activeStatus(MalMediaKind kind)
{
    return kind == MalMediaKind::Anime ? QStringLiteral("watching")
                                        : QStringLiteral("reading");
}

bool validListStatus(const QString &status, MalMediaKind kind)
{
    if (kind == MalMediaKind::Anime) {
        return status == QLatin1String("watching")
            || status == QLatin1String("completed")
            || status == QLatin1String("on_hold")
            || status == QLatin1String("dropped")
            || status == QLatin1String("plan_to_watch");
    }
    return status == QLatin1String("reading")
        || status == QLatin1String("completed")
        || status == QLatin1String("on_hold")
        || status == QLatin1String("dropped")
        || status == QLatin1String("plan_to_read");
}

std::optional<MalRemoteIdentity> canonicalAnimeIdentity(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("^mal:([1-9][0-9]*):([1-9][0-9]*)$"));
    const QRegularExpressionMatch match = pattern.match(id);
    if (!match.hasMatch())
        return std::nullopt;
    return MalRemoteIdentity{MalMediaKind::Anime,
                             match.captured(1),
                             match.captured(2).toInt(),
                             false};
}

std::optional<MalRemoteIdentity> canonicalMangaIdentity(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("^mal:([1-9][0-9]*):(chapter|ch):([1-9][0-9]*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(id);
    if (!match.hasMatch())
        return std::nullopt;
    return MalRemoteIdentity{MalMediaKind::Manga,
                             match.captured(1),
                             match.captured(3).toInt(),
                             false};
}

} // namespace

QString MalRemoteIdentity::remoteMediaId() const
{
    const QString prefix = mediaPrefix(kind);
    if (listAggregate)
        return prefix + QLatin1Char(':') + malId + QStringLiteral(":list");
    const QString unit = kind == MalMediaKind::Anime
        ? QStringLiteral(":episode:") : QStringLiteral(":chapter:");
    return prefix + QLatin1Char(':') + malId + unit
        + QString::number(unitNumber);
}

bool malConfigurationIsValid(const MalAuthConfiguration &configuration)
{
    return !configuration.clientId.trimmed().isEmpty()
        && configuration.clientId == configuration.clientId.trimmed()
        && configuration.clientId.size() <= 256
        && isHttps(configuration.authorizationEndpoint)
        && isHttps(configuration.tokenEndpoint)
        && isHttps(configuration.apiEndpoint)
        && isLoopbackRedirect(configuration.redirectUri)
        && (configuration.tokenBrokerEndpoint.isEmpty()
            || isHttps(configuration.tokenBrokerEndpoint))
        && !configuration.appName.trimmed().isEmpty()
        && !configuration.appVersion.trimmed().isEmpty();
}

std::optional<MalAuthConfiguration> malProductionConfiguration()
{
#if !defined(COLOSSEUM_MAL_CLIENT_ID) || !defined(COLOSSEUM_MAL_REDIRECT_URI)
    return std::nullopt;
#else
    MalAuthConfiguration configuration;
    configuration.clientId = QString::fromUtf8(COLOSSEUM_MAL_CLIENT_ID).trimmed();
    configuration.authorizationEndpoint =
        QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/authorize"));
    configuration.tokenEndpoint =
        QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/token"));
    configuration.apiEndpoint =
        QUrl(QStringLiteral("https://api.myanimelist.net/v2"));
    configuration.redirectUri =
        QUrl(QString::fromUtf8(COLOSSEUM_MAL_REDIRECT_URI).trimmed());
#ifdef COLOSSEUM_MAL_TOKEN_BROKER_URL
    configuration.tokenBrokerEndpoint =
        QUrl(QString::fromUtf8(COLOSSEUM_MAL_TOKEN_BROKER_URL).trimmed());
#endif
    configuration.appName = QStringLiteral("Colosseum");
#ifdef COLOSSEUM_VERSION
    configuration.appVersion = QString::fromUtf8(COLOSSEUM_VERSION);
#else
    configuration.appVersion = QStringLiteral("dev");
#endif
    return malConfigurationIsValid(configuration)
        ? std::optional<MalAuthConfiguration>(configuration)
        : std::nullopt;
#endif
}

QByteArray malPkceVerifierFromEntropy(const QByteArray &entropy)
{
    const QByteArray verifier = entropy.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return verifier.size() >= 43 && verifier.size() <= 128
        ? verifier : QByteArray();
}

QByteArray malStateFromEntropy(const QByteArray &entropy)
{
    const QByteArray state = entropy.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return state.size() >= 32 && state.size() <= 128 ? state : QByteArray();
}

bool malConstantTimeEqual(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;
    uchar difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index) {
        difference |= static_cast<uchar>(left.at(index))
            ^ static_cast<uchar>(right.at(index));
    }
    return difference == 0;
}

QUrl malAuthorizationUrl(const MalAuthConfiguration &configuration,
                         const QByteArray &state,
                         const QByteArray &codeVerifier)
{
    if (!malConfigurationIsValid(configuration)
        || state.isEmpty() || codeVerifier.size() < 43
        || codeVerifier.size() > 128) {
        return {};
    }
    QUrl url = configuration.authorizationEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), configuration.clientId);
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(state));
    query.addQueryItem(QStringLiteral("redirect_uri"),
                       configuration.redirectUri.toString(QUrl::FullyEncoded));
    query.addQueryItem(QStringLiteral("code_challenge"),
                       QString::fromLatin1(codeVerifier));
    // MAL's current official authorization documentation supports plain only.
    query.addQueryItem(QStringLiteral("code_challenge_method"),
                       QStringLiteral("plain"));
    url.setQuery(query);
    return url;
}

std::optional<MalRemoteIdentity> malRemoteIdentity(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() == 3 && parts.at(2) == QLatin1String("list")
        && (parts.at(0) == QLatin1String("anime")
            || parts.at(0) == QLatin1String("manga"))
        && positiveNumericId(parts.at(1))) {
        return MalRemoteIdentity{
            parts.at(0) == QLatin1String("anime")
                ? MalMediaKind::Anime : MalMediaKind::Manga,
            parts.at(1), 0, true};
    }
    if (parts.size() != 4
        || !positiveNumericId(parts.at(1))
        || !positiveNumericId(parts.at(3))) {
        return std::nullopt;
    }
    if (parts.at(0) == QLatin1String("anime")
        && parts.at(2) == QLatin1String("episode")) {
        return MalRemoteIdentity{MalMediaKind::Anime, parts.at(1),
                                 parts.at(3).toInt(), false};
    }
    if (parts.at(0) == QLatin1String("manga")
        && parts.at(2) == QLatin1String("chapter")) {
        return MalRemoteIdentity{MalMediaKind::Manga, parts.at(1),
                                 parts.at(3).toInt(), false};
    }
    return std::nullopt;
}

std::optional<MalRemoteIdentity> malIdentityForCanonicalFact(
    const TrackerDeliveryFact &fact)
{
    if (fact.mediaDomain == TrackerMediaDomain::Anime)
        return canonicalAnimeIdentity(fact.historyId);
    if (fact.mediaDomain == TrackerMediaDomain::Manga)
        return canonicalMangaIdentity(fact.historyId);
    return std::nullopt;
}

QString malRemoteMediaIdForCanonicalFact(const TrackerDeliveryFact &fact)
{
    const auto identity = malIdentityForCanonicalFact(fact);
    return identity ? identity->remoteMediaId() : QString();
}

std::optional<MalMutation> malMutationForDelivery(
    const MalRemoteIdentity &identity,
    const TrackerDeliveryFact &fact,
    int totalUnits)
{
    if (identity.listAggregate || !positiveNumericId(identity.malId)
        || identity.unitNumber <= 0
        || (fact.kind == TrackerDeliveryFactKind::Progress
            && fact.progress < 100)) {
        // MAL list progress is a count of completed episodes/chapters, never
        // Colosseum's in-item fractional Continue position.
        return std::nullopt;
    }

    MalMutation mutation;
    mutation.path = QLatin1Char('/') + mediaPrefix(identity.kind)
        + QLatin1Char('/') + identity.malId
        + QStringLiteral("/my_list_status");
    mutation.intendedProgress = identity.unitNumber;
    mutation.intendedCompleted =
        totalUnits > 0 && identity.unitNumber >= totalUnits;
    mutation.form.addQueryItem(mutationProgressField(identity.kind),
                               QString::number(identity.unitNumber));
    mutation.form.addQueryItem(
        QStringLiteral("status"),
        mutation.intendedCompleted ? QStringLiteral("completed")
                                   : activeStatus(identity.kind));
    return mutation;
}

std::optional<MalListItem> malListItemFromJson(
    const QJsonObject &row,
    MalMediaKind kind)
{
    const QJsonObject node = row.value(QStringLiteral("node")).toObject();
    const QJsonObject status = row.value(QStringLiteral("list_status")).toObject();
    const QJsonValue rawId = node.value(QStringLiteral("id"));
    QString id;
    if (rawId.isDouble())
        id = QString::number(static_cast<qint64>(rawId.toDouble()));
    else
        id = rawId.toString().trimmed();
    const QString state = status.value(QStringLiteral("status")).toString();
    if (!positiveNumericId(id) || !validListStatus(state, kind))
        return std::nullopt;

    const QString progressName = responseProgressField(kind);
    const int progress = status.value(progressName).toInt(-1);
    if (progress < 0)
        return std::nullopt;

    const QString totalName = kind == MalMediaKind::Anime
        ? QStringLiteral("num_episodes") : QStringLiteral("num_chapters");
    const int total = qMax(0, node.value(totalName).toInt());
    const qint64 updated = QDateTime::fromString(
        status.value(QStringLiteral("updated_at")).toString(),
        Qt::ISODateWithMs).toMSecsSinceEpoch();

    MalListItem item;
    item.kind = kind;
    item.malId = id;
    item.title = node.value(QStringLiteral("title")).toString().simplified().left(500);
    item.status = state;
    item.progress = progress;
    item.totalUnits = total;
    item.updatedAtMs = qMax<qint64>(0, updated);
    return item;
}

QString malListKey(MalMediaKind kind, const QString &malId)
{
    return mediaPrefix(kind) + QLatin1Char(':') + malId;
}

QString malSafeStatusLabel(const QString &status, MalMediaKind kind)
{
    if (!validListStatus(status, kind))
        return QStringLiteral("Unknown");
    QString result = status;
    result.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!result.isEmpty())
        result[0] = result.at(0).toUpper();
    return result;
}
