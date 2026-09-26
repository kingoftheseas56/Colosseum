#include "TrackerTypes.h"

namespace {

constexpr int kKnownCapabilityBits = static_cast<int>(TrackerProviderCapability::ReadHistory)
    | static_cast<int>(TrackerProviderCapability::ReadProgress)
    | static_cast<int>(TrackerProviderCapability::WriteProgress)
    | static_cast<int>(TrackerProviderCapability::WriteCompletion)
    | static_cast<int>(TrackerProviderCapability::Scrobble);

} // namespace

QString trackerProviderKey(TrackerProviderId providerId)
{
    switch (providerId) {
    case TrackerProviderId::Simkl:
        return QStringLiteral("simkl");
    case TrackerProviderId::Mal:
        return QStringLiteral("mal");
    case TrackerProviderId::Trakt:
        return QStringLiteral("trakt");
    case TrackerProviderId::AniList:
        return QStringLiteral("anilist");
    }
    return QString();
}

std::optional<TrackerProviderId> trackerProviderIdFromKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QLatin1String("simkl"))
        return TrackerProviderId::Simkl;
    if (normalized == QLatin1String("mal"))
        return TrackerProviderId::Mal;
    if (normalized == QLatin1String("trakt"))
        return TrackerProviderId::Trakt;
    if (normalized == QLatin1String("anilist"))
        return TrackerProviderId::AniList;
    return std::nullopt;
}

QString trackerProviderDisplayName(TrackerProviderId providerId)
{
    switch (providerId) {
    case TrackerProviderId::Simkl:
        return QStringLiteral("SIMKL");
    case TrackerProviderId::Mal:
        return QStringLiteral("MyAnimeList");
    case TrackerProviderId::Trakt:
        return QStringLiteral("Trakt");
    case TrackerProviderId::AniList:
        return QStringLiteral("AniList");
    }
    return QStringLiteral("Tracker");
}

bool trackerProviderCapabilitiesAreKnown(TrackerProviderCapabilities capabilities)
{
    return (capabilities.toInt() & ~kKnownCapabilityBits) == 0;
}

QList<TrackerProviderDescriptor> trackerBuiltInProviderCatalog()
{
    TrackerProviderCapabilities simkl;
#ifdef COLOSSEUM_SIMKL_CLIENT_ID
    simkl = TrackerProviderCapability::ReadHistory
        | TrackerProviderCapability::ReadProgress
        | TrackerProviderCapability::WriteProgress
        | TrackerProviderCapability::WriteCompletion
        | TrackerProviderCapability::Scrobble;
#endif
    return {
        {TrackerProviderId::Simkl, trackerProviderDisplayName(TrackerProviderId::Simkl),
         simkl, simkl != TrackerProviderCapabilities{}},
        {TrackerProviderId::Mal, trackerProviderDisplayName(TrackerProviderId::Mal), {}, false},
        {TrackerProviderId::Trakt, trackerProviderDisplayName(TrackerProviderId::Trakt), {}, false},
        {TrackerProviderId::AniList, trackerProviderDisplayName(TrackerProviderId::AniList), {}, false}
    };
}
