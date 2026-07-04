#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariantList>

#include "../series/seriesindex.h"

namespace {

void require(bool condition, const QString &message)
{
    if (!condition)
        qFatal("%s", qPrintable(message));
}

void execOrDie(QSqlDatabase &db, const QString &sql)
{
    QSqlQuery query(db);
    require(query.exec(sql),
            QStringLiteral("SQL failed: %1 :: %2")
                .arg(sql, query.lastError().text()));
}

QString createCanonicalFixture()
{
    QTemporaryDir dir;
    require(dir.isValid(), QStringLiteral("Temporary directory could not be created"));

    const QString dbPath = dir.filePath(QStringLiteral("canonical.sqlite"));

    {
        const QString connectionName = QStringLiteral("seriesindex_smoke_fixture");
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        require(db.open(),
                QStringLiteral("Fixture database failed to open: %1").arg(db.lastError().text()));

        execOrDie(db, QStringLiteral(
            "CREATE TABLE work ("
            "  id INTEGER PRIMARY KEY,"
            "  canonical_title TEXT NOT NULL,"
            "  canonical_author TEXT NOT NULL,"
            "  normalized_title TEXT NOT NULL,"
            "  normalized_author TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  confidence REAL NOT NULL"
            ")"));
        execOrDie(db, QStringLiteral(
            "CREATE TABLE series ("
            "  id INTEGER PRIMARY KEY,"
            "  canonical_title TEXT NOT NULL,"
            "  normalized_title TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  confidence REAL NOT NULL"
            ")"));
        execOrDie(db, QStringLiteral(
            "CREATE TABLE series_membership ("
            "  id INTEGER PRIMARY KEY,"
            "  work_id INTEGER NOT NULL,"
            "  series_id INTEGER NOT NULL,"
            "  display_position TEXT,"
            "  sort_order REAL,"
            "  confidence REAL NOT NULL,"
            "  source TEXT NOT NULL"
            ")"));
        execOrDie(db, QStringLiteral(
            "CREATE TABLE edition ("
            "  id INTEGER PRIMARY KEY,"
            "  work_id INTEGER NOT NULL,"
            "  isbn TEXT,"
            "  language TEXT,"
            "  publication_date TEXT,"
            "  publisher TEXT,"
            "  file_format TEXT,"
            "  source TEXT NOT NULL,"
            "  confidence REAL NOT NULL"
            ")"));
        execOrDie(db, QStringLiteral(
            "CREATE TABLE download_candidate ("
            "  id INTEGER PRIMARY KEY,"
            "  work_id INTEGER NOT NULL,"
            "  edition_id INTEGER,"
            "  source TEXT NOT NULL,"
            "  url TEXT,"
            "  file_hash TEXT,"
            "  file_format TEXT,"
            "  title TEXT,"
            "  author TEXT,"
            "  confidence REAL NOT NULL,"
            "  visible_only INTEGER NOT NULL DEFAULT 1"
            ")"));
        execOrDie(db, QStringLiteral(
            "CREATE TABLE source_assertion ("
            "  id INTEGER PRIMARY KEY,"
            "  source TEXT NOT NULL,"
            "  source_key TEXT,"
            "  subject_type TEXT NOT NULL,"
            "  subject_id INTEGER,"
            "  predicate TEXT NOT NULL,"
            "  value TEXT,"
            "  confidence REAL NOT NULL"
            ")"));

        execOrDie(db, QStringLiteral(
            "INSERT INTO work(id, canonical_title, canonical_author, normalized_title, normalized_author, source, confidence) VALUES "
            "(1, 'A Game of Thrones', 'George R.R. Martin', 'a game of thrones', 'george r r martin', 'goodreads_seed', 0.90),"
            "(2, 'A Clash of Kings', 'George R.R. Martin', 'a clash of kings', 'george r r martin', 'goodreads_seed', 0.90),"
            "(3, 'The Name of the Wind', 'Patrick Rothfuss', 'a name of wind', 'patrick rothfuss', 'goodreads_seed', 0.90),"
            "(4, 'The Wise Man''s Fear', 'Patrick Rothfuss', 'a wise man s fear', 'patrick rothfuss', 'goodreads_seed', 0.90)"
        ));
        execOrDie(db, QStringLiteral(
            "INSERT INTO series(id, canonical_title, normalized_title, source, confidence) VALUES "
            "(10, 'A Song of Ice and Fire', 'a song of ice and fire', 'goodreads_seed', 0.85),"
            "(11, 'The Kingkiller Chronicle', 'a kingkiller chronicle', 'goodreads_seed', 0.85),"
            "(12, 'Kingkiller', 'kingkiller', 'goodreads_seed', 0.60)"
        ));
        execOrDie(db, QStringLiteral(
            "INSERT INTO series_membership(id, work_id, series_id, display_position, sort_order, confidence, source) VALUES "
            "(100, 1, 10, '1', 1.0, 0.85, 'goodreads_seed'),"
            "(101, 1, 10, '1', 1.0, 0.45, 'libgen'),"
            "(102, 2, 10, '2', 2.0, 0.85, 'goodreads_seed'),"
            "(110, 3, 11, '1', 1.0, 0.85, 'goodreads_seed'),"
            "(111, 4, 11, '2', 2.0, 0.85, 'goodreads_seed'),"
            "(120, 3, 12, '1', 1.0, 0.55, 'goodreads_seed'),"
            "(121, 4, 12, '2', 2.0, 0.55, 'goodreads_seed')"
        ));
        execOrDie(db, QStringLiteral(
            "INSERT INTO edition(id, work_id, isbn, language, publication_date, publisher, file_format, source, confidence) VALUES "
            "(200, 1, '9780000000001', 'English', '1996', 'Voyager', 'EPUB', 'libgen', 0.78)"
        ));
        execOrDie(db, QStringLiteral(
            "INSERT INTO download_candidate(id, work_id, edition_id, source, url, file_hash, file_format, title, author, confidence, visible_only) VALUES "
            "(300, 1, 200, 'libgen', NULL, 'abcd1234abcd1234abcd1234abcd1234', 'EPUB', 'A Game of Thrones', 'George R.R. Martin', 0.85, 0),"
            "(310, 3, NULL, 'libgen', NULL, 'ee11ee11ee11ee11ee11ee11ee11ee11', 'EPUB', 'The Name of the Wind', 'Patrick Rothfuss', 0.80, 0),"
            "(311, 4, NULL, 'libgen', NULL, 'ff22ff22ff22ff22ff22ff22ff22ff22', 'EPUB', 'The Wise Man''s Fear', 'Patrick Rothfuss', 0.80, 0)"
        ));
        // Goodreads readership per work — the popularity signal topSeries ranks by (flagship = MAX member).
        // Kingkiller's flagship (2,000,000) outranks ASOIAF's (1,000,000); series 11 & 12 are the same
        // real series under alias titles (same author + flagship) and must collapse to one shelf tile.
        execOrDie(db, QStringLiteral(
            "INSERT INTO source_assertion(id, source, subject_type, subject_id, predicate, value, confidence) VALUES "
            "(400, 'goodreads_seed', 'work', 1, 'ratings_count', '1000000', 0.9),"
            "(401, 'goodreads_seed', 'work', 2, 'ratings_count', '500000', 0.9),"
            "(402, 'goodreads_seed', 'work', 3, 'ratings_count', '2000000', 0.9),"
            "(403, 'goodreads_seed', 'work', 4, 'ratings_count', '100000', 0.9),"
            "(410, 'goodreads_shelves', 'work', 1, 'genre', 'Sci-Fi & Fantasy', 0.8),"
            "(411, 'goodreads_shelves', 'work', 1, 'genres', 'Sci-Fi & Fantasy, Fiction & Literature', 0.8)"
        ));

        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    const QString persisted = QDir::temp().filePath(QStringLiteral("seriesindex_smoke_fixture.sqlite"));
    QFile::remove(persisted);
    require(QFile::copy(dbPath, persisted), QStringLiteral("Fixture database copy failed"));
    return persisted;
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString dbPath = createCanonicalFixture();
    SeriesIndex index(dbPath);

    const QVariantMap hit = index.lookup(QStringLiteral("A Game of Thrones"),
                                         QStringLiteral("George R.R. Martin"));
    require(hit.value(QStringLiteral("found")).toBool(),
            QStringLiteral("Canonical lookup should find AGOT"));
    require(hit.value(QStringLiteral("series")).toString() == QStringLiteral("A Song of Ice and Fire"),
            QStringLiteral("Canonical lookup should return the series title"));
    require(hit.value(QStringLiteral("position")).toString() == QStringLiteral("1"),
            QStringLiteral("Canonical lookup should return the series position"));
    require(hit.value(QStringLiteral("displayTitle")).toString() == QStringLiteral("A Game of Thrones"),
            QStringLiteral("Canonical lookup should preserve the work title"));

    const QVariantList entries = index.seriesEntries(QStringLiteral("A Song of Ice and Fire"));
    require(entries.size() == 2,
            QStringLiteral("Canonical seriesEntries should dedupe duplicate memberships"));
    require(entries[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("A Game of Thrones"),
            QStringLiteral("First canonical series entry should be AGOT"));
    require(entries[1].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("A Clash of Kings"),
            QStringLiteral("Second canonical series entry should be ACOK"));

    const QVariantList searchHits = index.search(QStringLiteral("song of ice fire"), 10);
    require(!searchHits.isEmpty(),
            QStringLiteral("Canonical search should return hits for the series query"));
    require(searchHits[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("series"),
            QStringLiteral("Canonical search should surface the series entity first"));
    require(searchHits[0].toMap().value(QStringLiteral("series")).toString() == QStringLiteral("A Song of Ice and Fire"),
            QStringLiteral("Canonical search should preserve the canonical series title"));

    const QVariantMap detail = index.bookDetailById(1);
    require(detail.value(QStringLiteral("canonical")).toBool(),
            QStringLiteral("Canonical detail should mark the result as canonical"));
    require(detail.value(QStringLiteral("series")).toString() == QStringLiteral("A Song of Ice and Fire"),
            QStringLiteral("Canonical detail should include the parent series"));
    const QVariantList candidates = detail.value(QStringLiteral("downloadCandidates")).toList();
    require(candidates.size() == 1,
            QStringLiteral("Canonical detail should expose the LibGen candidate"));
    require(candidates[0].toMap().value(QStringLiteral("md5")).toString() == QStringLiteral("abcd1234abcd1234abcd1234abcd1234"),
            QStringLiteral("Canonical detail should preserve the LibGen md5"));
    require(detail.value(QStringLiteral("genre")).toString() == QStringLiteral("Sci-Fi & Fantasy"),
            QStringLiteral("Canonical detail should carry the enriched Goodreads-shelf genre"));
    require(detail.value(QStringLiteral("genreLine")).toString().startsWith(QStringLiteral("Sci-Fi & Fantasy")),
            QStringLiteral("Canonical detail genreLine should lead with the genre when present"));

    const QVariantMap byTitle = index.bookDetail(QStringLiteral("A Game of Thrones"),
                                                 QStringLiteral("George R.R. Martin"));
    require(byTitle.value(QStringLiteral("workId")).toInt() == 1,
            QStringLiteral("Canonical title detail should resolve the same work id"));
    require(byTitle.value(QStringLiteral("title")).toString() == QStringLiteral("A Game of Thrones"),
            QStringLiteral("Canonical title detail should preserve the work title"));

    const QVariantList topBooks = index.topBooks(10);
    require(topBooks.size() == 4,
            QStringLiteral("Canonical topBooks should surface every downloadable fixture work"));
    require(topBooks[0].toMap().value(QStringLiteral("caption")).toString() == QStringLiteral("A Game of Thrones"),
            QStringLiteral("Canonical topBooks should rank the highest-confidence md5-backed title first"));
    require(topBooks[0].toMap().value(QStringLiteral("author")).toString() == QStringLiteral("George R.R. Martin"),
            QStringLiteral("Canonical topBooks should preserve the canonical author"));

    // topSeries: popular multi-book series, ranked by flagship readership, alias-duplicates collapsed.
    const QVariantList topSeries = index.topSeries(12);
    require(topSeries.size() == 2,
            QStringLiteral("Canonical topSeries should keep both real series and drop the alias duplicate"));
    require(topSeries[0].toMap().value(QStringLiteral("author")).toString() == QStringLiteral("Patrick Rothfuss"),
            QStringLiteral("Canonical topSeries should rank the higher-readership series (Kingkiller) first"));
    require(topSeries[0].toMap().value(QStringLiteral("count")).toInt() == 2,
            QStringLiteral("Canonical topSeries should report the series member count"));
    require(topSeries[0].toMap().value(QStringLiteral("canonical")).toBool(),
            QStringLiteral("Canonical topSeries tiles should be marked canonical"));
    require(topSeries[1].toMap().value(QStringLiteral("series")).toString() == QStringLiteral("A Song of Ice and Fire"),
            QStringLiteral("Canonical topSeries should surface the lower-readership series second"));

    QFile::remove(dbPath);
    return 0;
}
