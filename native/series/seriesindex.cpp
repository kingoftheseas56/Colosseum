#include "seriesindex.h"

#include <QDir>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace {
struct SeriesRow {
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
    return title.toLower().simplified();
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
    row.title = query.value(0).toString();
    row.author = query.value(1).toString();
    row.series = query.value(2).toString();
    row.position = query.value(3).toString();
    row.allSeries = query.value(4).toString();
    row.isBoxset = query.value(5).toInt() != 0;
    row.rating = query.value(6).toDouble();
    return row;
}

QVariantMap foundMap(const SeriesRow &row)
{
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("series"), row.series},
        {QStringLiteral("position"), row.position},
        {QStringLiteral("allSeries"), row.allSeries},
        {QStringLiteral("isBoxset"), row.isBoxset},
        {QStringLiteral("rating"), row.rating},
        {QStringLiteral("displayTitle"), row.title},
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

QVariantMap SeriesIndex::lookup(const QString &title, const QString &author) const
{
    if (!isReady())
        return {{QStringLiteral("found"), false}};

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT title,author,series,position,all_series,is_boxset,rating "
        "FROM books WHERE title_key=?"));

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
    query.prepare(QStringLiteral(
        "SELECT title,position,rating,year,cover FROM books "
        "WHERE series=? AND is_boxset=0 "
        "ORDER BY CAST(position AS REAL)"));
    query.bindValue(0, series);
    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("SeriesIndex seriesEntries query failed for")
            << series
            << QLatin1Char(':')
            << query.lastError().text();
        return out;
    }

    while (query.next()) {
        out.push_back(QVariantMap{
            {QStringLiteral("position"), query.value(1).toString()},
            {QStringLiteral("title"), query.value(0).toString()},
            {QStringLiteral("rating"), query.value(2).toDouble()},
            {QStringLiteral("year"), query.value(3).toInt()},
            {QStringLiteral("cover"), query.value(4).toString()},
        });
    }
    return out;
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
