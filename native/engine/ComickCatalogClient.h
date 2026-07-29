// ComickCatalogClient.h
//
// Volume-structure source for tankoban mode. Replaces MangaDexCatalogClient behind
// the IDENTICAL catalogReady/catalogFailed contract, so MangaEngine and QML barely
// change. Two steps:
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
// Emitted volumes: ascending QVariantMap{number:double, cover:"", chapterStart, chapterEnd}.
// cover is ALWAYS empty. Undownloaded tiles use the shelf's numbered placeholder; a
// downloaded volume's cover is its own first page (MangaVolumeIndex). WeebCentral's
// chapter list carries no thumbnails (verified 2026-07-29) and Comick's per-volume
// covers don't exist, so there is nothing to fetch.
//
// Threading: pure QNetworkAccessManager + QObject::connect lambdas, all on the main
// thread; each fetch carries its own PendingFetch via shared_ptr, so concurrent calls
// never share state (same shape as MangaDexCatalogClient).

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

class ComickCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit ComickCatalogClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // DB by WeebCentral ULID first, live Comick scrape on miss. Emits exactly one of
    // catalogReady/catalogFailed per call. Concurrent calls are allowed.
    void fetchSeries(const QString& weebCentralId, const QString& title);

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
    void emitFailure(PendingFetchPtr pending, const QString& reason);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga::comick
