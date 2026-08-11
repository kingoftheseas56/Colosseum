#pragma once
// ImdbCatalog — read-only seam onto the baked IMDb index (data/imdb_catalog.db,
// built by scripts/theatre_brain/build_imdb_db.py). The movies/shows twin of
// MalCatalog: QML paints, C++ decides. Allowlisted keys only, every value bound,
// limit clamped; missing db => ready()==false and every accessor returns empty so
// the Theatre pages honestly omit index shelves.
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ImdbCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ImdbCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~ImdbCatalog() override;

    Q_INVOKABLE bool ready() const { return m_ok; }
    // query allowlist: type(movie|series|mini), order(rating|votes|year|episodes),
    // ratingMin, votesMin, votesMax, yearFrom, yearTo, runtimeMax, genre, lang,
    // notLang, excludeAnime, episodesMin. Unknown key/order -> empty. "series"
    // matches tvSeries+mini rows; "mini" only minis. Rows: {tt,type,title,year,
    // endYear,runtimeMin,rating,votes,episodes,origLang,isAnime,genres[]}.
    Q_INVOKABLE QVariantList titleCatalog(const QVariantMap& query,
                                          int offset = 0, int limit = 24) const;
    // Offline normalized-title identity lookup. The baked IMDb schema has no
    // plot corpus, so returned synopsis is intentionally empty.
    Q_INVOKABLE QVariantList matchByTitle(const QString& title, int year = 0) const;
    // Cheap prefix search over the baked normalized title column. Fully offline;
    // manual Identify may fall through to Cinemeta only when this returns empty.
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 20) const;
    // batch facts for live-row filtering: {tt: {rating, votes, isAnime}}
    Q_INVOKABLE QVariantMap titleFacts(const QStringList& ids) const;

private:
    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_conn;
};
