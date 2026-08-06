#include "BiblioProviders.h"
#include "BiblioArtworkUrl.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrlQuery>

// Keyless Apple Books + Open Library parsing (spec 6.1). Defensive throughout:
// every accessor tolerates a missing/wrong-typed field, and a record with no
// usable title (no identity to canonicalize on) is dropped rather than crashing.

namespace {

// Strip HTML tags and decode the handful of entities Apple/Open Library emit,
// then collapse whitespace. Mirrors qml/BiblioApi.js stripHtml so the native and
// QML descriptions read identically.
QString stripHtml(const QString &in)
{
    if (in.isEmpty())
        return QString();
    static const QRegularExpression tag(QStringLiteral("<[^>]+>"));
    QString s = in;
    s.replace(tag, QStringLiteral(" "));
    // Named/numeric entities → their character. &amp; is decoded here (before the
    // generic sweep below) so it does not get swallowed as an "unknown entity".
    s.replace(QRegularExpression(QStringLiteral("&(nbsp|#xa0|#160);"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("&(mdash|#x2014|#8212);"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("—"));
    s.replace(QRegularExpression(QStringLiteral("&(ndash|#x2013|#8211);"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("–"));
    s.replace(QRegularExpression(QStringLiteral("&(quot|#34);"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\""));
    s.replace(QRegularExpression(QStringLiteral("&(apos|#39|#x27);"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("'"));
    s.replace(QRegularExpression(QStringLiteral("&amp;"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("&"));
    // Any entity still standing is noise → a space.
    s.replace(QRegularExpression(QStringLiteral("&[#a-z0-9]+;"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

// Fold a language code/name to a compact ISO-ish token the taxonomy consumes
// (BiblioTaxonomy::languageKey guarantees these are the tokens it expects).
QString normalizeLanguage(const QString &raw)
{
    const QString l = raw.simplified().toLower();
    if (l == QLatin1String("en") || l == QLatin1String("eng") || l == QLatin1String("english")) return QStringLiteral("en");
    if (l == QLatin1String("fr") || l == QLatin1String("fre") || l == QLatin1String("fra") || l == QLatin1String("french")) return QStringLiteral("fr");
    if (l == QLatin1String("ja") || l == QLatin1String("jpn") || l == QLatin1String("japanese")) return QStringLiteral("ja");
    if (l == QLatin1String("es") || l == QLatin1String("spa") || l == QLatin1String("spanish")) return QStringLiteral("es");
    if (l == QLatin1String("de") || l == QLatin1String("ger") || l == QLatin1String("deu") || l == QLatin1String("german")) return QStringLiteral("de");
    if (l == QLatin1String("it") || l == QLatin1String("ita") || l == QLatin1String("italian")) return QStringLiteral("it");
    return l; // unknown — pass through folded; never invented
}

QString labelField(const QJsonObject &entry, const QString &key)
{
    return entry.value(key).toObject().value(QStringLiteral("label")).toString();
}

QDate parseAppleDate(const QString &raw)
{
    if (raw.isEmpty())
        return QDate();
    const QDateTime dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid())
        return dt.date();
    return QDate::fromString(raw.left(10), QStringLiteral("yyyy-MM-dd"));
}

QDateTime stampedNow(const QDateTime &observedAt)
{
    return observedAt.isValid() ? observedAt : QDateTime::currentDateTimeUtc();
}

} // namespace

namespace BiblioProviders {

// Fold a title/author for the title+author identity layer: lowercase, drop
// parentheticals ("(Unabridged)"/"(Abridged)"), '&' -> " and ", strip other
// punctuation, collapse. Mirrors qml/BiblioApi.js pairKey so an ebook and its
// "(Unabridged)" audiobook fold to the same identity. Single source of truth —
// the canonicalizer calls this same fold for hand-built records.
QString foldTitleAuthor(const QString &raw)
{
    QString s = raw.toLower();
    s.replace(QRegularExpression(QStringLiteral("\\([^)]*\\)")), QStringLiteral(" "));
    s.replace(QChar('&'), QStringLiteral(" and "));
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return s.simplified();
}

QList<BiblioSourceRecord> parseAppleRss(const QByteArray &bytes, const QDateTime &observedAt)
{
    QList<BiblioSourceRecord> out;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return out;
    const QJsonValue entryVal = doc.object().value(QStringLiteral("feed")).toObject().value(QStringLiteral("entry"));
    // `entry` is a single object for a one-result feed, an array otherwise.
    QJsonArray entries;
    if (entryVal.isArray())
        entries = entryVal.toArray();
    else if (entryVal.isObject())
        entries.append(entryVal);
    else
        return out;

    const QDateTime observed = stampedNow(observedAt);
    const int count = entries.size();
    for (int i = 0; i < count; ++i) {
        const QJsonObject entry = entries.at(i).toObject();
        const QString title = labelField(entry, QStringLiteral("im:name"));
        if (title.isEmpty())
            continue; // no identity — drop defensively

        BiblioSourceRecord r;
        r.source = QStringLiteral("apple");
        r.observedAt = observed;
        r.title = title;
        r.author = labelField(entry, QStringLiteral("im:artist"));
        r.normalizedTitle = foldTitleAuthor(r.title);
        r.normalizedAuthor = foldTitleAuthor(r.author);

        const QString adamId = entry.value(QStringLiteral("id")).toObject()
                                   .value(QStringLiteral("attributes")).toObject()
                                   .value(QStringLiteral("im:id")).toString();
        r.sourceId = adamId.isEmpty() ? (QStringLiteral("apple-rss:") + r.normalizedTitle)
                                      : (QStringLiteral("apple:") + adamId);

        // Largest artwork is the last im:image entry (small -> large). Apple's own RSS
        // feed ships these as "0x<N>bb.png" — a shape its own CDN's "bb" resize style
        // cannot actually produce (verified live, 2026-08-06: HTTP 400, "Cannot produce
        // 0x170 image with Resize Style: 'bb'") — normalize to a working size instead
        // of caching a dead link (see BiblioArtworkUrl.h).
        const QJsonArray imgs = entry.value(QStringLiteral("im:image")).toArray();
        if (!imgs.isEmpty())
            r.artworkUrl = normalizedAppleArtworkUrl(
                imgs.last().toObject().value(QStringLiteral("label")).toString());

        r.published = parseAppleDate(labelField(entry, QStringLiteral("im:releaseDate")));
        r.format = QStringLiteral("ebook"); // the top-ebooks chart
        r.language = QStringLiteral("en");
        r.englishReadable = true;
        // Chart position -> a positive, rank-monotonic score (rank 1 highest).
        r.appleChartScore = static_cast<double>(count - i);
        out.append(r);
    }
    return out;
}

QList<BiblioSourceRecord> parseAppleSearch(const QByteArray &bytes, const QDateTime &observedAt)
{
    QList<BiblioSourceRecord> out;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return out;
    const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
    const QDateTime observed = stampedNow(observedAt);

    for (const QJsonValue &v : results) {
        const QJsonObject o = v.toObject();
        const QString wrapper = o.value(QStringLiteral("wrapperType")).toString();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        const bool isAudiobook = wrapper == QLatin1String("audiobook") || kind == QLatin1String("audiobook");

        const QString name = isAudiobook
            ? (o.value(QStringLiteral("collectionName")).toString().isEmpty()
                   ? o.value(QStringLiteral("trackName")).toString()
                   : o.value(QStringLiteral("collectionName")).toString())
            : (o.value(QStringLiteral("trackName")).toString().isEmpty()
                   ? o.value(QStringLiteral("trackCensoredName")).toString()
                   : o.value(QStringLiteral("trackName")).toString());
        if (name.isEmpty())
            continue; // malformed / title-less — skip

        BiblioSourceRecord r;
        r.source = QStringLiteral("apple");
        r.observedAt = observed;
        r.title = name;
        r.author = o.value(QStringLiteral("artistName")).toString();
        r.normalizedTitle = foldTitleAuthor(r.title);
        r.normalizedAuthor = foldTitleAuthor(r.author);

        const qint64 id = isAudiobook
            ? static_cast<qint64>(o.value(QStringLiteral("collectionId")).toDouble())
            : static_cast<qint64>(o.value(QStringLiteral("trackId")).toDouble());
        r.sourceId = id != 0 ? (QStringLiteral("apple:") + QString::number(id))
                             : (QStringLiteral("apple-search:") + r.normalizedTitle);

        r.description = stripHtml(o.value(QStringLiteral("description")).toString());
        r.artworkUrl = o.value(QStringLiteral("artworkUrl100")).toString();
        if (r.artworkUrl.isEmpty())
            r.artworkUrl = o.value(QStringLiteral("artworkUrl60")).toString();
        // Search's 60/100px thumbnails are VALID but read blurry once stretched into a
        // gallery card — upgrade to a real cover size (see BiblioArtworkUrl.h).
        r.artworkUrl = normalizedAppleArtworkUrl(r.artworkUrl);
        r.published = parseAppleDate(o.value(QStringLiteral("releaseDate")).toString());

        if (o.contains(QStringLiteral("averageUserRating")) || o.contains(QStringLiteral("userRatingCount"))) {
            r.hasRating = true;
            r.rating.average = o.value(QStringLiteral("averageUserRating")).toDouble();
            r.rating.count = static_cast<int>(o.value(QStringLiteral("userRatingCount")).toDouble());
        }

        r.format = isAudiobook ? QStringLiteral("audiobook") : QStringLiteral("ebook");
        r.language = QStringLiteral("en");
        r.englishReadable = true;
        out.append(r);
    }
    return out;
}

QList<BiblioSourceRecord> parseOpenLibrarySearch(const QByteArray &bytes, const QDateTime &observedAt)
{
    QList<BiblioSourceRecord> out;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return out;
    const QJsonArray docs = doc.object().value(QStringLiteral("docs")).toArray();
    const QDateTime observed = stampedNow(observedAt);

    for (const QJsonValue &v : docs) {
        const QJsonObject o = v.toObject();
        const QString title = o.value(QStringLiteral("title")).toString();
        const QString workKey = o.value(QStringLiteral("key")).toString();
        if (title.isEmpty() && workKey.isEmpty())
            continue; // no identity at all — drop

        BiblioSourceRecord r;
        r.source = QStringLiteral("openlibrary");
        r.observedAt = observed;
        r.workKey = workKey;
        r.title = title;

        const QJsonArray authors = o.value(QStringLiteral("author_name")).toArray();
        if (!authors.isEmpty())
            r.author = authors.first().toString();
        for (const QJsonValue &ak : o.value(QStringLiteral("author_key")).toArray())
            r.authorKeys << ak.toString();

        r.normalizedTitle = foldTitleAuthor(r.title);
        r.normalizedAuthor = foldTitleAuthor(r.author);
        r.sourceId = workKey.isEmpty() ? (QStringLiteral("openlibrary:") + r.normalizedTitle + QLatin1Char('|') + r.normalizedAuthor)
                                       : workKey;

        for (const QJsonValue &isbn : o.value(QStringLiteral("isbn")).toArray()) {
            const QString s = isbn.toString();
            if (!s.isEmpty())
                r.isbns << s;
        }

        r.firstPublishYear = o.value(QStringLiteral("first_publish_year")).toInt();
        if (r.firstPublishYear > 0)
            r.published = QDate(r.firstPublishYear, 1, 1);

        // Language: normalize the first token; English-readability is true if ANY
        // listed language is English (a work may carry the original + an English
        // edition on one doc).
        const QJsonArray langs = o.value(QStringLiteral("language")).toArray();
        if (!langs.isEmpty())
            r.language = normalizeLanguage(langs.first().toString());
        for (const QJsonValue &lv : langs)
            if (normalizeLanguage(lv.toString()) == QLatin1String("en"))
                r.englishReadable = true;

        r.pageCount = o.value(QStringLiteral("number_of_pages_median")).toInt();
        const QJsonArray pubs = o.value(QStringLiteral("publisher")).toArray();
        if (!pubs.isEmpty())
            r.publisher = pubs.first().toString();

        // Cover art is the canonicalizer's Open Library fallback when Apple carries
        // no artwork (Apple owns artwork per spec 6.1). cover_i is Open Library's
        // stable cover id served from covers.openlibrary.org.
        const int coverId = o.value(QStringLiteral("cover_i")).toInt();
        if (coverId > 0)
            r.artworkUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg").arg(coverId);

        // Description may be a plain string or an Open Library {type,value} object.
        const QJsonValue desc = o.value(QStringLiteral("description"));
        if (desc.isString())
            r.description = stripHtml(desc.toString());
        else if (desc.isObject())
            r.description = stripHtml(desc.toObject().value(QStringLiteral("value")).toString());

        // Popularity: the reading-log tally is Open Library's demand signal.
        if (o.contains(QStringLiteral("readinglog_count")))
            r.openLibraryPopularity = o.value(QStringLiteral("readinglog_count")).toDouble();
        else if (o.contains(QStringLiteral("want_to_read_count")))
            r.openLibraryPopularity = o.value(QStringLiteral("want_to_read_count")).toDouble();

        r.format = QStringLiteral("print"); // Open Library governs print/edition evidence
        out.append(r);
    }
    return out;
}

QUrl appleTopEbooksRssUrl(const QString &country, int limit, int genreId)
{
    const int safeLimit = qBound(1, limit, 200);
    QString path = QStringLiteral("https://itunes.apple.com/%1/rss/topebooks/limit=%2")
                       .arg(country.isEmpty() ? QStringLiteral("us") : country)
                       .arg(safeLimit);
    if (genreId > 0)
        path += QStringLiteral("/genre=%1").arg(genreId);
    path += QStringLiteral("/json");
    return QUrl(path);
}

QUrl appleSearchUrl(const QString &term, const QString &media, int limit, const QString &country)
{
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("media"), media.isEmpty() ? QStringLiteral("ebook") : media);
    q.addQueryItem(QStringLiteral("term"), term);
    q.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, limit, 200)));
    q.addQueryItem(QStringLiteral("country"), country.isEmpty() ? QStringLiteral("us") : country);
    url.setQuery(q);
    return url;
}

QUrl openLibrarySearchUrl(const QString &title, const QString &author, int limit)
{
    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), title);
    if (!author.isEmpty())
        q.addQueryItem(QStringLiteral("author"), author);
    q.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, limit, 100)));
    url.setQuery(q);
    return url;
}

} // namespace BiblioProviders
