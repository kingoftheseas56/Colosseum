#include "PorticoTrendBroker.h"

#include "PorticoCanonicalizer.h"
#include "PorticoTrendAdapter.h"
#include "PorticoTrendAdapters.h"

#include <QtGlobal>
#include <algorithm>

PorticoTrendBroker::PorticoTrendBroker(QObject *parent)
    : QObject(parent)
{
    configureFeed(QStringLiteral("openlibrary"), QStringLiteral("openlibrary"));
    configureFeed(QStringLiteral("anilist"), QStringLiteral("anilist"));
    configureFeed(QStringLiteral("youtube:songs"), QStringLiteral("youtube"),
                  {{QStringLiteral("category"), QStringLiteral("songs")},
                   {QStringLiteral("title"), QStringLiteral("Top songs this week")},
                   {QStringLiteral("ranked"), true}});
    configureFeed(QStringLiteral("youtube:artists"), QStringLiteral("youtube"),
                  {{QStringLiteral("category"), QStringLiteral("artists")},
                   {QStringLiteral("title"), QStringLiteral("Top artists this week")},
                   {QStringLiteral("ranked"), true}});
    configureFeed(QStringLiteral("webtoon"), QStringLiteral("webtoon"));
    configureFeed(QStringLiteral("globalcomix"), QStringLiteral("globalcomix"));
}
PorticoTrendAdapter *PorticoTrendBroker::createAdapter(const QString &sourceId)
{
    if (sourceId == QStringLiteral("stremio"))
        return new StremioCatalogAdapter(&m_network, this);
    if (sourceId == QStringLiteral("openlibrary"))
        return new OpenLibraryTrendAdapter(&m_network, this);
    if (sourceId == QStringLiteral("anilist"))
        return new AniListTrendAdapter(&m_network, this);
    if (sourceId == QStringLiteral("applemusic"))
        return new AppleMusicChartsAdapter(&m_network, this);
    if (sourceId == QStringLiteral("youtube"))
        return new YouTubeChartsAdapter(&m_network, this);
    if (sourceId == QStringLiteral("webtoon"))
        return new WebtoonTrendAdapter(&m_network, this);
    if (sourceId == QStringLiteral("globalcomix"))
        return new GlobalComixTrendAdapter(&m_network, this);
    if (sourceId == QStringLiteral("spotify"))
        return new SpotifyChartsAdapter(&m_network, this);
    return nullptr;
}

void PorticoTrendBroker::configureFeed(const QString &feedId,
                                       const QString &sourceId,
                                       const QVariantMap &config)
{
    if (feedId.trimmed().isEmpty() || sourceId.trimmed().isEmpty())
        return;

    auto existing = m_feeds.find(feedId);
    if (existing != m_feeds.end() && existing->adapter)
        existing->adapter->deleteLater();

    auto *source = createAdapter(sourceId);
    if (!source)
        return;

    FeedRuntime runtime;
    runtime.feedId = feedId;
    runtime.sourceId = sourceId;
    runtime.config = config;
    runtime.config.insert(QStringLiteral("feedId"), feedId);
    runtime.adapter = source;

    connect(source, &PorticoTrendAdapter::succeeded, this,
            [this, feedId, sourceId](const QString &, const PorticoTrend::Shelf &shelf) {
        const auto *runtime = feed(feedId);
        if (!runtime)
            return;
        auto normalized = PorticoCanonicalizer::decorate(shelf);
        normalized.id = feedId;
        normalized.sourceId = sourceId;
        normalized.title = runtime->config.value(QStringLiteral("title"),
                                                  normalized.title).toString();
        normalized.sourceLabel = runtime->config.value(QStringLiteral("sourceLabel"),
                                                        normalized.sourceLabel).toString();
        normalized.providerId = runtime->config.value(QStringLiteral("providerId")).toString();
        normalized.ranked = runtime->config.value(QStringLiteral("ranked"),
                                                   normalized.ranked).toBool();
        normalized.priority = runtime->config.value(QStringLiteral("priority"), 0).toInt();
        normalized.stale = false;
        normalized.fallbackReason.clear();

        QString cacheError;
        m_cache.save(normalized, &cacheError);
        m_model.setShelf(normalized);
        emit sourceReady(feedId, normalized.toVariantMap());
    });

    connect(source, &PorticoTrendAdapter::failed, this,
            [this, feedId](const QString &, const QString &message) {
        const auto cached = m_cache.load(feedId, m_cacheMaxAgeSeconds, true);
        if (cached.found) {
            auto fallback = cached.shelf;
            fallback.stale = true;
            fallback.fallbackReason = message;
            m_model.setShelf(fallback);
            m_model.setError(feedId, message);
        } else {
            m_model.setError(feedId, message);
        }
        emit sourceFailed(feedId, message);
    });

    m_feeds.insert(feedId, runtime);
    emit feedsChanged();
}
void PorticoTrendBroker::configureSource(const QString &sourceId,
                                          const QVariantMap &config)
{
    configureFeed(sourceId, sourceId, config);
}

PorticoTrendBroker::FeedRuntime *PorticoTrendBroker::feed(const QString &feedId)
{
    auto it = m_feeds.find(feedId);
    return it == m_feeds.end() ? nullptr : &it.value();
}

const PorticoTrendBroker::FeedRuntime *PorticoTrendBroker::feed(
    const QString &feedId) const
{
    auto it = m_feeds.constFind(feedId);
    return it == m_feeds.cend() ? nullptr : &it.value();
}

QStringList PorticoTrendBroker::sources() const
{
    return {
        QStringLiteral("stremio"),
        QStringLiteral("openlibrary"),
        QStringLiteral("anilist"),
        QStringLiteral("applemusic"),
        QStringLiteral("youtube"),
        QStringLiteral("webtoon"),
        QStringLiteral("globalcomix"),
        QStringLiteral("spotify")
    };
}

QStringList PorticoTrendBroker::feeds() const
{
    auto result = m_feeds.keys();
    std::sort(result.begin(), result.end());
    return result;
}

void PorticoTrendBroker::setRegion(const QString &region)
{
    const QString next = region.trimmed().toUpper();
    if (next.isEmpty() || next == m_region)
        return;
    m_region = next;
    emit regionChanged();
}

void PorticoTrendBroker::setLanguage(const QString &language)
{
    const QString next = language.trimmed();
    if (next.isEmpty() || next == m_language)
        return;
    m_language = next;
    emit languageChanged();
}

void PorticoTrendBroker::setLimit(int limit)
{
    const int next = qBound(1, limit, 200);
    if (next == m_limit)
        return;
    m_limit = next;
    emit limitChanged();
}
void PorticoTrendBroker::setCacheMaxAgeSeconds(qint64 seconds)
{
    const qint64 next = qMax<qint64>(0, seconds);
    if (next == m_cacheMaxAgeSeconds)
        return;
    m_cacheMaxAgeSeconds = next;
    emit cacheMaxAgeSecondsChanged();
}

PorticoTrend::Query PorticoTrendBroker::queryFor(const FeedRuntime &runtime) const
{
    PorticoTrend::Query query;
    query.region = m_region;
    query.language = m_language;
    query.limit = m_limit;
    query.category = runtime.config.value(QStringLiteral("category")).toString();
    return query;
}

void PorticoTrendBroker::refreshFeed(const QString &feedId)
{
    auto *runtime = feed(feedId);
    if (!runtime || !runtime->adapter) {
        emit sourceFailed(feedId, QStringLiteral("Unknown trend feed."));
        return;
    }

    const auto cached = m_cache.load(feedId, m_cacheMaxAgeSeconds, false);
    if (cached.found)
        m_model.setShelf(cached.shelf);

    m_model.setLoading(feedId, runtime->sourceId,
                       runtime->adapter->defaultTitle(),
                       runtime->adapter->sourceLabel(),
                       runtime->adapter->medium(),
                       runtime->adapter->stability());

    if (!runtime->adapter->isConfigured(runtime->config)) {
        const QString message = QStringLiteral("%1 is not configured.")
                                    .arg(runtime->adapter->sourceLabel());
        m_model.setError(feedId, message);
        emit sourceFailed(feedId, message);
        return;
    }

    runtime->adapter->fetch(queryFor(*runtime), runtime->config);
}

void PorticoTrendBroker::refreshSource(const QString &sourceId)
{
    if (m_feeds.contains(sourceId)) {
        refreshFeed(sourceId);
        return;
    }
    for (auto it = m_feeds.cbegin(); it != m_feeds.cend(); ++it)
        if (it->sourceId == sourceId)
            refreshFeed(it.key());
}

void PorticoTrendBroker::refreshAll()
{
    const auto ids = feeds();
    for (const QString &feedId : ids)
        refreshFeed(feedId);
}
QVariantMap PorticoTrendBroker::shelf(const QString &feedId) const
{
    return m_model.shelf(feedId);
}

void PorticoTrendBroker::clearCache()
{
    m_cache.clear();
}
