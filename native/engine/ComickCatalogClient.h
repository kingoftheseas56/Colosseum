// ComickCatalogClient.h
//
// Volume-structure source for tankoban mode. It is the only one: MangaEngine and QML
// see nothing but the catalogReady/catalogFailed contract below — exactly one of the
// two per call, carrying the title back so a caller can match it. Two steps:
//   1. DB read  — raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/
//                 main/db/<weebcentral-ulid>.json, unauthenticated, kept warm by our
//                 batch job. The record carries volumes plus its own gate verdict.
//   2. On miss  — live Comick scrape (api.comick.dev, token-free, browser UA REQUIRED
//                 or 403): search by title -> hid -> chapters across ALL languages
//                 (en-only tagging is sparse — the My Hero Academia finding) ->
//                 ComickVolumeGrouper.
// Either path ends at the completeness gate. Gate-fail => catalogFailed => the app
// shows the flat WeebCentral chapter list. There is NO interpolation anywhere.
//
// Emitted volumes: ascending QVariantMap{number:double, cover, chapterStart, chapterEnd}.
// cover carries the published DB record's per-volume `cover` URL when present (the
// Fandom-harvested art in colosseum-volume-db); it is empty for a volume the record has
// no art for and for the live WeebCentral/Comick scrape path (which has no per-volume
// covers). An empty cover means the shelf draws its numbered placeholder, and a
// downloaded volume still uses its own first page (MangaVolumeIndex).
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas, all on the main
// thread; each fetch carries its own PendingFetch via shared_ptr, so concurrent calls
// never share state — there is no per-client mutable request state at all.
//
// ACCEPTED LIMIT, written down rather than left to be discovered: there is no
// destructor and no in-flight abort. Destroying the client with a request outstanding
// severs the connection, so that call emits NEITHER signal and its reply is never
// deleteLater'd. That is an accepted limit, not a regression, and unreachable while
// MangaEngine owns the client for the app's lifetime — but anything that starts
// creating and destroying these per-page has to fix it first, because a dropped call
// hangs the page-reveal gate that waits on the one-signal-per-call promise.

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

#include "ComickVolumeGrouper.h"

class QNetworkAccessManager;

namespace tankoban::manga::comick {

// Pure parse of a published DB record — exposed for the harness.
struct ParsedRecord {
    bool ok = false;           // false = malformed JSON
    bool qualified = false;
    QString gateReason;
    QVariantList volumes;      // emit-ready; empty unless qualified
};
ParsedRecord parseDbRecord(const QByteArray& json);

// Pure parse of a live chapters payload — exposed for the harness.
QList<ChapterRow> parseChapterRows(const QByteArray& json);

// Pure db-URL builder — exposed for the harness. No network, no member state. The
// published DB is keyed by WeebCentral ULID; Slice B adds a second key shape, `mal-<id>`,
// for a series resolved by MAL id instead. A non-empty `malId` ALWAYS wins, even when
// `weebCentralId` is also present — see fetchSeries for the call site.
QString dbUrlFor(const QString& weebCentralId, const QString& malId);

class ComickCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit ComickCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // DB by WeebCentral ULID first, live Comick scrape on miss. Emits exactly one of
    // catalogReady/catalogFailed per call. Concurrent calls are allowed. `malId`,
    // when non-empty, looks the record up as `mal-<malId>` instead of the WeebCentral
    // ULID (Slice B) — the search/chapters fallback path is unaffected either way.
    void fetchSeries(const QString& weebCentralId, const QString& title,
                     const QString& malId = QString());

signals:
    void catalogReady(const QString& title, const QVariantList& volumes);
    void catalogFailed(const QString& title, const QString& reason);

private:
    struct PendingFetch;
    using PendingFetchPtr = std::shared_ptr<PendingFetch>;

    void stepDbRecord(PendingFetchPtr pending);
    void stepSearch(PendingFetchPtr pending);
    void stepChapters(PendingFetchPtr pending);

    void emitReady(PendingFetchPtr pending, const QVariantList& volumes, const QString& line);
    void emitFailure(PendingFetchPtr pending, const QString& reason,
                     const QString& line = QString());

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::comick
