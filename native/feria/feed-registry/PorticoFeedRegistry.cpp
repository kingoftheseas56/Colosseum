#include "PorticoFeedRegistry.h"

#include "PorticoDiscoveryService.h"

#include <QStringList>

namespace {

constexpr int NetflixPriority = 100;
constexpr int PrimePriority = 110;
constexpr int HboMaxPriority = 120;
constexpr int DisneyPriority = 130;
constexpr int AppleTvPriority = 140;
constexpr int SpotifyPriority = 200;
constexpr int YouTubeSongsPriority = 210;
constexpr int YouTubeArtistsPriority = 211;
constexpr int AppleMusicPriority = 220;
constexpr int OpenLibraryPriority = 300;
constexpr int AniListPriority = 310;
constexpr int WebtoonPriority = 320;
constexpr int GlobalComixPriority = 330;

PorticoFeedDefinition stremioFeed(
    const QString &feedId,
    const QString &catalogId,
    const QString &providerId,
    const QString &title,
    int priority)
{
    return {
        feedId,
        QStringLiteral("stremio"),
        {
            {QStringLiteral("baseUrl"), QStringLiteral("https://catalog.ers.pw")},
            {QStringLiteral("types"), QStringList{QStringLiteral("movie"), QStringLiteral("series")}},
            {QStringLiteral("catalogId"), catalogId},
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("title"), title},
            {QStringLiteral("sourceLabel"), QStringLiteral("Streaming Catalogs")},
            {QStringLiteral("ranked"), false},
            {QStringLiteral("priority"), priority},
        },
    };
}

PorticoFeedDefinition feed(
    const QString &feedId,
    const QString &sourceId,
    const QString &title,
    const QString &sourceLabel,
    bool ranked,
    int priority,
    const QString &providerId = {})
{
    QVariantMap config{
        {QStringLiteral("title"), title},
        {QStringLiteral("sourceLabel"), sourceLabel},
        {QStringLiteral("ranked"), ranked},
        {QStringLiteral("priority"), priority},
    };
    if (!providerId.isEmpty())
        config.insert(QStringLiteral("providerId"), providerId);
    return {feedId, sourceId, config};
}

} // namespace

QList<PorticoFeedDefinition> PorticoFeedRegistry::definitions(
    const PorticoFeedRuntimeInputs &runtimeInputs)
{
    QList<PorticoFeedDefinition> result;
    result.reserve(13);

    result.push_back(stremioFeed(
        QStringLiteral("netflix"), QStringLiteral("nfx"), QStringLiteral("netflix"),
        QStringLiteral("Popular on Netflix"), NetflixPriority));
    result.push_back(stremioFeed(
        QStringLiteral("prime"), QStringLiteral("amp"), QStringLiteral("prime"),
        QStringLiteral("Popular on Prime Video"), PrimePriority));
    result.push_back(stremioFeed(
        QStringLiteral("hbomax"), QStringLiteral("hbm"), QStringLiteral("hbomax"),
        QStringLiteral("Popular on HBO Max"), HboMaxPriority));
    result.push_back(stremioFeed(
        QStringLiteral("disney"), QStringLiteral("dnp"), QStringLiteral("disney"),
        QStringLiteral("Popular on Disney+"), DisneyPriority));
    result.push_back(stremioFeed(
        QStringLiteral("appletv"), QStringLiteral("atp"), QStringLiteral("appletv"),
        QStringLiteral("Popular on Apple TV+"), AppleTvPriority));

    const QString spotifyCsv = runtimeInputs.spotifyChartsCsv.trimmed();
    if (!spotifyCsv.isEmpty()) {
        auto definition = feed(
            QStringLiteral("spotify:songs"), QStringLiteral("spotify"),
            QStringLiteral("Top songs on Spotify"), QStringLiteral("Spotify Charts"),
            true, SpotifyPriority, QStringLiteral("spotify"));
        definition.config.insert(QStringLiteral("csvUrl"), spotifyCsv);
        result.push_back(definition);
    }

    auto youtubeSongs = feed(
        QStringLiteral("youtube:songs"), QStringLiteral("youtube"),
        QStringLiteral("Top songs this week"), QStringLiteral("YouTube Charts"),
        true, YouTubeSongsPriority, QStringLiteral("ytmusic"));
    youtubeSongs.config.insert(QStringLiteral("category"), QStringLiteral("songs"));
    result.push_back(youtubeSongs);

    auto youtubeArtists = feed(
        QStringLiteral("youtube:artists"), QStringLiteral("youtube"),
        QStringLiteral("Top artists this week"), QStringLiteral("YouTube Charts"),
        true, YouTubeArtistsPriority, QStringLiteral("ytmusic"));
    youtubeArtists.config.insert(QStringLiteral("category"), QStringLiteral("artists"));
    result.push_back(youtubeArtists);

    const QString appleMusicToken = runtimeInputs.appleMusicDeveloperToken.trimmed();
    if (!appleMusicToken.isEmpty()) {
        auto definition = feed(
            QStringLiteral("applemusic:albums"), QStringLiteral("applemusic"),
            QStringLiteral("Top albums on Apple Music"), QStringLiteral("Apple Music"),
            true, AppleMusicPriority, QStringLiteral("applemusic"));
        definition.config.insert(QStringLiteral("chartType"), QStringLiteral("albums"));
        definition.config.insert(QStringLiteral("developerToken"), appleMusicToken);
        result.push_back(definition);
    }

    result.push_back(feed(
        QStringLiteral("openlibrary"), QStringLiteral("openlibrary"),
        QStringLiteral("Trending books"), QStringLiteral("Open Library"),
        true, OpenLibraryPriority));
    result.push_back(feed(
        QStringLiteral("anilist"), QStringLiteral("anilist"),
        QStringLiteral("Trending manga"), QStringLiteral("AniList"),
        true, AniListPriority));
    result.push_back(feed(
        QStringLiteral("webtoon"), QStringLiteral("webtoon"),
        QStringLiteral("Trending on WEBTOON"), QStringLiteral("WEBTOON"),
        true, WebtoonPriority, QStringLiteral("webtoon")));
    result.push_back(feed(
        QStringLiteral("globalcomix"), QStringLiteral("globalcomix"),
        QStringLiteral("Popular comics"), QStringLiteral("GlobalComix"),
        false, GlobalComixPriority));

    return result;
}

void PorticoFeedRegistry::configure(
    PorticoDiscoveryService &discovery,
    const PorticoFeedRuntimeInputs &runtimeInputs)
{
    const auto feeds = definitions(runtimeInputs);
    for (const auto &definition : feeds)
        discovery.configureFeed(definition.feedId, definition.sourceId, definition.config);
}
