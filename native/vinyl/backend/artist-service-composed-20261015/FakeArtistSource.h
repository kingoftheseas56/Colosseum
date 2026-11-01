#pragma once

#include "IArtistSource.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace vinyl::artist {

// In-memory fake implementation of IArtistSource for offline QtTest suites.
// Never touches provider.py, disk, or network. Supports immediate or deferred
// callback delivery so callers can deterministically test generation fences.
class FakeArtistSource final : public IArtistSource {
public:
    FakeArtistSource() = default;
    ~FakeArtistSource() override = default;

    void setOnline(bool online) noexcept { m_online = online; }
    bool isOnline() const noexcept { return m_online; }

    void setCorpusVersion(QString version) { m_corpusVersion = std::move(version); }
    const QString &corpusVersion() const noexcept { return m_corpusVersion; }

    void setDeferredDelivery(bool deferred) noexcept { m_deferred = deferred; }
    bool deferredDelivery() const noexcept { return m_deferred; }

    void addArtist(const ::vinyl::catalogue::artist_proof::ArtistRow &row);
    void addRedirect(const ::vinyl::ArtistMbid &from, const ::vinyl::ArtistMbid &to);
    void setReleaseGroups(const ::vinyl::ArtistMbid &artist,
                          QVector<::vinyl::catalogue::artist_proof::GroupRow> groups);
    void setEditionRecordings(const ::vinyl::ReleaseMbid &release,
                              QVector<::vinyl::RecordingMbid> recordings);

    void setMediaBatch(const ::vinyl::ArtistMbid &artist,
                       ::vinyl::artist_media::SourceBatch batch);
    void addMediaMapping(const ::vinyl::artist_media::VerifiedMediaMapping &mapping);

    void setArtworkPayload(const ::vinyl::ArtistMbid &artist, ArtworkSourcePayload payload);

    std::size_t pendingCallbackCount() const noexcept { return m_pendingCallbacks.size(); }
    void deliverAllPending();
    bool deliverNextPending();

    const QSet<quint64> &cancelledCatalogueTickets() const noexcept { return m_cancelledCatalogue; }
    const QSet<quint64> &cancelledMediaTickets() const noexcept { return m_cancelledMedia; }
    const QSet<quint64> &cancelledArtworkTickets() const noexcept { return m_cancelledArtwork; }

    // IArtistSource implementation
    void lookupArtist(
        const ::vinyl::catalogue::artist_proof::ArtistSelector &selector,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::ArtistProviderResult)> completion) override;

    void fetchReleaseGroups(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(::vinyl::catalogue::artist_proof::GroupsProviderResult)> completion) override;

    void cancelCatalogue(std::uint64_t ticket) noexcept override;

    void fetchMedia(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(::vinyl::artist_media::SourceBatch)> completion) override;

    std::optional<::vinyl::artist_media::VerifiedMediaMapping> lookupMediaMapping(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ProviderItemRef &providerItem) const override;

    void cancelMedia(std::uint64_t ticket) noexcept override;

    void fetchArtwork(
        const ::vinyl::ArtistMbid &artist,
        std::uint64_t ticket,
        std::function<void(ArtworkSourcePayload)> completion) override;

    void cancelArtwork(std::uint64_t ticket) noexcept override;

    bool hasReleaseInGroup(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ReleaseGroupMbid &group,
        const ::vinyl::ReleaseMbid &release) const override;

    bool hasRecordingInRelease(
        const ::vinyl::ArtistMbid &artist,
        const ::vinyl::ReleaseGroupMbid &group,
        const ::vinyl::ReleaseMbid &release,
        const ::vinyl::RecordingMbid &recording) const override;

private:
    void dispatchOrQueue(std::function<void()> fn);

    bool m_online = true;
    bool m_deferred = false;
    QString m_corpusVersion = QStringLiteral("mb-corpus-2026-10-15");

    QVector<::vinyl::catalogue::artist_proof::ArtistRow> m_artists;
    QVector<std::pair<::vinyl::ArtistMbid, ::vinyl::ArtistMbid>> m_redirects;
    QHash<QString, QVector<::vinyl::catalogue::artist_proof::GroupRow>> m_groupsByArtist;
    QHash<QString, QVector<::vinyl::RecordingMbid>> m_recordingsByRelease;

    QHash<QString, ::vinyl::artist_media::SourceBatch> m_mediaBatches;
    QVector<::vinyl::artist_media::VerifiedMediaMapping> m_mediaMappings;

    QHash<QString, ArtworkSourcePayload> m_artworkPayloads;

    std::vector<std::function<void()>> m_pendingCallbacks;
    QSet<quint64> m_cancelledCatalogue;
    QSet<quint64> m_cancelledMedia;
    QSet<quint64> m_cancelledArtwork;
};

} // namespace vinyl::artist
