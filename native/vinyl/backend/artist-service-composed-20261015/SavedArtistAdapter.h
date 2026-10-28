#pragma once

#include "../artist-live-prototypes-20261015/bridge/ArtistServiceBridge.h"
#include "../artist-live-prototypes-20261015/personal/ArtistPersonalState.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

namespace vinyl::artist {

// Hermetic in-memory PersonalBytesPort for offline tests and reload verification.
// Compares expected prior bytes on commit just like AtomicFilePort.
class InMemoryPersonalBytesPort final : public ::artist_personal::PersonalBytesPort {
public:
    InMemoryPersonalBytesPort() = default;
    ~InMemoryPersonalBytesPort() override = default;

    void setAvailable(bool available) noexcept { m_available = available; }
    bool isAvailable() const noexcept { return m_available; }

    bool hasSnapshot(const QString &profileScopeId) const;
    QByteArray rawBytes(const QString &profileScopeId) const;

    ::artist_personal::Load read(const QString &profileScopeId) override;
    ::artist_personal::Status commit(
        const QString &profileScopeId,
        const ::artist_personal::BytesSnapshot &expected,
        const QByteArray &next) override;

private:
    bool m_available = true;
    QHash<QString, QByteArray> m_snapshots;
};

// Concrete ISavedArtistPort adapter wrapping ArtistPersonalState.
class SavedArtistAdapter final : public ISavedArtistPort {
public:
    explicit SavedArtistAdapter(
        ::artist_personal::ArtistPersonalState &personalState,
        qint64 initialTimestampMs = 1700000000000LL);
    ~SavedArtistAdapter() override = default;

    SavedArtistAdapter(const SavedArtistAdapter &) = delete;
    SavedArtistAdapter &operator=(const SavedArtistAdapter &) = delete;

    ::artist_personal::ArtistPersonalState &personalState() noexcept { return m_personal; }
    const ::artist_personal::ArtistPersonalState &personalState() const noexcept { return m_personal; }

    SavedReply read(const QString &profileScopeId, const QString &artistMbid) override;
    SavedReply write(const QString &profileScopeId, const QString &artistMbid, bool saved) override;

private:
    static SavedReply fromStatus(
        const QString &profileScopeId,
        const QString &artistMbid,
        const ::artist_personal::Status &status);

    ::artist_personal::ArtistPersonalState &m_personal;
    qint64 m_nextTimestampMs = 1700000000000LL;
};

} // namespace vinyl::artist
