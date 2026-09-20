#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtGlobal>

struct StremioLoopbackCallback {
    bool accepted = false;
    QByteArray authKey;
    QString error;
};

struct StremioAccountIdentity {
    QString accountId;
    QString displayName;
};

// The datastore protocol returns opaque Stremio media IDs.  They remain
// opaque through this boundary: identifiers such as `kitsu:alpha` must never
// be guessed into a different provider's identity.
struct StremioLibraryItem {
    QString id;
    QString type;
    bool removed = false;
    bool temporary = false;
    bool libraryMember = false;
    QJsonObject raw;
};

struct StremioLibraryItemDecode {
    QList<StremioLibraryItem> items;
    int malformedRows = 0;
};

// Method and body are deliberately kept native-only.  The body may contain a
// device credential and is never made a QML property or a diagnostic string.
struct StremioDatastoreRequest {
    QString method;
    QJsonObject payload;
};

// Addon collection entries remain opaque provider documents. The codec
// validates only their bounded transport identity and known container fields,
// retaining every other field for a later fresh-read/rebase write.
struct StremioAddonCollectionDecode {
    QJsonArray addons;
    int malformedRows = 0;
    bool containerValid = false;
};

// Video order is supplied by the existing Theatre/addon metadata reader. The
// Stremio watched bitfield has no safe meaning without that exact order.
struct StremioEpisodeIdentity {
    QString videoId;
    int season = -1;
    int episode = -1;
};

// Portable records shaped for the existing Theatre owners. They carry no
// request envelope, credential, addon URL, or inferred cross-provider ID.
struct StremioTheatreItemProjection {
    bool valid = false;
    bool hasCollection = false;
    bool hasProgress = false;
    bool hasHistory = false;
    // Movie watched/unwatched is current state. It intentionally remains
    // separate from cumulative History, whose dated records are additive.
    bool hasWatchState = false;
    bool watched = false;
    qint64 watchActionAtMs = 0;
    QVariantMap collection;
    QVariantMap progress;
    QVariantMap history;
    QString error;
};

namespace StremioCodec {

StremioLoopbackCallback decodeLoopbackCallback(
    const QByteArray &request,
    const QString &expectedPath);

bool isProductionEndpoint(const QUrl &endpoint);
bool isTaggedLoopbackEndpoint(const QUrl &endpoint);
QUrl browserLoginUrl(const QUrl &callback);
bool decodeGetUserResult(
    const QJsonObject &response,
    StremioAccountIdentity *identity,
    QString *error = nullptr);

// Stremio account datastore support. Meta is intentionally lightweight;
// callers fetch detailed `libraryItem` rows in capped batches and treat a bad
// row as isolated provider data rather than poisoning an entire pull.
QStringList decodeLibraryItemMeta(
    const QJsonValue &result,
    int *malformedRows = nullptr,
    int maximumRows = 256);
QList<QStringList> boundedLibraryItemBatches(
    const QStringList &ids,
    int maximumBatchSize = 64);
StremioLibraryItemDecode decodeLibraryItems(
    const QJsonValue &result,
    int maximumRows = 256);

// Applies a state patch without discarding Stremio fields Colosseum does not
// own. The item identity is immutable across a datastorePut.
bool mergeLibraryItemPatch(
    const QJsonObject &existing,
    const QJsonObject &patch,
    QJsonObject *merged,
    QString *error = nullptr);

StremioDatastoreRequest datastoreMetaRequest(const QByteArray &authKey);
QList<StremioDatastoreRequest> datastoreGetRequests(
    const QByteArray &authKey,
    const QStringList &ids,
    int maximumBatchSize = 64);
StremioDatastoreRequest datastorePutRequest(
    const QByteArray &authKey,
    const QJsonObject &change);

QString normalizedAddonTransportUrl(const QString &transportUrl);
StremioDatastoreRequest addonCollectionGetRequest(const QByteArray &authKey);
StremioDatastoreRequest addonCollectionSetRequest(
    const QByteArray &authKey,
    const QJsonArray &addons);
StremioAddonCollectionDecode decodeAddonCollection(
    const QJsonValue &result,
    int maximumRows = 64);

// The per-series watched value is an anchored, zlib-compressed bitfield. A
// malformed or ambiguous field fails closed and leaves the caller's existing
// state untouched; callers retain its raw provider value for retry.
bool decodeWatchedEpisodes(
    const QString &field,
    const QList<StremioEpisodeIdentity> &videos,
    QSet<QString> *watchedVideoIds,
    QString *error = nullptr);
bool encodeWatchedEpisodes(
    const QSet<QString> &watchedVideoIds,
    const QList<StremioEpisodeIdentity> &videos,
    QString *field,
    QString *error = nullptr);
// Checks a metadata episode against the opaque provider series identity. This
// intentionally recognizes only the existing Theatre root convention and
// never guesses an IMDb or fixed-width colon identity.
bool episodeBelongsToSeries(
    const QString &seriesId,
    const StremioEpisodeIdentity &episode);
bool movieFlaggedWatched(const StremioLibraryItem &item, bool *watched);

// Maps a single valid Stremio library item into existing Theatre Collection,
// Progress and History shapes. A malformed item has no partial owner output.
StremioTheatreItemProjection projectTheatreItem(
    const StremioLibraryItem &item);

} // namespace StremioCodec
