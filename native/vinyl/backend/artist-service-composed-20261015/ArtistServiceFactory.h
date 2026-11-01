#pragma once

#include "../artist-live-prototypes-20261015/bridge/ArtistServiceBridge.h"
#include "../artist-live-prototypes-20261015/personal/ArtistPersonalState.h"
#include "ArtistReadAdapter.h"
#include "IArtistSource.h"
#include "RecordingPlaybackPort.h"
#include "SavedArtistAdapter.h"

#include <memory>

namespace vinyl::artist {

// Self-contained composed service bundle owning the personal state instance,
// concrete adapters (ArtistReadAdapter, SavedArtistAdapter, RecordingPlaybackPort),
// and the QML-facing ArtistServiceBridge.
class ComposedArtistService final {
public:
    ComposedArtistService(
        IArtistSource &source,
        ::artist_personal::PersonalBytesPort &personalBytesPort,
        int discographyPageSize = 2,
        QObject *bridgeParent = nullptr);

    explicit ComposedArtistService(
        IArtistSource &source,
        int discographyPageSize = 2,
        QObject *bridgeParent = nullptr);

    ~ComposedArtistService() = default;

    ComposedArtistService(const ComposedArtistService &) = delete;
    ComposedArtistService &operator=(const ComposedArtistService &) = delete;

    ArtistServiceBridge &bridge() noexcept { return m_bridge; }
    const ArtistServiceBridge &bridge() const noexcept { return m_bridge; }

    ArtistReadAdapter &readAdapter() noexcept { return m_readAdapter; }
    const ArtistReadAdapter &readAdapter() const noexcept { return m_readAdapter; }

    SavedArtistAdapter &savedAdapter() noexcept { return m_savedAdapter; }
    const SavedArtistAdapter &savedAdapter() const noexcept { return m_savedAdapter; }

    RecordingPlaybackPort &playbackPort() noexcept { return m_playbackPort; }
    const RecordingPlaybackPort &playbackPort() const noexcept { return m_playbackPort; }

    ::artist_personal::ArtistPersonalState &personalState() noexcept { return m_personalState; }
    const ::artist_personal::ArtistPersonalState &personalState() const noexcept { return m_personalState; }

private:
    std::unique_ptr<InMemoryPersonalBytesPort> m_ownedBytesPort;
    ::artist_personal::ArtistPersonalState m_personalState;
    ArtistReadAdapter m_readAdapter;
    SavedArtistAdapter m_savedAdapter;
    RecordingPlaybackPort m_playbackPort;
    ArtistServiceBridge m_bridge;
};

// Factory functions building a fully wired ComposedArtistService or standalone ArtistServiceBridge.
std::unique_ptr<ComposedArtistService> createComposedArtistService(
    IArtistSource &source,
    ::artist_personal::PersonalBytesPort &personalBytesPort,
    int discographyPageSize = 2,
    QObject *bridgeParent = nullptr);

std::unique_ptr<ComposedArtistService> createComposedArtistService(
    IArtistSource &source,
    int discographyPageSize = 2,
    QObject *bridgeParent = nullptr);

std::unique_ptr<ArtistServiceBridge> createArtistServiceBridge(
    IArtistReadPort &readPort,
    ISavedArtistPort &savedPort,
    IPlaybackCommandPort &playbackPort,
    QObject *parent = nullptr);

} // namespace vinyl::artist
