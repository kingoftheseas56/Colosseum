#include "ArtistServiceFactory.h"

namespace vinyl::artist {

ComposedArtistService::ComposedArtistService(
    IArtistSource &source,
    ::artist_personal::PersonalBytesPort &personalBytesPort,
    int discographyPageSize,
    QObject *bridgeParent)
    : m_ownedBytesPort(nullptr),
      m_personalState(personalBytesPort),
      m_readAdapter(source, discographyPageSize),
      m_savedAdapter(m_personalState),
      m_playbackPort(source, bridgeParent),
      m_bridge(m_readAdapter, m_savedAdapter, m_playbackPort, bridgeParent) {}

ComposedArtistService::ComposedArtistService(
    IArtistSource &source,
    int discographyPageSize,
    QObject *bridgeParent)
    : m_ownedBytesPort(std::make_unique<InMemoryPersonalBytesPort>()),
      m_personalState(*m_ownedBytesPort),
      m_readAdapter(source, discographyPageSize),
      m_savedAdapter(m_personalState),
      m_playbackPort(source, bridgeParent),
      m_bridge(m_readAdapter, m_savedAdapter, m_playbackPort, bridgeParent) {}

std::unique_ptr<ComposedArtistService> createComposedArtistService(
    IArtistSource &source,
    ::artist_personal::PersonalBytesPort &personalBytesPort,
    int discographyPageSize,
    QObject *bridgeParent) {
    return std::make_unique<ComposedArtistService>(
        source, personalBytesPort, discographyPageSize, bridgeParent);
}

std::unique_ptr<ComposedArtistService> createComposedArtistService(
    IArtistSource &source,
    int discographyPageSize,
    QObject *bridgeParent) {
    return std::make_unique<ComposedArtistService>(
        source, discographyPageSize, bridgeParent);
}

std::unique_ptr<ArtistServiceBridge> createArtistServiceBridge(
    IArtistReadPort &readPort,
    ISavedArtistPort &savedPort,
    IPlaybackCommandPort &playbackPort,
    QObject *parent) {
    return std::make_unique<ArtistServiceBridge>(readPort, savedPort, playbackPort, parent);
}

} // namespace vinyl::artist
