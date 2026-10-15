#include "FakeArtistSource.h"

namespace vinyl::artist {

void FakeArtistSource::addArtist(const ::vinyl::catalogue::artist_proof::ArtistRow &row) {
    const QString mbid = row.canonical.mbid.toString().toLower();
    for (int i = 0; i < m_artists.size(); ++i) {
        if (m_artists.at(i).canonical.mbid.toString().toLower() == mbid) {
            m_artists[i] = row;
            return;
        }
    }
    m_artists.push_back(row);
}

void FakeArtistSource::addRedirect(const ::vinyl::ArtistMbid &from, const ::vinyl::ArtistMbid &to) {
    for (auto &pair : m_redirects) {
        if (pair.first == from) {
            pair.second = to;
            return;
        }
    }
    m_redirects.push_back({from, to});
}

void FakeArtistSource::setReleaseGroups(
    const ::vinyl::ArtistMbid &artist,
    QVector<::vinyl::catalogue::artist_proof::GroupRow> groups) {
    m_groupsByArtist.insert(artist.toString().toLower(), std::move(groups));
}

void FakeArtistSource::setEditionRecordings(
    const ::vinyl::ReleaseMbid &release,
    QVector<::vinyl::RecordingMbid> recordings) {
    m_recordingsByRelease.insert(release.toString().toLower(), std::move(recordings));
}

void FakeArtistSource::setMediaBatch(
    const ::vinyl::ArtistMbid &artist,
    ::vinyl::artist_media::SourceBatch batch) {
    m_mediaBatches.insert(artist.toString().toLower(), std::move(batch));
}

void FakeArtistSource::addMediaMapping(const ::vinyl::artist_media::VerifiedMediaMapping &mapping) {
    m_mediaMappings.push_back(mapping);
}

void FakeArtistSource::setArtworkPayload(
    const ::vinyl::ArtistMbid &artist,
    ArtworkSourcePayload payload) {
    m_artworkPayloads.insert(artist.toString().toLower(), std::move(payload));
}

void FakeArtistSource::dispatchOrQueue(std::function<void()> fn) {
    if (m_deferred) {
        m_pendingCallbacks.push_back(std::move(fn));
    } else {
        fn();
    }
}

void FakeArtistSource::deliverAllPending() {
    std::vector<std::function<void()>> batch;
    batch.swap(m_pendingCallbacks);
    for (auto &fn : batch) {
        if (fn) fn();
    }
}

bool FakeArtistSource::deliverNextPending() {
    if (m_pendingCallbacks.empty()) return false;
    auto fn = std::move(m_pendingCallbacks.front());
    m_pendingCallbacks.erase(m_pendingCallbacks.begin());
    if (fn) fn();
    return true;
}

void FakeArtistSource::lookupArtist(
    const ::vinyl::catalogue::artist_proof::ArtistSelector &selector,
    std::uint64_t ticket,
    std::function<void(::vinyl::catalogue::artist_proof::ArtistProviderResult)> completion) {
    Q_UNUSED(ticket);
    dispatchOrQueue([this, selector, done = std::move(completion)]() mutable {
        using ::vinyl::catalogue::CatalogueErrorCode;
        using ::vinyl::catalogue::artist_proof::ArtistEnvelope;
        using ::vinyl::catalogue::artist_proof::ArtistProviderResult;
        if (!m_online) {
            done(ArtistProviderResult::failure(CatalogueErrorCode::Unavailable,
                                               QStringLiteral("source"),
                                               QStringLiteral("Fake artist source is offline.")));
            return;
        }
        ArtistEnvelope env;
        env.corpusVersion = m_corpusVersion;
        if (!selector.mbid) {
            env.candidates = m_artists;
        } else {
            for (const auto &redirect : m_redirects) {
                if (redirect.first == *selector.mbid) {
                    env.redirectedTo = redirect.second;
                    break;
                }
            }
            if (!env.redirectedTo) {
                for (const auto &row : m_artists) {
                    if (row.canonical.mbid == *selector.mbid) {
                        env.candidates.push_back(row);
                    }
                }
            }
        }
        done(ArtistProviderResult::success(std::move(env)));
    });
}

void FakeArtistSource::fetchReleaseGroups(
    const ::vinyl::ArtistMbid &artist,
    std::uint64_t ticket,
    std::function<void(::vinyl::catalogue::artist_proof::GroupsProviderResult)> completion) {
    Q_UNUSED(ticket);
    dispatchOrQueue([this, artist, done = std::move(completion)]() mutable {
        using ::vinyl::catalogue::CatalogueErrorCode;
        using ::vinyl::catalogue::artist_proof::GroupsEnvelope;
        using ::vinyl::catalogue::artist_proof::GroupsProviderResult;
        if (!m_online) {
            done(GroupsProviderResult::failure(CatalogueErrorCode::Unavailable,
                                               QStringLiteral("source"),
                                               QStringLiteral("Fake artist source is offline.")));
            return;
        }
        GroupsEnvelope env;
        env.corpusVersion = m_corpusVersion;
        env.rows = m_groupsByArtist.value(artist.toString().toLower());
        done(GroupsProviderResult::success(std::move(env)));
    });
}

void FakeArtistSource::cancelCatalogue(std::uint64_t ticket) noexcept {
    m_cancelledCatalogue.insert(static_cast<quint64>(ticket));
}

void FakeArtistSource::fetchMedia(
    const ::vinyl::ArtistMbid &artist,
    std::uint64_t ticket,
    std::function<void(::vinyl::artist_media::SourceBatch)> completion) {
    Q_UNUSED(ticket);
    dispatchOrQueue([this, artist, done = std::move(completion)]() mutable {
        using ::vinyl::artist_media::QueryStatus;
        using ::vinyl::artist_media::SourceBatch;
        if (!m_online) {
            SourceBatch unavailable;
            unavailable.artist = artist;
            unavailable.status = QueryStatus::Unavailable;
            done(std::move(unavailable));
            return;
        }
        const QString key = artist.toString().toLower();
        const auto it = m_mediaBatches.constFind(key);
        if (it != m_mediaBatches.cend()) {
            done(it.value());
            return;
        }
        SourceBatch empty;
        empty.artist = artist;
        empty.status = QueryStatus::Empty;
        empty.sourceLabel = QStringLiteral("FakeArtistSource");
        empty.snapshotId = m_corpusVersion;
        done(std::move(empty));
    });
}

std::optional<::vinyl::artist_media::VerifiedMediaMapping> FakeArtistSource::lookupMediaMapping(
    const ::vinyl::ArtistMbid &artist,
    const ::vinyl::ProviderItemRef &providerItem) const {
    for (const auto &m : m_mediaMappings) {
        if (m.artist == artist &&
            m.provider == providerItem.providerId() &&
            m.providerItem == providerItem.itemId()) {
            return m;
        }
    }
    return std::nullopt;
}

void FakeArtistSource::cancelMedia(std::uint64_t ticket) noexcept {
    m_cancelledMedia.insert(static_cast<quint64>(ticket));
}

void FakeArtistSource::fetchArtwork(
    const ::vinyl::ArtistMbid &artist,
    std::uint64_t ticket,
    std::function<void(ArtworkSourcePayload)> completion) {
    Q_UNUSED(ticket);
    dispatchOrQueue([this, artist, done = std::move(completion)]() mutable {
        if (!m_online) {
            ArtworkSourcePayload offline;
            offline.available = false;
            done(std::move(offline));
            return;
        }
        const QString key = artist.toString().toLower();
        const auto it = m_artworkPayloads.constFind(key);
        if (it != m_artworkPayloads.cend()) {
            done(it.value());
            return;
        }
        ArtworkSourcePayload fallback;
        fallback.available = true;
        fallback.authority = QStringLiteral("artwork-provider");
        fallback.sourceItemId = QStringLiteral("artwork:") + key;
        fallback.revision = m_corpusVersion;
        for (const auto &row : m_artists) {
            if (row.canonical.mbid == artist) {
                fallback.artistName = row.canonical.name;
                break;
            }
        }
        done(std::move(fallback));
    });
}

void FakeArtistSource::cancelArtwork(std::uint64_t ticket) noexcept {
    m_cancelledArtwork.insert(static_cast<quint64>(ticket));
}

bool FakeArtistSource::hasReleaseInGroup(
    const ::vinyl::ArtistMbid &artist,
    const ::vinyl::ReleaseGroupMbid &group,
    const ::vinyl::ReleaseMbid &release) const {
    const auto groups = m_groupsByArtist.value(artist.toString().toLower());
    for (const auto &row : groups) {
        if (row.canonical.mbid == group) {
            for (const auto &edition : row.editions) {
                if (edition.mbid == release && edition.releaseGroupMbid == group) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool FakeArtistSource::hasRecordingInRelease(
    const ::vinyl::ArtistMbid &artist,
    const ::vinyl::ReleaseGroupMbid &group,
    const ::vinyl::ReleaseMbid &release,
    const ::vinyl::RecordingMbid &recording) const {
    if (!hasReleaseInGroup(artist, group, release)) {
        return false;
    }
    const QString releaseKey = release.toString().toLower();
    const auto explicitIt = m_recordingsByRelease.constFind(releaseKey);
    if (explicitIt != m_recordingsByRelease.cend()) {
        for (const auto &rec : explicitIt.value()) {
            if (rec == recording) return true;
        }
        return false;
    }
    const auto groups = m_groupsByArtist.value(artist.toString().toLower());
    for (const auto &row : groups) {
        if (row.canonical.mbid == group) {
            for (const auto &sourcedRec : row.recordings) {
                if (sourcedRec.canonical.mbid == recording) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace vinyl::artist
