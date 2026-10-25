#include "ArtistPersonalState.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <utility>

namespace artist_personal {
namespace {
constexpr qint64 MaxSnapshotBytes = 4 * 1024 * 1024;

Status fromLibrary(const vinyl::LibraryMutationStatus &s) {
    if (s.ok) return Status::success(s.changed);
    if (s.error.code == vinyl::LibraryErrorCode::EntryNotFound)
        return Status::failure(Code::MissingItem, s.error.detail);
    if (s.error.code == vinyl::LibraryErrorCode::InvalidSavedAt)
        return Status::failure(Code::InvalidTimestamp, s.error.detail);
    return Status::failure(Code::InvalidState, s.error.detail);
}
Status fromPlaylist(const vinyl::PlaylistMutationStatus &s) {
    if (s.ok) return Status::success(s.changed);
    using E = vinyl::PlaylistErrorCode;
    switch (s.error.code) {
    case E::InvalidPlaylistId: return Status::failure(Code::InvalidPlaylistId);
    case E::InvalidPlaylistItemId: return Status::failure(Code::InvalidItemId);
    case E::InvalidPlaylistName: return Status::failure(Code::InvalidName);
    case E::InvalidCreatedAt:
    case E::InvalidAddedAt: return Status::failure(Code::InvalidTimestamp);
    case E::DuplicatePlaylistItemId: return Status::failure(Code::DuplicateItem);
    case E::PlaylistItemNotFound: return Status::failure(Code::MissingItem);
    case E::IndexOutOfRange: return Status::failure(Code::IndexOutOfRange);
    default: return Status::failure(Code::InvalidState);
    }
}
bool validSnapshot(const std::optional<vinyl::PersonalDisplaySnapshot> &snapshot) {
    if (!snapshot) return true;
    const auto good = [](const QString &s) {
        if (s.size() > 256) return false;
        for (const QChar ch : s)
            if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
        return true;
    };
    return good(snapshot->title) && good(snapshot->artistCredit);
}
Code restoreError(vinyl::PersonalStatePersistenceErrorCode error) {
    using E = vinyl::PersonalStatePersistenceErrorCode;
    if (error == E::UnsupportedVersion) return Code::UnsupportedVersion;
    if (error == E::ProfileMismatch) return Code::ProfileMismatch;
    return Code::Corrupt;
}
} // namespace

QString codeName(Code code) {
    switch (code) {
    case Code::Ok: return "ok";
    case Code::NotOpen: return "not_open";
    case Code::InvalidProfile: return "invalid_profile";
    case Code::InvalidArtistMbid: return "invalid_artist_mbid";
    case Code::InvalidRecordingMbid: return "invalid_recording_mbid";
    case Code::InvalidPlaylistId: return "invalid_playlist_id";
    case Code::InvalidItemId: return "invalid_item_id";
    case Code::InvalidName: return "invalid_name";
    case Code::InvalidTimestamp: return "invalid_timestamp";
    case Code::InvalidSnapshot: return "invalid_snapshot";
    case Code::DuplicatePlaylist: return "duplicate_playlist";
    case Code::MissingPlaylist: return "missing_playlist";
    case Code::MissingItem: return "missing_item";
    case Code::DuplicateItem: return "duplicate_item";
    case Code::IndexOutOfRange: return "index_out_of_range";
    case Code::Corrupt: return "corrupt";
    case Code::UnsupportedVersion: return "unsupported_version";
    case Code::ProfileMismatch: return "profile_mismatch";
    case Code::IoError: return "io_error";
    case Code::ConcurrentChange: return "concurrent_change";
    case Code::InvalidState: return "invalid_state";
    }
    return "unknown";
}

AtomicFilePort::AtomicFilePort(QString testRoot)
    : m_root(testRoot.isEmpty() ? QString() : QDir::cleanPath(std::move(testRoot))) {}

QString AtomicFilePort::filePath(const QString &profileScopeId) const {
    // Host-owned case-sensitive ProfileScopeId never becomes a path component.
    const QByteArray hash = QCryptographicHash::hash(
        profileScopeId.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(m_root).filePath("profile-" + QString::fromLatin1(hash) + ".json");
}

Load AtomicFilePort::read(const QString &profileScopeId) {
    if (m_root.isEmpty())
        return {Status::failure(Code::InvalidState, "No explicit isolated root"), {}};
    const QString path = filePath(profileScopeId);
    QFileInfo info(path);
    if (info.isSymLink())
        return {Status::failure(Code::IoError, "Refusing symlink"), {}};
    QFile file(path);
    if (!file.exists()) return {Status::success(), {false, {}}};
    if (info.size() > MaxSnapshotBytes)
        return {Status::failure(Code::Corrupt, "Snapshot exceeds prototype limit"), {}};
    if (!file.open(QIODevice::ReadOnly))
        return {Status::failure(Code::IoError, file.errorString()), {}};
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return {Status::failure(Code::IoError, file.errorString()), {}};
    return {Status::success(), {true, bytes}};
}

Status AtomicFilePort::commit(const QString &profileScopeId,
                              const BytesSnapshot &expected, const QByteArray &next) {
    if (m_root.isEmpty() || next.size() > MaxSnapshotBytes)
        return Status::failure(Code::InvalidState, "Invalid root or oversized snapshot");
    if (!QDir().mkpath(m_root))
        return Status::failure(Code::IoError, "Cannot create isolated root");
    const QString path = filePath(profileScopeId);
#ifdef Q_OS_WIN
    if (path.size() + 5 >= 248)
        return Status::failure(Code::IoError, "Fixture path too long for Win32 file lock");
#endif
    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return Status::failure(Code::ConcurrentChange, "Profile writer lock is held");
    const Load current = read(profileScopeId);
    if (!current.status.ok()) return current.status;
    if (!(current.snapshot == expected))
        return Status::failure(Code::ConcurrentChange, "Profile file changed since open");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Status::failure(Code::IoError, file.errorString());
    if (file.write(next) != next.size()) {
        file.cancelWriting();
        return Status::failure(Code::IoError, "Incomplete snapshot write");
    }
    if (!file.commit()) {
        const QString error = file.errorString();
        lock.unlock();
        return Status::failure(Code::IoError, error);
    }
    lock.unlock();
    return Status::success(true);
}

Status ArtistPersonalState::open(const QString &scope) {
    m_profile.reset();
    m_library.reset();
    m_playlists.clear();
    m_disk = {};
    m_loadedVersion = 0;
    auto parsed = vinyl::ProfileScopeId::parse(scope);
    if (!parsed.ok()) return Status::failure(Code::InvalidProfile);
    const Load loaded = m_port.read(parsed.value->value());
    if (!loaded.status.ok()) return loaded.status;
    if (!loaded.snapshot.exists) {
        m_library.emplace(*parsed.value);
    } else {
        auto restored = vinyl::VinylPersonalStatePersistence::restore(
            *parsed.value, loaded.snapshot.content);
        if (!restored.ok()) return Status::failure(
            restoreError(restored.error.code),
            vinyl::personalStatePersistenceErrorCodeName(restored.error.code)
                + ":" + restored.error.field);
        m_library.emplace(std::move(restored.value->library));
        m_playlists = std::move(restored.value->playlists);
        const QJsonDocument doc = QJsonDocument::fromJson(loaded.snapshot.content);
        m_loadedVersion = doc.object().value("version").toInt();
    }
    m_profile = *parsed.value;
    m_disk = loaded.snapshot;
    return Status::success();
}

Status ArtistPersonalState::apply(
    const std::function<Status(vinyl::VinylLibrary &, QVector<vinyl::Playlist> &)> &fn) {
    if (!ready()) return Status::failure(Code::NotOpen);
    auto library = *m_library;
    auto playlists = m_playlists;
    Status result = fn(library, playlists);
    if (!result.ok() || !result.changed) return result;
    auto bytes = vinyl::VinylPersonalStatePersistence::capture(
        *m_profile, library, playlists);
    if (!bytes.ok())
        return Status::failure(Code::InvalidState, bytes.error.detail);
    Status written = m_port.commit(m_profile->value(), m_disk, *bytes.value);
    if (!written.ok()) return written;
    m_library = std::move(library);
    m_playlists = std::move(playlists);
    m_disk = {true, *bytes.value};
    m_loadedVersion = vinyl::VinylPersonalStatePersistence::SchemaVersion;
    return Status::success(true);
}

int ArtistPersonalState::indexOfPlaylist(
    const QVector<vinyl::Playlist> &playlists, const QString &id) const {
    for (int i = 0; i < playlists.size(); ++i)
        if (playlists.at(i).playlistId().value() == id) return i;
    return -1;
}

const vinyl::LibraryEntry *ArtistPersonalState::findSavedArtist(
    const QString &artistMbid) const {
    if (!ready()) return nullptr;
    auto id = vinyl::ArtistMbid::parse(artistMbid);
    if (!id.ok()) return nullptr;
    return m_library->find(vinyl::CanonicalEntityRef::from(*id.value));
}

QVector<vinyl::Playlist> ArtistPersonalState::playlistsByCreation() const {
    auto result = m_playlists;
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.createdAtMs() != b.createdAtMs())
            return a.createdAtMs() < b.createdAtMs();
        return a.playlistId().value() < b.playlistId().value();
    });
    return result;
}

const vinyl::Playlist *ArtistPersonalState::findPlaylist(const QString &id) const {
    if (!ready()) return nullptr;
    const int index = indexOfPlaylist(m_playlists, id);
    return index < 0 ? nullptr : &m_playlists.at(index);
}

Status ArtistPersonalState::saveArtist(
    const QString &artistMbid, qint64 at,
    std::optional<vinyl::PersonalDisplaySnapshot> display) {
    auto id = vinyl::ArtistMbid::parse(artistMbid);
    if (!id.ok()) return Status::failure(Code::InvalidArtistMbid);
    if (!validSnapshot(display)) return Status::failure(Code::InvalidSnapshot);
    auto entry = vinyl::LibraryEntry::create(
        vinyl::CanonicalEntityRef::from(*id.value), at, std::move(display));
    if (!entry.ok()) return Status::failure(Code::InvalidTimestamp);
    return apply([&](auto &library, auto &) {
        return fromLibrary(library.save(*entry.value));
    });
}

Status ArtistPersonalState::unsaveArtist(const QString &artistMbid) {
    auto id = vinyl::ArtistMbid::parse(artistMbid);
    if (!id.ok()) return Status::failure(Code::InvalidArtistMbid);
    return apply([&](auto &library, auto &) {
        return fromLibrary(library.remove(vinyl::CanonicalEntityRef::from(*id.value)));
    });
}

Status ArtistPersonalState::toggleArtist(
    const QString &artistMbid, qint64 at,
    std::optional<vinyl::PersonalDisplaySnapshot> display) {
    auto id = vinyl::ArtistMbid::parse(artistMbid);
    if (!id.ok()) return Status::failure(Code::InvalidArtistMbid);
    if (!validSnapshot(display)) return Status::failure(Code::InvalidSnapshot);
    return apply([&](auto &library, auto &) {
        auto canonical = vinyl::CanonicalEntityRef::from(*id.value);
        if (library.contains(canonical))
            return fromLibrary(library.remove(canonical));
        auto entry = vinyl::LibraryEntry::create(canonical, at, display);
        if (!entry.ok()) return Status::failure(Code::InvalidTimestamp);
        return fromLibrary(library.save(*entry.value));
    });
}

Status ArtistPersonalState::createPlaylist(
    const QString &id, const QString &name, qint64 at) {
    auto parsed = vinyl::PlaylistId::parse(id);
    if (!parsed.ok()) return Status::failure(Code::InvalidPlaylistId);
    if (!ready()) return Status::failure(Code::NotOpen);
    auto candidate = vinyl::Playlist::create(*m_profile, *parsed.value, name,
                                             at, vinyl::PlaylistProvenanceKind::Manual);
    if (!candidate.ok()) {
        return Status::failure(candidate.error.code == vinyl::PlaylistErrorCode::InvalidPlaylistName
                                   ? Code::InvalidName : Code::InvalidTimestamp);
    }
    return apply([&](auto &, auto &playlists) {
        if (indexOfPlaylist(playlists, id) >= 0)
            return Status::failure(Code::DuplicatePlaylist);
        playlists.append(*candidate.value);
        return Status::success(true);
    });
}

Status ArtistPersonalState::renamePlaylist(const QString &id, const QString &name) {
    if (!vinyl::PlaylistId::parse(id).ok())
        return Status::failure(Code::InvalidPlaylistId);
    return apply([&](auto &, auto &playlists) {
        const int i = indexOfPlaylist(playlists, id);
        if (i < 0) return Status::failure(Code::MissingPlaylist);
        return fromPlaylist(playlists[i].rename(name));
    });
}

Status ArtistPersonalState::deletePlaylist(const QString &id) {
    if (!vinyl::PlaylistId::parse(id).ok())
        return Status::failure(Code::InvalidPlaylistId);
    return apply([&](auto &, auto &playlists) {
        const int i = indexOfPlaylist(playlists, id);
        if (i < 0) return Status::failure(Code::MissingPlaylist);
        playlists.removeAt(i);
        return Status::success(true);
    });
}

Status ArtistPersonalState::addRecording(
    const QString &playlistId, const QString &itemId, const QString &recordingMbid,
    qint64 at, int index, std::optional<vinyl::PersonalDisplaySnapshot> display) {
    if (!vinyl::PlaylistId::parse(playlistId).ok())
        return Status::failure(Code::InvalidPlaylistId);
    auto parsedItem = vinyl::PlaylistItemId::parse(itemId);
    if (!parsedItem.ok()) return Status::failure(Code::InvalidItemId);
    auto recording = vinyl::RecordingMbid::parse(recordingMbid);
    if (!recording.ok()) return Status::failure(Code::InvalidRecordingMbid);
    if (!validSnapshot(display)) return Status::failure(Code::InvalidSnapshot);
    auto candidate = vinyl::PlaylistItem::create(*parsedItem.value, *recording.value,
                                                  at, std::nullopt, std::move(display));
    if (!candidate.ok()) return Status::failure(Code::InvalidTimestamp);
    return apply([&](auto &, auto &playlists) {
        const int i = indexOfPlaylist(playlists, playlistId);
        if (i < 0) return Status::failure(Code::MissingPlaylist);
        return fromPlaylist(index == -1
            ? playlists[i].append(*candidate.value)
            : playlists[i].insert(index, *candidate.value));
    });
}

Status ArtistPersonalState::removeRecording(
    const QString &playlistId, const QString &itemId) {
    if (!vinyl::PlaylistId::parse(playlistId).ok())
        return Status::failure(Code::InvalidPlaylistId);
    auto parsed = vinyl::PlaylistItemId::parse(itemId);
    if (!parsed.ok()) return Status::failure(Code::InvalidItemId);
    return apply([&](auto &, auto &playlists) {
        const int i = indexOfPlaylist(playlists, playlistId);
        if (i < 0) return Status::failure(Code::MissingPlaylist);
        return fromPlaylist(playlists[i].remove(*parsed.value));
    });
}

Status ArtistPersonalState::moveRecording(
    const QString &playlistId, const QString &itemId, int dest) {
    if (!vinyl::PlaylistId::parse(playlistId).ok())
        return Status::failure(Code::InvalidPlaylistId);
    auto parsed = vinyl::PlaylistItemId::parse(itemId);
    if (!parsed.ok()) return Status::failure(Code::InvalidItemId);
    return apply([&](auto &, auto &playlists) {
        const int i = indexOfPlaylist(playlists, playlistId);
        if (i < 0) return Status::failure(Code::MissingPlaylist);
        return fromPlaylist(playlists[i].move(*parsed.value, dest));
    });
}
} // namespace artist_personal
