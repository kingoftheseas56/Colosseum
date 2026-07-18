#pragma once
// MalCatalog — read-only seam onto the baked MyAnimeList catalog
// (data/mal_catalog.db, built by scripts/anime_brain/build_mal_db.py from the
// weekly Kaggle dump; genre-page revival, Hemanth 2026-07-18). QML paints, C++
// decides: db, schema, and query shape live here. Rows come back JIKAN-SHAPED
// (nested images.jpg, [{name}] credit/genre lists, Jikan status strings) so the
// genre pages' existing card mappers consume them without a rendering change.
// Missing db => ready()==false and every accessor returns empty — the pages
// fall through to their live Jikan/AniList/Kitsu ladder untouched.
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>

class MalCatalog final : public QObject {
    Q_OBJECT
public:
    explicit MalCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~MalCatalog() override;

    Q_INVOKABLE bool ready() const { return m_ok; }
    // medium: "anime" | "manga"; order: "members" | "score". Jikan-shaped maps.
    Q_INVOKABLE QVariantList genreEntries(const QString& medium, const QString& genre,
                                          const QString& order, int limit = 24) const;
    // the TRUE catalog total for a genre (baked before the browsable slice)
    Q_INVOKABLE int genreCount(const QString& medium, const QString& genre) const;
    // [{name, count}] for index tiles, count DESC
    Q_INVOKABLE QVariantList genreCounts(const QString& medium) const;

private:
    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_conn;
};
