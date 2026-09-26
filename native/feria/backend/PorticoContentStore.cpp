#include "PorticoContentStore.h"

#include <QChar>
#include <QRegularExpression>
#include <algorithm>

PorticoContentStore::PorticoContentStore(QObject *parent)
    : QObject(parent)
{
}

QVariantMap PorticoContentStore::toPorticoTitle(const QVariantMap &item)
{
    QVariantMap out;
    const QString key = item.value(QStringLiteral("canonicalKey")).toString();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    const int year = item.value(QStringLiteral("year")).toInt();
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();

    out.insert(QStringLiteral("id"), key);
    out.insert(QStringLiteral("k"), kind);
    out.insert(QStringLiteral("t"), item.value(QStringLiteral("title")).toString());
    out.insert(QStringLiteral("by"), item.value(QStringLiteral("creator")).toString());
    out.insert(QStringLiteral("y"), year > 0 ? QString::number(year) : QString());
    out.insert(QStringLiteral("f"), item.value(QStringLiteral("genres")).toStringList());
    out.insert(QStringLiteral("s"), extra.value(QStringLiteral("description")).toString());
    out.insert(QStringLiteral("imageUrl"), item.value(QStringLiteral("imageUrl")).toString());
    out.insert(QStringLiteral("canonicalUrl"), item.value(QStringLiteral("canonicalUrl")).toString());
    out.insert(QStringLiteral("canonicalKey"), key);
    out.insert(QStringLiteral("sourceId"), item.value(QStringLiteral("sourceId")).toString());
    out.insert(QStringLiteral("sourceIds"), item.value(QStringLiteral("sourceIds")).toStringList());
    out.insert(QStringLiteral("externalIds"), item.value(QStringLiteral("externalIds")).toMap());
    out.insert(QStringLiteral("rank"), item.value(QStringLiteral("rank")).toInt());
    out.insert(QStringLiteral("_trend"), item);
    return out;
}
void PorticoContentStore::ingestShelf(const QString &, const QVariantMap &shelf)
{
    bool changed = false;
    for (const auto &value : shelf.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        const QString key = item.value(QStringLiteral("canonicalKey")).toString();
        if (key.isEmpty())
            continue;

        QVariantMap title = toPorticoTitle(item);
        const auto existing = m_titles.constFind(key);
        if (existing != m_titles.cend()) {
            if (title.value(QStringLiteral("imageUrl")).toString().isEmpty())
                title.insert(QStringLiteral("imageUrl"),
                             existing->value(QStringLiteral("imageUrl")));
            if (title.value(QStringLiteral("s")).toString().isEmpty())
                title.insert(QStringLiteral("s"), existing->value(QStringLiteral("s")));
        }
        if (existing == m_titles.cend() || existing.value() != title) {
            m_titles.insert(key, title);
            changed = true;
        }
    }
    if (changed)
        bumpRevision();
}

QVariantMap PorticoContentStore::title(const QString &canonicalKey) const
{
    return m_titles.value(canonicalKey);
}

bool PorticoContentStore::contains(const QString &canonicalKey) const
{
    return m_titles.contains(canonicalKey);
}

QStringList PorticoContentStore::ids() const
{
    return m_titles.keys();
}
QString PorticoContentStore::normalizedSearchText(QString value)
{
    value = value.normalized(QString::NormalizationForm_D).toCaseFolded();
    QString out;
    out.reserve(value.size());
    bool spaced = false;
    for (const QChar ch : value) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue;
        if (ch.isLetterOrNumber()) {
            if (spaced && !out.isEmpty())
                out += QLatin1Char(' ');
            out += ch;
            spaced = false;
        } else {
            spaced = true;
        }
    }
    return out.trimmed();
}

QStringList PorticoContentStore::search(const QString &query, int limit) const
{
    const QString needle = normalizedSearchText(query);
    if (needle.isEmpty())
        return {};
    const QStringList words = needle.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    struct Hit { QString id; int score = 0; QString title; };
    QList<Hit> hits;
    for (auto it = m_titles.cbegin(); it != m_titles.cend(); ++it) {
        const QVariantMap value = it.value();
        const QString title = normalizedSearchText(value.value(QStringLiteral("t")).toString());
        const QString creator = normalizedSearchText(value.value(QStringLiteral("by")).toString());
        const QString haystack = title + QLatin1Char(' ') + creator
            + QLatin1Char(' ') + normalizedSearchText(kindLabel(value.value(QStringLiteral("k")).toString()));
        bool matches = true;
        for (const QString &word : words)
            matches = matches && haystack.contains(word);
        if (!matches)
            continue;
        int score = title == needle ? 3 : (title.startsWith(needle) ? 2 : 1);
        hits.push_back({it.key(), score, title});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.title < b.title;
    });

    QStringList result;
    const int count = qMin(qMax(0, limit), hits.size());
    for (int i = 0; i < count; ++i)
        result.push_back(hits.at(i).id);
    return result;
}

QString PorticoContentStore::providerLabel(const QString &providerId) const
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("openlibrary"), QStringLiteral("Open Library")},
        {QStringLiteral("anilist"), QStringLiteral("AniList")},
        {QStringLiteral("globalcomix"), QStringLiteral("GlobalComix")},
        {QStringLiteral("stremio"), QStringLiteral("Stremio")},
        {QStringLiteral("applemusic"), QStringLiteral("Apple Music")},
        {QStringLiteral("youtube"), QStringLiteral("YouTube")},
        {QStringLiteral("ytmusic"), QStringLiteral("YouTube Music")},
        {QStringLiteral("spotify"), QStringLiteral("Spotify")},
        {QStringLiteral("webtoon"), QStringLiteral("WEBTOON")}
    };
    return labels.value(providerId, providerId);
}

QString PorticoContentStore::kindLabel(const QString &kind) const
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("film"), QStringLiteral("Film")},
        {QStringLiteral("series"), QStringLiteral("Series")},
        {QStringLiteral("anime"), QStringLiteral("Anime series")},
        {QStringLiteral("song"), QStringLiteral("Song")},
        {QStringLiteral("music-video"), QStringLiteral("Music video")},
        {QStringLiteral("album"), QStringLiteral("Album")},
        {QStringLiteral("artist"), QStringLiteral("Artist")},
        {QStringLiteral("book"), QStringLiteral("Book")},
        {QStringLiteral("manga"), QStringLiteral("Manga")},
        {QStringLiteral("comic"), QStringLiteral("Comic")},
        {QStringLiteral("webtoon"), QStringLiteral("Webcomic")}
    };
    return labels.value(kind, kind);
}

QString PorticoContentStore::verbForKind(const QString &kind) const
{
    if (kind == QStringLiteral("film") || kind == QStringLiteral("series")
        || kind == QStringLiteral("anime"))
        return QStringLiteral("watch");
    if (kind == QStringLiteral("song") || kind == QStringLiteral("music-video")
        || kind == QStringLiteral("album") || kind == QStringLiteral("artist"))
        return QStringLiteral("listen");
    return QStringLiteral("read");
}

QString PorticoContentStore::shapeForKind(const QString &kind) const
{
    if (kind == QStringLiteral("album") || kind == QStringLiteral("song")
        || kind == QStringLiteral("music-video"))
        return QStringLiteral("square");
    if (kind == QStringLiteral("artist"))
        return QStringLiteral("circle");
    return QStringLiteral("poster");
}

void PorticoContentStore::bumpRevision()
{
    ++m_revision;
    emit revisionChanged();
}
