#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

namespace Colosseum::Player2
{
// Player2HostServices is the ORCHESTRATION SEAM between the player and the app. The player asks
// (typed requests) and renders whatever the host resolves; it never searches, ranks, downloads or
// persists on its own — those policies live in the host (production: the app's catalog / stream / the
// download store; lab: deterministic fixtures). Every request resolves EXACTLY ONCE via its matching
// signal, whose payload is one of: data, an empty collection, or an error map {"error": "..."}. This
// keeps the engine and QML free of catalog/source policy (Task 14 contract).
//
// Payloads are QVariant (QML-native, no meta-type registration): a map per object, a list of maps per
// collection. Documented shapes:
//   episode  : { mediaId, title, season, episode, durationSeconds, poster, progressFraction?,
//               watched?, dead? , error? }  (progressFraction/watched are host-owned; "now-playing"
//               is NOT here — the shell derives it by comparing media ids to what it plays)
//   source   : { id, title, url, quality, sizeBytes, seeders, dead?, current? }
//   subtitle : { id, url, lang, langName, provider, downloads, external }
//   segment  : { kind("intro"|"recap"|"credits"), startSeconds, endSeconds, autoSkip }
//   download : { sourceId, state("queued"|"active"|"ready"|"failed"), progress, error? }
//   metadata : { mediaId, title, logo, backdrop, seasons, year?, plot?, resumeSeconds }
class Player2HostServices : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~Player2HostServices() override = default;

    // --- typed requests (the shell calls these; each resolves once via the matching signal) --------
    // direction: -1 previous, +1 next. Resolves adjacentEpisodeResolved with an episode map (possibly
    // {dead:true} at a series boundary, or {error}).
    Q_INVOKABLE virtual void requestAdjacentEpisode(const QString &mediaId, int direction) = 0;
    // The whole episode list for one season (the browser's season pills + list). Resolves once via
    // seasonEpisodesResolved with an episode list in ascending episode order (empty if none, never an
    // error). Fetched lazily per season so switching seasons never front-loads the rest.
    Q_INVOKABLE virtual void requestSeasonEpisodes(const QString &mediaId, int season) = 0;
    // Ranked alternate sources for the SAME media (identity/position preserved by the caller).
    Q_INVOKABLE virtual void requestAlternateSources(const QString &mediaId) = 0;
    // Online subtitle candidates for the media (provider search lives in the host).
    Q_INVOKABLE virtual void requestOnlineSubtitles(const QString &mediaId) = 0;
    // Intro/recap/credits skip segments for the media.
    Q_INVOKABLE virtual void requestSkipSegments(const QString &mediaId) = 0;
    // Intent to download a chosen source; progress arrives via downloadStateChanged (may fire more
    // than once — it is a state stream, the one documented exception to resolve-once).
    Q_INVOKABLE virtual void requestDownload(const QString &mediaId, const QString &sourceId) = 0;
    // Hydrate richer metadata (logo/backdrop/seasons/resume point) for the media.
    Q_INVOKABLE virtual void requestMetadata(const QString &mediaId) = 0;
    // Fire-and-forget progress persistence (resume points, watched state). No result.
    Q_INVOKABLE virtual void reportProgress(const QString &mediaId, double position, double duration) = 0;

signals:
    void adjacentEpisodeResolved(const QString &mediaId, int direction, const QVariantMap &episode);
    void seasonEpisodesResolved(const QString &mediaId, int season, const QVariantList &episodes);
    void alternateSourcesResolved(const QString &mediaId, const QVariantList &sources);
    void onlineSubtitlesResolved(const QString &mediaId, const QVariantList &subtitles);
    void skipSegmentsResolved(const QString &mediaId, const QVariantList &segments);
    void downloadStateChanged(const QString &mediaId, const QVariantMap &state);
    void metadataResolved(const QString &mediaId, const QVariantMap &metadata);
};
}
