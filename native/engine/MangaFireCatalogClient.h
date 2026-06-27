// MangaFireCatalogClient.h
//
// On-demand single-series volume fetch against mangafire.to, lifted from
// Tankoban 2's proven C++ scraper. Colosseum-lean form: the proven HTTP
// pipeline + HTML parsing are kept verbatim; TB2's disk-cache layer
// (writeCatalogJson / LocalMangaCatalogLoader / the MangaCatalog struct /
// patchWeebCentralBlock) is dropped — Colosseum has no read-back loader, so
// writing JSON would be cost with zero benefit. Results go straight to QML as
// a plain QVariantList; the engine bridge never sees a C++ struct.
//
// HTTP pipeline (mirrors TB2 / mangafire_ingest.py):
//   1. GET /filter?keyword=<title>       -> first /manga/<slug>.<hash> link
//      (Cloudflare-walled in practice -> falls back to sitemap discovery)
//   2. GET /sitemap.xml -> sitemap-list-*.xml -> fuzzy slug match (the path
//      that actually works from a non-browser client)
//   3. GET /ajax/manga/<hash>/volume/en  -> volume number + per-volume cover URL
//   4. GET /ajax/manga/<hash>/chapter/en -> vol -> chapter range mapping
//
// Each emitted volume map: { number:int, cover:string, chapterStart:string,
// chapterEnd:string } — clean, contiguous, with real per-volume covers, so QML
// needs no MangaDex-style reconstruction.
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas, all on the
// main thread; each fetch carries its own PendingFetch via shared_ptr.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::mangafire {

class MangaFireCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit MangaFireCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaFireCatalogClient() override;

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

    void stepFilter(PendingFetchPtr pending);
    void stepSitemapIndex(PendingFetchPtr pending, const QString& fallbackReason);
    void stepSitemapList(PendingFetchPtr pending,
                         const QStringList& listUrls,
                         int index);
    void stepVolume(PendingFetchPtr pending);
    void stepChapter(PendingFetchPtr pending);
    void finish(PendingFetchPtr pending);

    void emitFailure(PendingFetchPtr pending, const QString& reason);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::mangafire
