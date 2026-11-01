#include "AniListProtocol.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr int kMaximumGraphQlInt = std::numeric_limits<int>::max();
constexpr int kMaximumPerChunk = 100;
constexpr int kMaximumChunk = 10000;
constexpr qsizetype kMaximumLists = 64;
constexpr qsizetype kMaximumEntriesPerPage = 500;
constexpr qsizetype kMaximumTitleLength = 500;

bool isHttpsHost(const QUrl &url, const QString &host)
{
    return url.isValid() && url.scheme() == QLatin1String("https")
        && url.host().toLower() == host && url.port() == -1
        && url.userName().isEmpty() && url.password().isEmpty()
        && url.query().isEmpty() && url.fragment().isEmpty();
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

// Positive decimal id that also fits a GraphQL Int.
std::optional<int> positiveId(const QString &value)
{
    bool ok = false;
    const qulonglong number = value.toULongLong(&ok);
    if (!ok || number == 0
        || number > static_cast<qulonglong>(kMaximumGraphQlInt)
        || value != QString::number(number)) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

QString mediaPrefix(AniListMediaKind kind)
{
    return kind == AniListMediaKind::Anime ? QStringLiteral("anime")
                                            : QStringLiteral("manga");
}

// JSON number holding an integer; anything else is a type failure.
bool integerValue(const QJsonValue &value, qint64 *out)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number != std::floor(number)
        || std::fabs(number) > 9.0e15) {
        return false;
    }
    *out = static_cast<qint64>(number);
    return true;
}

std::optional<QString> idString(const QJsonValue &value)
{
    qint64 number = 0;
    if (!integerValue(value, &number) || number <= 0
        || number > kMaximumGraphQlInt) {
        return std::nullopt;
    }
    return QString::number(number);
}

// Missing or null is zero (an unknown total); a wrong type or negative value
// is a failure.
std::optional<int> optionalCount(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
        return 0;
    qint64 number = 0;
    if (!integerValue(value, &number) || number < 0
        || number > kMaximumGraphQlInt) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

std::optional<int> requiredProgress(const QJsonValue &value)
{
    qint64 number = 0;
    if (!integerValue(value, &number) || number < 0
        || number > kMaximumGraphQlInt) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

// AniList `updatedAt` is Unix seconds. Missing or null is zero.
std::optional<qint64> updatedAtMs(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
        return qint64(0);
    qint64 seconds = 0;
    if (!integerValue(value, &seconds) || seconds < 0
        || seconds > 4102444800LL) {
        return std::nullopt;
    }
    return seconds * 1000;
}

QString titleFrom(const QJsonObject &title)
{
    QString text =
        title.value(QStringLiteral("romaji")).toString().simplified();
    if (text.isEmpty())
        text = title.value(QStringLiteral("english")).toString().simplified();
    return text.left(kMaximumTitleLength);
}

// Returns the `data` object only when the response is a clean GraphQL success.
std::optional<QJsonObject> cleanData(const QJsonDocument &document)
{
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    const QJsonValue errors = root.value(QStringLiteral("errors"));
    if (!errors.isUndefined() && !errors.isNull()) {
        if (!errors.isArray() || !errors.toArray().isEmpty())
            return std::nullopt;
    }
    const QJsonValue data = root.value(QStringLiteral("data"));
    if (!data.isObject())
        return std::nullopt;
    return data.toObject();
}

struct ParsedMedia {
    QString aniListId;
    QString malId;
    QString title;
    int totalUnits = 0;
};

std::optional<ParsedMedia> parseMedia(const QJsonValue &value,
                                      AniListMediaKind kind)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject media = value.toObject();
    const auto id = idString(media.value(QStringLiteral("id")));
    if (!id)
        return std::nullopt;

    ParsedMedia parsed;
    parsed.aniListId = *id;
    const QJsonValue idMal = media.value(QStringLiteral("idMal"));
    if (!idMal.isNull() && !idMal.isUndefined()) {
        const auto mal = idString(idMal);
        if (!mal)
            return std::nullopt;
        parsed.malId = *mal;
    }
    const QJsonValue title = media.value(QStringLiteral("title"));
    if (!title.isObject())
        return std::nullopt;
    parsed.title = titleFrom(title.toObject());

    // Anime counts episodes, manga counts chapters. Volumes are never read.
    const auto total = optionalCount(media.value(
        kind == AniListMediaKind::Anime ? QStringLiteral("episodes")
                                        : QStringLiteral("chapters")));
    if (!total)
        return std::nullopt;
    parsed.totalUnits = *total;
    return parsed;
}

std::optional<AniListEntryState> parseEntryState(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    const auto mediaId = idString(object.value(QStringLiteral("mediaId")));
    const QString status = object.value(QStringLiteral("status")).toString();
    const auto progress =
        requiredProgress(object.value(QStringLiteral("progress")));
    const auto updated =
        updatedAtMs(object.value(QStringLiteral("updatedAt")));
    if (!mediaId || !aniListStatusIsValid(status) || !progress || !updated)
        return std::nullopt;
    AniListEntryState state;
    state.aniListId = *mediaId;
    state.status = status;
    state.progress = *progress;
    state.updatedAtMs = *updated;
    return state;
}

QJsonObject request(const QString &query, const QJsonObject &variables)
{
    return QJsonObject{{QStringLiteral("query"), query},
                       {QStringLiteral("variables"), variables}};
}

bool errorsMatch(const QJsonDocument &document,
                 const std::function<bool(const QJsonObject &)> &predicate)
{
    if (!document.isObject())
        return false;
    const QJsonValue errors = document.object().value(QStringLiteral("errors"));
    if (!errors.isArray())
        return false;
    const QJsonArray array = errors.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isObject() && predicate(entry.toObject()))
            return true;
    }
    return false;
}

} // namespace

QString AniListRemoteIdentity::remoteMediaId() const
{
    const QString prefix = QStringLiteral("anilist:") + mediaPrefix(kind);
    if (listAggregate)
        return prefix + QLatin1Char(':') + aniListId + QStringLiteral(":list");
    const QString unit = kind == AniListMediaKind::Anime
        ? QStringLiteral(":episode:") : QStringLiteral(":chapter:");
    return prefix + QLatin1Char(':') + aniListId + unit
        + QString::number(unitNumber);
}

bool aniListConfigurationIsValid(const AniListAuthConfiguration &configuration)
{
    return !configuration.clientId.trimmed().isEmpty()
        && configuration.clientId == configuration.clientId.trimmed()
        && configuration.clientId.size() <= 256
        && isHttpsHost(configuration.authorizationEndpoint,
                       QStringLiteral("anilist.co"))
        && isHttpsHost(configuration.graphqlEndpoint,
                       QStringLiteral("graphql.anilist.co"))
        && isLoopbackRedirect(configuration.redirectUri)
        && !configuration.appName.trimmed().isEmpty()
        && !configuration.appVersion.trimmed().isEmpty();
}

std::optional<AniListAuthConfiguration> aniListProductionConfiguration()
{
#if !defined(COLOSSEUM_ANILIST_CLIENT_ID) || !defined(COLOSSEUM_ANILIST_REDIRECT_URI)
    return std::nullopt;
#else
    AniListAuthConfiguration configuration;
    configuration.clientId =
        QString::fromUtf8(COLOSSEUM_ANILIST_CLIENT_ID).trimmed();
    configuration.authorizationEndpoint =
        QUrl(QStringLiteral("https://anilist.co/api/v2/oauth/authorize"));
    configuration.graphqlEndpoint =
        QUrl(QStringLiteral("https://graphql.anilist.co"));
    configuration.redirectUri =
        QUrl(QString::fromUtf8(COLOSSEUM_ANILIST_REDIRECT_URI).trimmed());
    configuration.appName = QStringLiteral("Colosseum");
#ifdef COLOSSEUM_VERSION
    configuration.appVersion = QString::fromUtf8(COLOSSEUM_VERSION);
#else
    configuration.appVersion = QStringLiteral("dev");
#endif
    return aniListConfigurationIsValid(configuration)
        ? std::optional<AniListAuthConfiguration>(configuration)
        : std::nullopt;
#endif
}

std::optional<AniListRemoteIdentity> aniListRemoteIdentity(
    const QString &remoteMediaId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^anilist:(anime|manga):([1-9][0-9]*):"
                       "(episode|chapter|list)(?::([1-9][0-9]*))?$"));
    const QRegularExpressionMatch match = pattern.match(remoteMediaId);
    if (!match.hasMatch())
        return std::nullopt;
    const bool anime = match.captured(1) == QLatin1String("anime");
    const QString unit = match.captured(3);
    AniListRemoteIdentity identity;
    identity.kind = anime ? AniListMediaKind::Anime : AniListMediaKind::Manga;
    identity.aniListId = match.captured(2);
    if (!positiveId(identity.aniListId))
        return std::nullopt;
    if (unit == QLatin1String("list")) {
        if (!match.captured(4).isEmpty())
            return std::nullopt;
        identity.listAggregate = true;
        return identity;
    }
    // Anime counts episodes and manga counts chapters; no cross-kind unit.
    if ((anime && unit != QLatin1String("episode"))
        || (!anime && unit != QLatin1String("chapter"))
        || match.captured(4).isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const int number = match.captured(4).toInt(&ok);
    if (!ok)
        return std::nullopt;
    identity.unitNumber = number;
    return identity;
}

QString aniListMediaTypeName(AniListMediaKind kind)
{
    return kind == AniListMediaKind::Anime ? QStringLiteral("ANIME")
                                            : QStringLiteral("MANGA");
}

bool aniListStatusIsValid(const QString &status)
{
    return status == QLatin1String("CURRENT")
        || status == QLatin1String("PLANNING")
        || status == QLatin1String("COMPLETED")
        || status == QLatin1String("DROPPED")
        || status == QLatin1String("PAUSED")
        || status == QLatin1String("REPEATING");
}

QString aniListSafeStatusLabel(const QString &status)
{
    if (!aniListStatusIsValid(status))
        return QStringLiteral("Unknown");
    return status.at(0) + status.mid(1).toLower();
}

QString aniListListKey(AniListMediaKind kind, const QString &aniListId)
{
    return mediaPrefix(kind) + QLatin1Char(':') + aniListId;
}

QJsonObject aniListViewerRequest()
{
    return request(QStringLiteral("query { Viewer { id name } }"), {});
}

std::optional<QJsonObject> aniListMediaListCollectionRequest(
    const QString &userId,
    AniListMediaKind kind,
    int chunk,
    int perChunk)
{
    const auto id = positiveId(userId);
    if (!id || chunk < 1 || chunk > kMaximumChunk
        || perChunk < 1 || perChunk > kMaximumPerChunk) {
        return std::nullopt;
    }
    return request(
        QStringLiteral(
            "query ($userId: Int!, $type: MediaType!, $chunk: Int!, "
            "$perChunk: Int!) { MediaListCollection(userId: $userId, "
            "type: $type, chunk: $chunk, perChunk: $perChunk) { "
            "hasNextChunk lists { entries { status progress updatedAt "
            "media { id idMal title { romaji english } episodes chapters } "
            "} } } }"),
        QJsonObject{{QStringLiteral("userId"), *id},
                    {QStringLiteral("type"), aniListMediaTypeName(kind)},
                    {QStringLiteral("chunk"), chunk},
                    {QStringLiteral("perChunk"), perChunk}});
}

std::optional<QJsonObject> aniListMediaByMalIdRequest(
    const QString &malId,
    AniListMediaKind kind)
{
    const auto id = positiveId(malId);
    if (!id)
        return std::nullopt;
    return request(
        QStringLiteral(
            "query ($idMal: Int!, $type: MediaType!) { Media(idMal: $idMal, "
            "type: $type) { id idMal title { romaji english } episodes "
            "chapters } }"),
        QJsonObject{{QStringLiteral("idMal"), *id},
                    {QStringLiteral("type"), aniListMediaTypeName(kind)}});
}

std::optional<QJsonObject> aniListSaveEntryRequest(
    const QString &mediaId,
    int progress,
    const QString &status)
{
    const auto id = positiveId(mediaId);
    if (!id || progress < 0
        || (!status.isEmpty() && !aniListStatusIsValid(status))) {
        return std::nullopt;
    }
    QJsonObject variables{{QStringLiteral("mediaId"), *id},
                          {QStringLiteral("progress"), progress}};
    if (!status.isEmpty())
        variables.insert(QStringLiteral("status"), status);
    return request(
        QStringLiteral(
            "mutation ($mediaId: Int!, $progress: Int!, "
            "$status: MediaListStatus) { SaveMediaListEntry("
            "mediaId: $mediaId, progress: $progress, status: $status) { "
            "id mediaId status progress updatedAt } }"),
        variables);
}

std::optional<QJsonObject> aniListMediaListEntryRequest(
    const QString &userId,
    const QString &mediaId)
{
    const auto user = positiveId(userId);
    const auto media = positiveId(mediaId);
    if (!user || !media)
        return std::nullopt;
    return request(
        QStringLiteral(
            "query ($userId: Int!, $mediaId: Int!) { MediaList("
            "userId: $userId, mediaId: $mediaId) { id mediaId status "
            "progress updatedAt } }"),
        QJsonObject{{QStringLiteral("userId"), *user},
                    {QStringLiteral("mediaId"), *media}});
}

std::optional<AniListViewer> aniListParseViewer(const QJsonDocument &document)
{
    const auto data = cleanData(document);
    if (!data)
        return std::nullopt;
    const QJsonValue viewer = data->value(QStringLiteral("Viewer"));
    if (!viewer.isObject())
        return std::nullopt;
    const QJsonObject object = viewer.toObject();
    const auto id = idString(object.value(QStringLiteral("id")));
    if (!id)
        return std::nullopt;
    AniListViewer result;
    result.userId = *id;
    result.userName = object.value(QStringLiteral("name"))
                          .toString().simplified().left(128);
    return result;
}

std::optional<AniListListPage> aniListParseMediaListCollection(
    const QJsonDocument &document,
    AniListMediaKind kind)
{
    const auto data = cleanData(document);
    if (!data)
        return std::nullopt;
    const QJsonValue collection =
        data->value(QStringLiteral("MediaListCollection"));
    if (!collection.isObject())
        return std::nullopt;
    const QJsonObject object = collection.toObject();
    const QJsonValue hasNext = object.value(QStringLiteral("hasNextChunk"));
    const QJsonValue lists = object.value(QStringLiteral("lists"));
    if (!hasNext.isBool() || !lists.isArray()
        || lists.toArray().size() > kMaximumLists) {
        return std::nullopt;
    }

    AniListListPage page;
    page.hasNextChunk = hasNext.toBool();
    for (const QJsonValue &listValue : lists.toArray()) {
        if (!listValue.isObject())
            return std::nullopt;
        const QJsonValue entries =
            listValue.toObject().value(QStringLiteral("entries"));
        if (!entries.isArray())
            return std::nullopt;
        for (const QJsonValue &entryValue : entries.toArray()) {
            if (page.items.size() >= kMaximumEntriesPerPage
                || !entryValue.isObject()) {
                return std::nullopt;
            }
            const QJsonObject entry = entryValue.toObject();
            const auto media =
                parseMedia(entry.value(QStringLiteral("media")), kind);
            const QString status =
                entry.value(QStringLiteral("status")).toString();
            const auto progress =
                requiredProgress(entry.value(QStringLiteral("progress")));
            const auto updated =
                updatedAtMs(entry.value(QStringLiteral("updatedAt")));
            if (!media || !aniListStatusIsValid(status) || !progress
                || !updated) {
                return std::nullopt;
            }
            AniListListItem item;
            item.kind = kind;
            item.aniListId = media->aniListId;
            item.malId = media->malId;
            item.title = media->title;
            item.status = status;
            item.progress = *progress;
            item.totalUnits = media->totalUnits;
            item.updatedAtMs = *updated;
            page.items.append(item);
        }
    }
    return page;
}

std::optional<AniListMediaRef> aniListParseMediaLookup(
    const QJsonDocument &document,
    AniListMediaKind kind)
{
    const auto data = cleanData(document);
    if (!data)
        return std::nullopt;
    const auto media = parseMedia(data->value(QStringLiteral("Media")), kind);
    if (!media)
        return std::nullopt;
    AniListMediaRef ref;
    ref.kind = kind;
    ref.aniListId = media->aniListId;
    ref.malId = media->malId;
    ref.title = media->title;
    ref.totalUnits = media->totalUnits;
    return ref;
}

std::optional<AniListEntryState> aniListParseSaveEntry(
    const QJsonDocument &document)
{
    const auto data = cleanData(document);
    if (!data)
        return std::nullopt;
    return parseEntryState(data->value(QStringLiteral("SaveMediaListEntry")));
}

std::optional<AniListEntryState> aniListParseMediaListEntry(
    const QJsonDocument &document)
{
    const auto data = cleanData(document);
    if (!data)
        return std::nullopt;
    return parseEntryState(data->value(QStringLiteral("MediaList")));
}

bool aniListResponseIsNotFound(const QJsonDocument &document)
{
    return errorsMatch(document, [](const QJsonObject &error) {
        qint64 status = 0;
        return integerValue(error.value(QStringLiteral("status")), &status)
            && status == 404;
    });
}

bool aniListResponseIsInvalidToken(const QJsonDocument &document)
{
    return errorsMatch(document, [](const QJsonObject &error) {
        qint64 status = 0;
        if (integerValue(error.value(QStringLiteral("status")), &status)
            && status == 401) {
            return true;
        }
        return error.value(QStringLiteral("message")).toString()
            .contains(QLatin1String("invalid token"), Qt::CaseInsensitive);
    });
}
