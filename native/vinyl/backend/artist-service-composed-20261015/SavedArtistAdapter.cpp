#include "SavedArtistAdapter.h"

namespace vinyl::artist {

bool InMemoryPersonalBytesPort::hasSnapshot(const QString &profileScopeId) const {
    return m_snapshots.contains(profileScopeId);
}

QByteArray InMemoryPersonalBytesPort::rawBytes(const QString &profileScopeId) const {
    return m_snapshots.value(profileScopeId);
}

::artist_personal::Load InMemoryPersonalBytesPort::read(const QString &profileScopeId) {
    using ::artist_personal::Code;
    using ::artist_personal::Status;
    if (!m_available) {
        return {Status::failure(Code::IoError, QStringLiteral("Personal store is offline.")), {}};
    }
    const auto it = m_snapshots.constFind(profileScopeId);
    if (it == m_snapshots.cend()) {
        return {Status::success(), {false, {}}};
    }
    return {Status::success(), {true, it.value()}};
}

::artist_personal::Status InMemoryPersonalBytesPort::commit(
    const QString &profileScopeId,
    const ::artist_personal::BytesSnapshot &expected,
    const QByteArray &next) {
    using ::artist_personal::BytesSnapshot;
    using ::artist_personal::Code;
    using ::artist_personal::Status;
    if (!m_available) {
        return Status::failure(Code::IoError, QStringLiteral("Personal store is offline."));
    }
    const auto it = m_snapshots.constFind(profileScopeId);
    const BytesSnapshot current = (it == m_snapshots.cend())
        ? BytesSnapshot{false, {}}
        : BytesSnapshot{true, it.value()};
    if (!(current == expected)) {
        return Status::failure(Code::ConcurrentChange,
                               QStringLiteral("Profile snapshot changed concurrently."));
    }
    m_snapshots.insert(profileScopeId, next);
    return Status::success(true);
}

SavedArtistAdapter::SavedArtistAdapter(
    ::artist_personal::ArtistPersonalState &personalState,
    qint64 initialTimestampMs)
    : m_personal(personalState),
      m_nextTimestampMs(initialTimestampMs > 0 ? initialTimestampMs : 1700000000000LL) {}

SavedReply SavedArtistAdapter::fromStatus(
    const QString &profileScopeId,
    const QString &artistMbid,
    const ::artist_personal::Status &status) {
    using ::artist_personal::Code;
    SavedReply reply;
    reply.profileScopeId = profileScopeId;
    reply.artistMbid = artistMbid.toLower();
    reply.saved = false;
    if (status.code == Code::IoError || status.code == Code::ConcurrentChange) {
        reply.state = State::Unavailable;
    } else {
        reply.state = State::Error;
    }
    reply.errorCode = ::artist_personal::codeName(status.code);
    reply.message = status.detail.isEmpty()
        ? QStringLiteral("Personal state operation failed.")
        : status.detail;
    return reply;
}

SavedReply SavedArtistAdapter::read(const QString &profileScopeId, const QString &artistMbid) {
    const ::artist_personal::Status opened = m_personal.open(profileScopeId);
    if (!opened.ok()) {
        return fromStatus(profileScopeId, artistMbid, opened);
    }
    const auto parsedArtist = ::vinyl::ArtistMbid::parse(artistMbid);
    if (!parsedArtist.ok()) {
        SavedReply bad;
        bad.state = State::Error;
        bad.saved = false;
        bad.profileScopeId = profileScopeId;
        bad.artistMbid = artistMbid;
        bad.errorCode = QStringLiteral("invalid_artist_mbid");
        bad.message = QStringLiteral("Invalid Artist MBID.");
        return bad;
    }
    SavedReply ok;
    ok.state = State::Ready;
    ok.profileScopeId = profileScopeId;
    ok.artistMbid = parsedArtist.value->toString().toLower();
    ok.saved = m_personal.isSavedArtist(ok.artistMbid);
    return ok;
}

SavedReply SavedArtistAdapter::write(
    const QString &profileScopeId,
    const QString &artistMbid,
    bool saved) {
    const ::artist_personal::Status opened = m_personal.open(profileScopeId);
    if (!opened.ok()) {
        return fromStatus(profileScopeId, artistMbid, opened);
    }
    const auto parsedArtist = ::vinyl::ArtistMbid::parse(artistMbid);
    if (!parsedArtist.ok()) {
        SavedReply bad;
        bad.state = State::Error;
        bad.saved = false;
        bad.profileScopeId = profileScopeId;
        bad.artistMbid = artistMbid;
        bad.errorCode = QStringLiteral("invalid_artist_mbid");
        bad.message = QStringLiteral("Invalid Artist MBID.");
        return bad;
    }

    const QString canonicalMbid = parsedArtist.value->toString().toLower();
    ::artist_personal::Status mutated = ::artist_personal::Status::success(false);
    if (saved) {
        mutated = m_personal.saveArtist(canonicalMbid, ++m_nextTimestampMs);
    } else if (m_personal.isSavedArtist(canonicalMbid)) {
        mutated = m_personal.unsaveArtist(canonicalMbid);
    }

    if (!mutated.ok()) {
        return fromStatus(profileScopeId, canonicalMbid, mutated);
    }

    SavedReply ok;
    ok.state = State::Ready;
    ok.profileScopeId = profileScopeId;
    ok.artistMbid = canonicalMbid;
    ok.saved = m_personal.isSavedArtist(canonicalMbid);
    return ok;
}

} // namespace vinyl::artist
