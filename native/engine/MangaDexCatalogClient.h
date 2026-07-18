// MangaDexCatalogClient.h
//
// On-demand single-series volume fetch against the MangaDex API. Replaces
// MangaFireCatalogClient: MangaFire relaunched as a JS SPA on 2026-07 and cut
// volumes from the product entirely (their own notice: "no longer able to keep
// your previous volume reading progress"), so every scrape endpoint now returns
// an empty app shell. MangaDex is keyless/no-login (standing sourcing law) and
// carries real per-volume tankōbon covers for the shelf.
//
// HTTP pipeline (all JSON, no HTML parsing):
//   1. GET api.mangadex.org/manga?title=<t>&order[relevance]=desc -> manga id
//      (best match by normalized title over title + altTitles, else first)
//   2. GET api.mangadex.org/cover?manga[]=<id>&order[volume]=asc  -> per-volume
//      cover files (paginated; prefer the original "ja" tankōbon cover)
//   3. GET api.mangadex.org/manga/<id>/aggregate                  -> volume ->
//      chapter ranges WHERE MANGADEX KNOWS THEM (partial for big licensed
//      titles — covers-first: a volume with no range ships chapterStart="" and
//      QML falls back to the flat chapter list, never a fabricated range)
//
// Each emitted volume map: { number:double, cover:string, chapterStart:string,
// chapterEnd:string } — ascending by volume number; empty range strings mean
// "range unknown". Same catalogReady/catalogFailed contract as the old client.
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas, all on the
// main thread; each fetch carries its own PendingFetch via shared_ptr.

#pragma once

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::mangadex {

// PHANTOM-VOLUME FOLD (2026-07-18, the Berserk finding) — exposed for the harness.
// MangaDex files alternate-edition covers under decimal volume keys ("1.1" = a
// variant cover OF volume 1, not a volume). A decimal key survives as a real
// volume only when the chapter aggregate lists it (chapterAnchored); every other
// decimal cover folds into floor(key): its tile disappears, and its cover is
// donated to the base volume iff the base has none.
void foldPhantomCoverVolumes(QMap<double, QString>& covers,
                             const QSet<double>& chapterAnchored);

class MangaDexCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit MangaDexCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaDexCatalogClient() override;

    // Fire the full pipeline for a single series.
    // Emits catalogReady(title, volumes) on success exactly once, or
    // catalogFailed(title, reason) on any step failure.
    // Concurrent calls are allowed; each carries its own internal state.
    void fetchByTitle(const QString& title);

signals:
    // volumes: ascending QVariantList of QVariantMap{number, cover, chapterStart, chapterEnd}
    void catalogReady(const QString& title, const QVariantList& volumes);
    void catalogFailed(const QString& title, const QString& reason);

private:
    struct PendingFetch;
    using PendingFetchPtr = std::shared_ptr<PendingFetch>;

    void stepSearch(PendingFetchPtr pending);
    void stepCovers(PendingFetchPtr pending, int offset);
    void stepAggregate(PendingFetchPtr pending);
    void finish(PendingFetchPtr pending);

    void emitFailure(PendingFetchPtr pending, const QString& reason);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::mangadex
