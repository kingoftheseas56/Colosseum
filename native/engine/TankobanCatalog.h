#pragma once
// TankobanCatalog — read-only seam onto the baked Tankoban volume catalogue
// (data/tankoban_catalog.db, built by scripts/manga_brain/build_tankoban_catalog.py
// from the arc's spine.jsonl, plus an optional BookWalker/mymangaindex cover
// harvest). Catalogue-independence Slice 1 (2026-08-20): the offline,
// provider-free source of per-series volume counts and covers — the data
// spine every later slice reads. MalCatalog pattern exactly: QML paints, C++
// decides; db/schema/query shape live here. Missing db => ready()==false and
// every accessor returns empty — later slices fall through to an honest
// shelf-less page, never a guess.
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

class TankobanCatalog final : public QObject {
    Q_OBJECT
public:
    explicit TankobanCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~TankobanCatalog() override;

    Q_INVOKABLE bool ready() const { return m_ok; }

    // {volumeCount:int, countBasis:string} — countBasis is "mal" or
    // "bookwalker". Empty map when the db is missing or malId is unknown.
    Q_INVOKABLE QVariantMap seriesInfo(int malId) const;

    // [{number:string, cover:string, name:string}], numeric-aware ordered
    // (2 before 10). Numbers "1".."N" are synthesized from seriesInfo's
    // volumeCount wherever no baked volume row exists for that number; baked
    // cover_url/name overlay the synthesized rows where present. Empty list
    // when the db is missing, malId is unknown, or the series has neither a
    // known count nor any baked volume rows.
    Q_INVOKABLE QVariantList volumes(int malId) const;

private:
    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_conn;
};
