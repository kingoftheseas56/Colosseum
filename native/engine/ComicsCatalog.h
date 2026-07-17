#pragma once
// ComicsCatalog — read-only seam onto the availability-first SQLite catalogue
// (spec 2026-07-17 phase 2b). QML paints, C++ decides: the db, its schema, and
// query shape live here; QML gets QVariant maps/lists. Point queries are indexed
// and sub-ms at this scale (21.5k series / 53k downloads), so the API is
// synchronous — no async ceremony a 31 MB local file doesn't need.
// Missing/invalid db => ready()==false and every accessor returns empty (the app
// runs on without the catalogue — it is pipeline-deployed, not shipped).
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

class ComicsCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ComicsCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~ComicsCatalog() override;
    Q_INVOKABLE bool ready() const { return m_ok; }
    // {gcdId,title,year,yearEnded,issueCount,publisher,cover,synopsis,downloads,kinds,latestPost} or {}
    Q_INVOKABLE QVariantMap series(int gcdId) const;
    // ranked: exact-title class first, then prefix, then contains; downloads DESC within class
    // rows: {gcdId,title,year,yearEnded,publisher,cover,downloads}
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 30) const;
    // {postId,title,link,date,kind,method,fanMade,yearStart} date DESC
    Q_INVOKABLE QVariantList downloadsFor(int gcdId) const;
    // every series whose title equals text (case-insensitive), downloads DESC —
    // lets QML decide "unambiguous title -> run page, else live franchise shelf"
    Q_INVOKABLE QVariantList exactMatches(const QString& text) const;
private:
    QSqlDatabase m_db;
    QString m_conn;
    bool m_ok = false;
};
