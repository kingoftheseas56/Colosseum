#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

// AniList speaks GraphQL over one fixed POST endpoint. Everything in this file
// is pure: request builders return the JSON body (`query` + `variables`) and
// parsers turn a decoded response into plain structs. Nothing here touches the
// network, the vault, or any token.
//
// Manga progress is CHAPTERS only. No query or mutation here reads or writes
// `progressVolumes`; Tankoban (volume) mode is never representable.

enum class AniListMediaKind : quint8 {
    Anime,
    Manga
};

struct AniListAuthConfiguration {
    QString clientId;
    QUrl authorizationEndpoint;
    QUrl graphqlEndpoint;
    QUrl redirectUri;
    QString appName;
    QString appVersion;
};

struct AniListViewer {
    QString userId;
    QString userName;
};

struct AniListListItem {
    AniListMediaKind kind = AniListMediaKind::Anime;
    QString aniListId;
    QString malId; // Empty when AniList has no MAL cross-reference.
    QString title;
    QString status;
    int progress = 0;
    int totalUnits = 0;
    qint64 updatedAtMs = 0;
};

struct AniListListPage {
    QList<AniListListItem> items;
    bool hasNextChunk = false;
};

// A media record from an idMal lookup; no list state attached.
struct AniListMediaRef {
    AniListMediaKind kind = AniListMediaKind::Anime;
    QString aniListId;
    QString malId;
    QString title;
    int totalUnits = 0;
};

// List state returned by SaveMediaListEntry or a single-entry readback.
struct AniListEntryState {
    QString aniListId; // Media id.
    QString status;
    int progress = 0;
    qint64 updatedAtMs = 0;
};

struct AniListRemoteIdentity {
    AniListMediaKind kind = AniListMediaKind::Anime;
    QString aniListId;
    int unitNumber = 0;
    bool listAggregate = false;

    QString remoteMediaId() const;
};

struct AniListMutation {
    QJsonObject body;
    int intendedProgress = 0;
    bool intendedCompleted = false;
};

bool aniListConfigurationIsValid(const AniListAuthConfiguration &configuration);
std::optional<AniListAuthConfiguration> aniListProductionConfiguration();

std::optional<AniListRemoteIdentity> aniListRemoteIdentity(
    const QString &remoteMediaId);

QString aniListMediaTypeName(AniListMediaKind kind);
bool aniListStatusIsValid(const QString &status);
QString aniListSafeStatusLabel(const QString &status);
QString aniListListKey(AniListMediaKind kind, const QString &aniListId);

// Request builders. Each returns nullopt for out-of-range arguments.
QJsonObject aniListViewerRequest();
std::optional<QJsonObject> aniListMediaListCollectionRequest(
    const QString &userId,
    AniListMediaKind kind,
    int chunk = 1,
    int perChunk = 100);
std::optional<QJsonObject> aniListMediaByMalIdRequest(
    const QString &malId,
    AniListMediaKind kind);
// Empty `status` leaves the entry's status untouched.
std::optional<QJsonObject> aniListSaveEntryRequest(
    const QString &mediaId,
    int progress,
    const QString &status);
std::optional<QJsonObject> aniListMediaListEntryRequest(
    const QString &userId,
    const QString &mediaId);

// Parsers fail closed: any GraphQL `errors` array, missing field, or wrong
// type yields nullopt.
std::optional<AniListViewer> aniListParseViewer(const QJsonDocument &document);
std::optional<AniListListPage> aniListParseMediaListCollection(
    const QJsonDocument &document,
    AniListMediaKind kind);
std::optional<AniListMediaRef> aniListParseMediaLookup(
    const QJsonDocument &document,
    AniListMediaKind kind);
std::optional<AniListEntryState> aniListParseSaveEntry(
    const QJsonDocument &document);
std::optional<AniListEntryState> aniListParseMediaListEntry(
    const QJsonDocument &document);

// True when the response carries a GraphQL "Not Found" (404) error, so callers
// can tell "no such record" from a malformed response.
bool aniListResponseIsNotFound(const QJsonDocument &document);
// True when the GraphQL errors say the bearer token is invalid or expired.
bool aniListResponseIsInvalidToken(const QJsonDocument &document);
