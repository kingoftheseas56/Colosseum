#include "seriesindex.h"

#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace {
struct SeriesRow {
    int workId = 0;
    int seriesId = 0;
    QString title;
    QString author;
    QString series;
    QString position;
    QString allSeries;
    bool isBoxset = false;
    double rating = 0.0;
};

QString normalizeTitle(const QString &title)
{
    QString out = title.toCaseFolded();
    out.replace(QLatin1Char('&'), QStringLiteral(" and "));
    out.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    out.replace(QRegularExpression(QStringLiteral("\\bthe\\b")), QStringLiteral(" "));
    return out.simplified().trimmed();
}

QString fuzzyLikePattern(const QString &normalized)
{
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QStringLiteral("%");
    return QStringLiteral("%") + parts.join(QStringLiteral("%")) + QStringLiteral("%");
}

QString stripTrailingParenthetical(const QString &title)
{
    QString out = title;
    out.remove(QRegularExpression(QStringLiteral("\\s*\\([^()]*\\)\\s*$")));
    return out.trimmed();
}

QString stripSubtitle(const QString &title)
{
    const int colon = title.indexOf(QLatin1Char(':'));
    return colon >= 0 ? title.left(colon).trimmed() : title.trimmed();
}

QStringList lookupKeysForTitle(const QString &title)
{
    QStringList keys;
    const auto add = [&keys](const QString &candidate) {
        const QString normalized = normalizeTitle(candidate);
        if (!normalized.isEmpty() && !keys.contains(normalized))
            keys.push_back(normalized);
    };

    add(title);
    add(stripTrailingParenthetical(title));
    add(stripSubtitle(title));
    add(stripSubtitle(stripTrailingParenthetical(title)));
    add(stripTrailingParenthetical(stripSubtitle(title)));
    return keys;
}

QString surnameKey(const QString &author)
{
    QStringList parts = author.toLower().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};

    QString surname = parts.constLast();
    surname.remove(QRegularExpression(QStringLiteral("^[^\\p{L}\\p{N}]+|[^\\p{L}\\p{N}]+$")));
    return surname;
}

bool authorMatches(const SeriesRow &row, const QString &surname)
{
    return !surname.isEmpty() && row.author.toLower().contains(surname);
}

SeriesRow rowFromQuery(const QSqlQuery &query)
{
    SeriesRow row;
    row.title = query.value(QStringLiteral("title")).toString();
    row.author = query.value(QStringLiteral("author")).toString();
    row.series = query.value(QStringLiteral("series")).toString();
    row.position = query.value(QStringLiteral("position")).toString();
    row.allSeries = query.value(QStringLiteral("all_series")).toString();
    row.isBoxset = query.value(QStringLiteral("is_boxset")).toInt() != 0;
    row.rating = query.value(QStringLiteral("rating")).toDouble();
    row.workId = query.value(QStringLiteral("work_id")).toInt();
    row.seriesId = query.value(QStringLiteral("series_id")).toInt();
    return row;
}

QVariantMap foundMap(const SeriesRow &row)
{
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("workId"), row.workId},
        {QStringLiteral("seriesId"), row.seriesId},
        {QStringLiteral("series"), row.series},
        {QStringLiteral("position"), row.position},
        {QStringLiteral("allSeries"), row.allSeries},
        {QStringLiteral("isBoxset"), row.isBoxset},
        {QStringLiteral("rating"), row.rating},
        {QStringLiteral("displayTitle"), row.title},
    };
}

QVariantMap topBookMap(int workId,
                       const QString &title,
                       const QString &author,
                       const QString &series,
                       const QString &position,
                       const QString &cover)
{
    static const char *kC1[] = {
        "#6a4a2c", "#5a3a3f", "#3f5a4a", "#4a4063", "#6a5a2c", "#3f4a63"
    };
    static const char *kC2[] = {
        "#1d1209", "#180d10", "#101a14", "#13101f", "#1d1809", "#0e121b"
    };
    const int paletteIndex = qAbs(workId) % int(std::size(kC1));

    QVariantMap row{
        {QStringLiteral("workId"), workId},
        {QStringLiteral("title"), title},
        {QStringLiteral("caption"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("cover"), cover},
        {QStringLiteral("c1"), QString::fromLatin1(kC1[paletteIndex])},
        {QStringLiteral("c2"), QString::fromLatin1(kC2[paletteIndex])},
        {QStringLiteral("canonical"), true},
    };
    if (!series.isEmpty())
        row.insert(QStringLiteral("series"), series);
    if (!position.isEmpty())
        row.insert(QStringLiteral("seriesPosition"), position);
    return row;
}

// A series shelf tile: no cover art in the canonical graph, so the tile is a warm gradient stack
// (palette keyed off the series id) plus the roster count. Shape mirrors what BiblioSeries.qml opens
// with (`series` + `author`) and what the World shelf renders (`count`, `c1`/`c2`).
QVariantMap seriesTileMap(int seriesId,
                          const QString &series,
                          const QString &author,
                          int count)
{
    static const char *kC1[] = {
        "#5a2f45", "#6b2f45", "#7a3a4f", "#5a3550", "#3f5868", "#7a5a2f", "#2f5a55", "#4a3550"
    };
    static const char *kC2[] = {
        "#180c14", "#1a0c12", "#1c0f14", "#170d1b", "#0e161d", "#1d160c", "#0c1a18", "#150d1a"
    };
    const int paletteIndex = qAbs(seriesId) % int(std::size(kC1));
    return {
        {QStringLiteral("seriesId"), seriesId},
        {QStringLiteral("series"), series},
        {QStringLiteral("author"), author},
        {QStringLiteral("count"), count},
        {QStringLiteral("c1"), QString::fromLatin1(kC1[paletteIndex])},
        {QStringLiteral("c2"), QString::fromLatin1(kC2[paletteIndex])},
        {QStringLiteral("canonical"), true},
    };
}
}  // namespace

SeriesIndex::SeriesIndex(const QString &dbPath, QObject *parent)
    : QObject(parent),
      m_dbPath(QDir::cleanPath(dbPath)),
      m_connectionName(QStringLiteral("SeriesIndex.%1").arg(quintptr(this), 0, 16))
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qWarning().noquote() << QStringLiteral("SeriesIndex: QSQLITE driver unavailable.");
        return;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex: failed to open")
            << m_dbPath
            << QStringLiteral("read-only:")
            << m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return;
    }

    qInfo().noquote() << QStringLiteral("SeriesIndex: opened")
                      << m_dbPath
                      << QStringLiteral("(read-only)");
    detectSchema();
}

SeriesIndex::~SeriesIndex()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool SeriesIndex::isReady() const
{
    return m_db.isValid() && m_db.isOpen();
}

void SeriesIndex::detectSchema()
{
    m_schemaMode = SchemaMode::Unknown;
    if (!isReady())
        return;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"))) {
        qWarning().noquote() << QStringLiteral("SeriesIndex: schema probe failed:")
                             << query.lastError().text();
        return;
    }

    QStringList tables;
    while (query.next())
        tables.push_back(query.value(0).toString());

    if (tables.contains(QStringLiteral("books"))) {
        m_schemaMode = SchemaMode::LegacyBooks;
    } else if (tables.contains(QStringLiteral("work"))
               && tables.contains(QStringLiteral("series"))
               && tables.contains(QStringLiteral("series_membership"))) {
        m_schemaMode = SchemaMode::CanonicalGraph;
    }

    const QString mode =
        m_schemaMode == SchemaMode::LegacyBooks ? QStringLiteral("legacy-books")
        : m_schemaMode == SchemaMode::CanonicalGraph ? QStringLiteral("canonical-graph")
                                                     : QStringLiteral("unknown");
    qInfo().noquote() << QStringLiteral("SeriesIndex: schema mode") << mode;
}

QVariantMap SeriesIndex::lookup(const QString &title, const QString &author) const
{
    if (!isReady())
        return {{QStringLiteral("found"), false}};

    QSqlQuery query(m_db);
    if (m_schemaMode == SchemaMode::LegacyBooks) {
        query.prepare(QStringLiteral(
            "SELECT "
            "  0 AS work_id,"
            "  0 AS series_id,"
            "  title AS title,"
            "  author AS author,"
            "  series AS series,"
            "  position AS position,"
            "  all_series AS all_series,"
            "  is_boxset AS is_boxset,"
            "  rating AS rating "
            "FROM books WHERE title_key=?"));
    } else if (m_schemaMode == SchemaMode::CanonicalGraph) {
        query.prepare(QStringLiteral(
            "SELECT "
            "  w.id AS work_id,"
            "  COALESCE(s.id, 0) AS series_id,"
            "  w.canonical_title AS title,"
            "  w.canonical_author AS author,"
            "  COALESCE(s.canonical_title, '') AS series,"
            "  COALESCE(sm.display_position, '') AS position,"
            "  COALESCE(s.canonical_title, '') AS all_series,"
            "  0 AS is_boxset,"
            "  0.0 AS rating "
            "FROM work w "
            "LEFT JOIN series_membership sm ON sm.id = ("
            "  SELECT sm2.id FROM series_membership sm2 "
            "  WHERE sm2.work_id = w.id "
            "  ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
            "  LIMIT 1"
            ") "
            "LEFT JOIN series s ON s.id = sm.series_id "
            "WHERE w.normalized_title=?"));
    } else {
        return {{QStringLiteral("found"), false}};
    }

    const QString surname = surnameKey(author);
    bool sawHit = false;
    SeriesRow firstHit;

    for (const QString &key : lookupKeysForTitle(title)) {
        query.bindValue(0, key);
        if (!query.exec()) {
            qWarning().noquote()
                << QStringLiteral("SeriesIndex lookup query failed for")
                << key
                << QLatin1Char(':')
                << query.lastError().text();
            return {{QStringLiteral("found"), false}};
        }
        if (!query.next())
            continue;

        const SeriesRow row = rowFromQuery(query);
        if (!sawHit) {
            firstHit = row;
            sawHit = true;
        }
        if (authorMatches(row, surname))
            return foundMap(row);
    }

    return sawHit ? foundMap(firstHit) : QVariantMap{{QStringLiteral("found"), false}};
}

QVariantList SeriesIndex::seriesEntries(const QString &series) const
{
    QVariantList out;
    if (!isReady() || series.trimmed().isEmpty())
        return out;

    QSqlQuery query(m_db);
    if (m_schemaMode == SchemaMode::LegacyBooks) {
        query.prepare(QStringLiteral(
            "SELECT title,position,rating,year,cover FROM books "
            "WHERE series=? AND is_boxset=0 "
            "ORDER BY CAST(position AS REAL)"));
        query.bindValue(0, series);
    } else if (m_schemaMode == SchemaMode::CanonicalGraph) {
        query.prepare(QStringLiteral(
            "SELECT DISTINCT "
            "  w.id AS work_id,"
            "  COALESCE(("
            "    SELECT sm2.display_position FROM series_membership sm2 "
            "    WHERE sm2.work_id = w.id AND sm2.series_id = s.id "
            "    ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
            "    LIMIT 1"
            "  ), '') AS position,"
            "  w.canonical_title AS title,"
            "  0.0 AS rating,"
            "  0 AS year,"
            "  '' AS cover,"
            "  COALESCE(("
            "    SELECT sm2.sort_order FROM series_membership sm2 "
            "    WHERE sm2.work_id = w.id AND sm2.series_id = s.id "
            "    ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
            "    LIMIT 1"
            "  ), 999999.0) AS sort_order "
            "FROM series s "
            "JOIN series_membership sm ON sm.series_id = s.id "
            "JOIN work w ON w.id = sm.work_id "
            "WHERE s.normalized_title=? "
            "ORDER BY sort_order, title"));
        query.bindValue(0, normalizeTitle(series));
    } else {
        return out;
    }

    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex seriesEntries query failed for")
            << series
            << QLatin1Char(':')
            << query.lastError().text();
        return out;
    }

    while (query.next()) {
        if (m_schemaMode == SchemaMode::LegacyBooks) {
            out.push_back(QVariantMap{
                {QStringLiteral("position"), query.value(1).toString()},
                {QStringLiteral("title"), query.value(0).toString()},
                {QStringLiteral("author"), QString()},
                {QStringLiteral("rating"), query.value(2).toDouble()},
                {QStringLiteral("year"), query.value(3).toInt()},
                {QStringLiteral("cover"), query.value(4).toString()},
            });
        } else {
            out.push_back(QVariantMap{
                {QStringLiteral("workId"), query.value(QStringLiteral("work_id")).toInt()},
                {QStringLiteral("position"), query.value(QStringLiteral("position")).toString()},
                {QStringLiteral("title"), query.value(QStringLiteral("title")).toString()},
                {QStringLiteral("author"), QString()},
                {QStringLiteral("rating"), query.value(QStringLiteral("rating")).toDouble()},
                {QStringLiteral("year"), query.value(QStringLiteral("year")).toInt()},
                {QStringLiteral("cover"), query.value(QStringLiteral("cover")).toString()},
            });
        }
    }
    return out;
}

QVariantList SeriesIndex::search(const QString &queryText, int limit) const
{
    QVariantList out;
    if (!isReady())
        return out;

    const QString normalized = normalizeTitle(queryText);
    if (normalized.isEmpty())
        return out;

    const int boundedLimit = qBound(1, limit <= 0 ? 24 : limit, 100);

    QSqlQuery query(m_db);
    if (m_schemaMode == SchemaMode::CanonicalGraph) {
        query.prepare(QStringLiteral(
            "WITH series_hits AS ("
            "  SELECT "
            "    'series' AS kind,"
            "    s.id AS entity_id,"
            "    s.canonical_title AS title,"
            "    COALESCE(("
            "      SELECT w.canonical_author FROM series_membership sm2 "
            "      JOIN work w ON w.id = sm2.work_id "
            "      WHERE sm2.series_id = s.id "
            "      ORDER BY COALESCE(sm2.sort_order, 999999.0), w.id "
            "      LIMIT 1"
            "    ), '') AS author,"
            "    s.canonical_title AS series,"
            "    COUNT(DISTINCT sm.work_id) AS item_count,"
            "    CASE "
            "      WHEN s.normalized_title = ? THEN 200 "
            "      WHEN s.normalized_title LIKE ? THEN 140 "
            "      ELSE 0 "
            "    END AS score "
            "  FROM series s "
            "  LEFT JOIN series_membership sm ON sm.series_id = s.id "
            "  WHERE s.normalized_title LIKE ? "
            "  GROUP BY s.id, s.canonical_title, s.normalized_title"
            "), work_hits AS ("
            "  SELECT "
            "    'book' AS kind,"
            "    w.id AS entity_id,"
            "    w.canonical_title AS title,"
            "    w.canonical_author AS author,"
            "    COALESCE(s.canonical_title, '') AS series,"
            "    0 AS item_count,"
            "    CASE "
            "      WHEN w.normalized_title = ? THEN 180 "
            "      WHEN w.normalized_title LIKE ? THEN 130 "
            "      ELSE 0 "
            "    END "
            "    + CASE "
            "        WHEN COALESCE(s.canonical_title, '') <> '' THEN 10 "
            "        ELSE 0 "
            "      END AS score "
            "  FROM work w "
            "  LEFT JOIN series_membership sm ON sm.id = ("
            "    SELECT sm2.id FROM series_membership sm2 "
            "    WHERE sm2.work_id = w.id "
            "    ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
            "    LIMIT 1"
            "  ) "
            "  LEFT JOIN series s ON s.id = sm.series_id "
            "  WHERE w.normalized_title LIKE ? "
            ") "
            "SELECT kind, entity_id, title, author, series, item_count, score "
            "FROM ("
            "  SELECT * FROM series_hits "
            "  UNION ALL "
            "  SELECT * FROM work_hits"
            ") "
            "ORDER BY score DESC, title ASC "
            "LIMIT ?"));

        const QString likeNeedle = fuzzyLikePattern(normalized);
        query.bindValue(0, normalized);
        query.bindValue(1, likeNeedle);
        query.bindValue(2, likeNeedle);
        query.bindValue(3, normalized);
        query.bindValue(4, likeNeedle);
        query.bindValue(5, likeNeedle);
        query.bindValue(6, boundedLimit);
    } else if (m_schemaMode == SchemaMode::LegacyBooks) {
        query.prepare(QStringLiteral(
            "SELECT "
            "  'book' AS kind,"
            "  0 AS entity_id,"
            "  title AS title,"
            "  author AS author,"
            "  COALESCE(series, '') AS series,"
            "  0 AS item_count,"
            "  CASE "
            "    WHEN title_key = ? THEN 180 "
            "    WHEN title_key LIKE ? THEN 120 "
            "    ELSE 0 "
            "  END AS score "
            "FROM books "
            "WHERE title_key LIKE ? "
            "ORDER BY score DESC, title ASC "
            "LIMIT ?"));
        const QString likeNeedle = fuzzyLikePattern(normalized);
        query.bindValue(0, normalized);
        query.bindValue(1, likeNeedle);
        query.bindValue(2, likeNeedle);
        query.bindValue(3, boundedLimit);
    } else {
        return out;
    }

    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex search query failed for")
            << queryText
            << QLatin1Char(':')
            << query.lastError().text();
        return out;
    }

    while (query.next()) {
        const QString kind = query.value(QStringLiteral("kind")).toString();
        const QString title = query.value(QStringLiteral("title")).toString();
        const QString author = query.value(QStringLiteral("author")).toString();
        const QString series = query.value(QStringLiteral("series")).toString();
        const int itemCount = query.value(QStringLiteral("item_count")).toInt();
        const int entityId = query.value(QStringLiteral("entity_id")).toInt();
        QVariantMap row{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("title"), title},
            {QStringLiteral("author"), author},
            {QStringLiteral("series"), series},
            {QStringLiteral("count"), itemCount},
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("score"), query.value(QStringLiteral("score")).toInt()},
        };
        if (kind == QStringLiteral("series"))
            row.insert(QStringLiteral("seriesId"), entityId);
        else if (kind == QStringLiteral("book"))
            row.insert(QStringLiteral("workId"), entityId);
        if (kind == QStringLiteral("series")) {
            row.insert(QStringLiteral("genreLine"),
                       author.isEmpty()
                           ? QStringLiteral("SERIES")
                           : QStringLiteral("SERIES  \u00b7  %1").arg(author));
        } else {
            row.insert(QStringLiteral("genreLine"), author);
        }
        out.push_back(row);
    }

    return out;
}

QVariantList SeriesIndex::topBooks(int limit) const
{
    QVariantList out;
    if (!isReady())
        return out;

    const int boundedLimit = qBound(1, limit <= 0 ? 10 : limit, 100);
    QSqlQuery query(m_db);
    if (m_schemaMode == SchemaMode::CanonicalGraph) {
        query.prepare(QStringLiteral(
            "SELECT "
            "  w.id AS work_id,"
            "  w.canonical_title AS title,"
            "  w.canonical_author AS author,"
            "  COALESCE(s.canonical_title, '') AS series,"
            "  COALESCE(sm.display_position, '') AS position,"
            "  '' AS cover,"
            "  ("
            "    SELECT COUNT(*) FROM download_candidate dc "
            "    WHERE dc.work_id = w.id "
            "      AND dc.source = 'libgen' "
            "      AND COALESCE(dc.file_hash, '') <> ''"
            "  ) AS libgen_ready_count,"
            "  COALESCE(("
            "    SELECT MAX(dc.confidence) FROM download_candidate dc "
            "    WHERE dc.work_id = w.id"
            "  ), 0.0) AS best_download_confidence,"
            "  COALESCE(sm.sort_order, 999999.0) AS series_sort_order,"
            "  w.confidence AS work_confidence "
            "FROM work w "
            "LEFT JOIN series_membership sm ON sm.id = ("
            "  SELECT sm2.id FROM series_membership sm2 "
            "  WHERE sm2.work_id = w.id "
            "  ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
            "  LIMIT 1"
            ") "
            "LEFT JOIN series s ON s.id = sm.series_id "
            "ORDER BY "
            "  libgen_ready_count DESC, "
            "  best_download_confidence DESC, "
            "  work_confidence DESC, "
            "  series_sort_order ASC, "
            "  w.id ASC "
            "LIMIT ?"));
        query.bindValue(0, boundedLimit);
    } else if (m_schemaMode == SchemaMode::LegacyBooks) {
        query.prepare(QStringLiteral(
            "SELECT "
            "  0 AS work_id,"
            "  title AS title,"
            "  author AS author,"
            "  COALESCE(series, '') AS series,"
            "  COALESCE(position, '') AS position,"
            "  COALESCE(cover, '') AS cover "
            "FROM books "
            "WHERE is_boxset = 0 "
            "ORDER BY rating DESC, title ASC "
            "LIMIT ?"));
        query.bindValue(0, boundedLimit);
    } else {
        return out;
    }

    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex topBooks query failed:")
            << query.lastError().text();
        return out;
    }

    while (query.next()) {
        out.push_back(topBookMap(query.value(QStringLiteral("work_id")).toInt(),
                                 query.value(QStringLiteral("title")).toString(),
                                 query.value(QStringLiteral("author")).toString(),
                                 query.value(QStringLiteral("series")).toString(),
                                 query.value(QStringLiteral("position")).toString(),
                                 query.value(QStringLiteral("cover")).toString()));
    }

    return out;
}

QVariantList SeriesIndex::topSeries(int limit) const
{
    QVariantList out;
    if (!isReady() || m_schemaMode != SchemaMode::CanonicalGraph)
        return out;  // series discovery is a canonical-graph surface; legacy DBs hide the shelf.

    const int boundedLimit = qBound(1, limit <= 0 ? 12 : limit, 60);

    // Rank series by their flagship book's Goodreads readership (the most-read member), among series
    // that have at least two charting members AND at least one downloadable (libgen md5) book — so the
    // shelf is real multi-book series a reader can actually start. Over-fetch, then collapse the graph's
    // alias-duplicate series (e.g. "Twilight" / "The Twilight Saga") which share one author + flagship.
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT "
        "  s.id AS series_id,"
        "  s.canonical_title AS series,"
        "  COUNT(DISTINCT sm.work_id) AS member_count,"
        "  MAX(CAST(COALESCE(sa.value, '0') AS INTEGER)) AS flagship_ratings,"
        "  SUM(CASE WHEN EXISTS("
        "    SELECT 1 FROM download_candidate dc "
        "    WHERE dc.work_id = w.id AND dc.source = 'libgen' AND COALESCE(dc.file_hash, '') <> ''"
        "  ) THEN 1 ELSE 0 END) AS ready_count,"
        "  (SELECT w2.canonical_author FROM series_membership sm2 "
        "     JOIN work w2 ON w2.id = sm2.work_id "
        "     WHERE sm2.series_id = s.id "
        "     ORDER BY COALESCE(sm2.sort_order, 999999.0), w2.id LIMIT 1) AS author "
        "FROM series s "
        "JOIN series_membership sm ON sm.series_id = s.id "
        "JOIN work w ON w.id = sm.work_id "
        "LEFT JOIN source_assertion sa "
        "  ON sa.subject_id = w.id AND sa.subject_type = 'work' AND sa.predicate = 'ratings_count' "
        "GROUP BY s.id, s.canonical_title "
        "HAVING member_count >= 2 AND ready_count > 0 "
        "ORDER BY flagship_ratings DESC, member_count DESC, series ASC "
        "LIMIT ?"));
    query.bindValue(0, boundedLimit * 4 + 8);  // headroom so dedup can't starve the shelf

    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex topSeries query failed:")
            << query.lastError().text();
        return out;
    }

    QSet<QString> seenFlagship;  // (author | flagship_ratings) — collapses alias-duplicate series
    while (query.next() && out.size() < boundedLimit) {
        const int seriesId = query.value(QStringLiteral("series_id")).toInt();
        const QString series = query.value(QStringLiteral("series")).toString();
        const QString author = query.value(QStringLiteral("author")).toString();
        const int memberCount = query.value(QStringLiteral("member_count")).toInt();
        const QString flagship = query.value(QStringLiteral("flagship_ratings")).toString();

        const QString dedupKey = author.toCaseFolded() + QLatin1Char('|') + flagship;
        if (flagship != QStringLiteral("0") && seenFlagship.contains(dedupKey))
            continue;  // keep the first (highest member_count) of a colliding alias group
        seenFlagship.insert(dedupKey);

        out.push_back(seriesTileMap(seriesId, series, author, memberCount));
    }

    return out;
}

QVariantMap SeriesIndex::bookDetail(const QString &title, const QString &author) const
{
    const QVariantMap hit = lookup(title, author);
    const int workId = hit.value(QStringLiteral("workId")).toInt();
    if (workId <= 0)
        return {};

    QVariantMap detail = bookDetailById(workId);
    if (!detail.isEmpty())
        return detail;

    // Legacy DBs and partial canonical rows still get a minimal local detail object.
    QVariantMap fallback{
        {QStringLiteral("id"), workId},
        {QStringLiteral("workId"), workId},
        {QStringLiteral("title"), hit.value(QStringLiteral("displayTitle")).toString().isEmpty()
                                 ? title
                                 : hit.value(QStringLiteral("displayTitle")).toString()},
        {QStringLiteral("author"), author},
        {QStringLiteral("series"), hit.value(QStringLiteral("series")).toString()},
        {QStringLiteral("seriesPosition"), hit.value(QStringLiteral("position")).toString()},
        {QStringLiteral("genreLine"),
         hit.value(QStringLiteral("series")).toString().isEmpty()
             ? QStringLiteral("BOOK  \u00b7  %1").arg(author)
             : QStringLiteral("SERIES  \u00b7  %1").arg(author)},
        {QStringLiteral("tagline"), QString()},
        {QStringLiteral("synopsis"), QStringLiteral("Canonical metadata loaded from the local Biblio index.")},
        {QStringLiteral("cover"), QString()},
        {QStringLiteral("downloadCandidates"), QVariantList{}},
        {QStringLiteral("canonical"), true},
    };
    return fallback;
}

QVariantMap SeriesIndex::bookDetailById(int workId) const
{
    if (!isReady() || workId <= 0 || m_schemaMode != SchemaMode::CanonicalGraph)
        return {};

    QSqlQuery workQuery(m_db);
    workQuery.prepare(QStringLiteral(
        "SELECT "
        "  w.id AS work_id,"
        "  w.canonical_title AS title,"
        "  w.canonical_author AS author,"
        "  COALESCE(s.id, 0) AS series_id,"
        "  COALESCE(s.canonical_title, '') AS series,"
        "  COALESCE(sm.display_position, '') AS position,"
        "  COALESCE(("
        "    SELECT e.publication_date FROM edition e "
        "    WHERE e.work_id = w.id "
        "    ORDER BY COALESCE(e.confidence, 0.0) DESC, e.id "
        "    LIMIT 1"
        "  ), '') AS publication_date "
        "FROM work w "
        "LEFT JOIN series_membership sm ON sm.id = ("
        "  SELECT sm2.id FROM series_membership sm2 "
        "  WHERE sm2.work_id = w.id "
        "  ORDER BY COALESCE(sm2.confidence, 0.0) DESC, COALESCE(sm2.sort_order, 999999.0), sm2.id "
        "  LIMIT 1"
        ") "
        "LEFT JOIN series s ON s.id = sm.series_id "
        "WHERE w.id = ?"));
    workQuery.bindValue(0, workId);
    if (!workQuery.exec() || !workQuery.next()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex bookDetailById query failed for")
            << workId
            << QLatin1Char(':')
            << workQuery.lastError().text();
        return {};
    }

    const QString title = workQuery.value(QStringLiteral("title")).toString();
    const QString author = workQuery.value(QStringLiteral("author")).toString();
    const QString series = workQuery.value(QStringLiteral("series")).toString();
    const QString position = workQuery.value(QStringLiteral("position")).toString();
    const QString publicationDate = workQuery.value(QStringLiteral("publication_date")).toString();
    QString year;
    if (!publicationDate.isEmpty()) {
        const QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral("(\\d{4})")).match(publicationDate);
        if (match.hasMatch())
            year = match.captured(1);
    }

    // Canonical genre — the Goodreads-shelf tag the graph was enriched with (source='goodreads_shelves').
    // 'genre' is the one label the detail page leads with; 'genres' is the short chip list.
    QString genre;
    QString genresCsv;
    {
        QSqlQuery genreQuery(m_db);
        genreQuery.prepare(QStringLiteral(
            "SELECT predicate, value FROM source_assertion "
            "WHERE subject_id = ? AND source = 'goodreads_shelves' AND predicate IN ('genre','genres')"));
        genreQuery.bindValue(0, workId);
        if (genreQuery.exec()) {
            while (genreQuery.next()) {
                if (genreQuery.value(0).toString() == QStringLiteral("genre"))
                    genre = genreQuery.value(1).toString();
                else
                    genresCsv = genreQuery.value(1).toString();
            }
        }
    }

    QVariantList downloadCandidates;
    QSqlQuery dlQuery(m_db);
    dlQuery.prepare(QStringLiteral(
        "SELECT dc.source, dc.file_hash, dc.file_format, dc.title, dc.author, dc.confidence, "
        "       dc.visible_only, COALESCE(e.publication_date, '') AS publication_date, "
        "       COALESCE(e.language, '') AS language "
        "FROM download_candidate dc "
        "LEFT JOIN edition e ON e.id = dc.edition_id "
        "WHERE dc.work_id = ? "
        "ORDER BY COALESCE(dc.confidence, 0.0) DESC, dc.id"));
    dlQuery.bindValue(0, workId);
    if (!dlQuery.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex bookDetailById downloads query failed for")
            << workId
            << QLatin1Char(':')
            << dlQuery.lastError().text();
    } else {
        while (dlQuery.next()) {
            const QString source = dlQuery.value(0).toString();
            const QString fileHash = dlQuery.value(1).toString();
            const QString fileFormat = dlQuery.value(2).toString();
            const QString candidateYear = dlQuery.value(7).toString();
            const bool visibleOnly = dlQuery.value(6).toInt() != 0;

            QVariantMap candidate{
                {QStringLiteral("source"), source},
                {QStringLiteral("md5"), fileHash},
                {QStringLiteral("format"), fileFormat},
                {QStringLiteral("title"), dlQuery.value(3).toString()},
                {QStringLiteral("author"), dlQuery.value(4).toString()},
                {QStringLiteral("confidence"), dlQuery.value(5).toDouble()},
                {QStringLiteral("visibleOnly"), visibleOnly},
                {QStringLiteral("year"), candidateYear},
                {QStringLiteral("language"), dlQuery.value(8).toString()},
                {QStringLiteral("best"), source == QStringLiteral("libgen") && !fileHash.isEmpty()},
            };

            // BiblioBook's native downloader can only act on LibGen md5-backed rows.
            if (fileHash.isEmpty())
                candidate.remove(QStringLiteral("md5"));
            downloadCandidates.push_back(candidate);
        }
    }

    QVariantMap detail{
        {QStringLiteral("id"), workId},
        {QStringLiteral("workId"), workId},
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("year"), year},
        {QStringLiteral("cover"), QString()},
        {QStringLiteral("tagline"), QString()},
        {QStringLiteral("synopsis"),
         series.isEmpty()
             ? QStringLiteral("Canonical metadata loaded from the local Biblio graph.")
             : QStringLiteral("%1 is part of %2%3.")
                   .arg(title,
                        series,
                        position.isEmpty() ? QString()
                                           : QStringLiteral(" (book %1)").arg(position))},
        {QStringLiteral("genre"), genre},
        {QStringLiteral("genres"), genresCsv},
        {QStringLiteral("genreLine"),
         !genre.isEmpty()
             ? QStringLiteral("%1  \u00b7  %2%3")
                   .arg(genre, author, year.isEmpty() ? QString() : QStringLiteral("  \u00b7  %1").arg(year))
             : series.isEmpty()
                   ? QStringLiteral("BOOK  \u00b7  %1%2")
                         .arg(author, year.isEmpty() ? QString() : QStringLiteral("  \u00b7  %1").arg(year))
                   : QStringLiteral("SERIES  \u00b7  %1%2")
                         .arg(author, year.isEmpty() ? QString() : QStringLiteral("  \u00b7  %1").arg(year))},
        {QStringLiteral("series"), series},
        {QStringLiteral("seriesPosition"), position},
        {QStringLiteral("downloadCandidates"), downloadCandidates},
        {QStringLiteral("canonical"), true},
    };
    return detail;
}

void SeriesIndex::selfTest() const
{
    const auto logLookup = [this](const QString &title, const QString &author) {
        const QVariantMap hit = lookup(title, author);
        qInfo().noquote()
            << QStringLiteral("SeriesIndex smoke lookup(%1, %2) -> found=%3 series=%4 position=%5 allSeries=%6 isBoxset=%7 rating=%8 displayTitle=%9")
                   .arg(title,
                        author,
                        hit.value(QStringLiteral("found")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
                        hit.value(QStringLiteral("series")).toString(),
                        hit.value(QStringLiteral("position")).toString(),
                        hit.value(QStringLiteral("allSeries")).toString(),
                        hit.value(QStringLiteral("isBoxset")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
                        hit.value(QStringLiteral("rating")).toString(),
                        hit.value(QStringLiteral("displayTitle")).toString());
    };

    logLookup(QStringLiteral("Catching Fire"), QStringLiteral("Suzanne Collins"));
    logLookup(QStringLiteral("Throne of Glass"), QStringLiteral("Sarah J. Maas"));
    logLookup(QStringLiteral("The Fault in Our Stars"), QStringLiteral("John Green"));

    const QVariantList rows = seriesEntries(QStringLiteral("Harry Potter"));
    QStringList summary;
    summary.reserve(rows.size());
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        summary.push_back(QStringLiteral("%1:%2")
                              .arg(row.value(QStringLiteral("position")).toString(),
                                   row.value(QStringLiteral("title")).toString()));
    }
    qInfo().noquote()
        << QStringLiteral("SeriesIndex smoke seriesEntries(Harry Potter) -> count=%1 %2")
               .arg(rows.size())
               .arg(summary.join(QStringLiteral(" | ")));
}
