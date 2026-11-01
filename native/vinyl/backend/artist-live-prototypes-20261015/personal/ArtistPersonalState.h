#pragma once

#include "VinylPersonalStatePersistence.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>
#include <optional>

namespace artist_personal {

// Reference backend only. Production adoption must use BA5/Collection ownership.
enum class Code {
    Ok, NotOpen, InvalidProfile, InvalidArtistMbid, InvalidRecordingMbid,
    InvalidPlaylistId, InvalidItemId, InvalidName, InvalidTimestamp,
    InvalidSnapshot, DuplicatePlaylist, MissingPlaylist, MissingItem,
    DuplicateItem, IndexOutOfRange, Corrupt, UnsupportedVersion,
    ProfileMismatch, IoError, ConcurrentChange, InvalidState
};
QString codeName(Code code);
struct Status {
    Code code = Code::Ok;
    bool changed = false;
    QString detail;
    bool ok() const { return code == Code::Ok; }
    static Status success(bool changed = false) { return {Code::Ok, changed, {}}; }
    static Status failure(Code code, QString detail = {}) {
        return {code, false, std::move(detail)};
    }
};
struct BytesSnapshot {
    bool exists = false;
    QByteArray content;
    bool operator==(const BytesSnapshot &other) const {
        return exists == other.exists && content == other.content;
    }
};
struct Load {
    Status status;
    BytesSnapshot snapshot;
};

// Atomic compare-before-write byte-store seam. Adopters replace with a
// BA5/Collection transaction, not with a second production state database.
class PersonalBytesPort {
public:
    virtual ~PersonalBytesPort() = default;
    virtual Load read(const QString &profileScopeId) = 0;
    virtual Status commit(const QString &profileScopeId,
                          const BytesSnapshot &expected, const QByteArray &next) = 0;
};
class AtomicFilePort final : public PersonalBytesPort {
public:
    explicit AtomicFilePort(QString testRoot);
    QString filePath(const QString &profileScopeId) const;
    Load read(const QString &profileScopeId) override;
    Status commit(const QString &profileScopeId,
                  const BytesSnapshot &expected, const QByteArray &next) override;
private:
    QString m_root;
};

class ArtistPersonalState final {
public:
    explicit ArtistPersonalState(PersonalBytesPort &port) : m_port(port) {}
    Status open(const QString &profileScopeId);
    bool ready() const { return m_profile.has_value() && m_library.has_value(); }
    int loadedVersion() const { return m_loadedVersion; }

    const vinyl::LibraryEntry *findSavedArtist(const QString &artistMbid) const;
    bool isSavedArtist(const QString &artistMbid) const {
        return findSavedArtist(artistMbid) != nullptr;
    }
    QVector<vinyl::Playlist> playlistsByCreation() const;
    const vinyl::Playlist *findPlaylist(const QString &playlistId) const;

    Status saveArtist(const QString &artistMbid, qint64 savedAtMs,
                      std::optional<vinyl::PersonalDisplaySnapshot> display = std::nullopt);
    Status unsaveArtist(const QString &artistMbid);
    Status toggleArtist(const QString &artistMbid, qint64 savedAtMs,
                        std::optional<vinyl::PersonalDisplaySnapshot> display = std::nullopt);

    Status createPlaylist(const QString &playlistId, const QString &name, qint64 createdAtMs);
    Status renamePlaylist(const QString &playlistId, const QString &name);
    Status deletePlaylist(const QString &playlistId);
    Status addRecording(const QString &playlistId, const QString &itemId,
                        const QString &recordingMbid, qint64 addedAtMs,
                        int insertionIndex = -1,
                        std::optional<vinyl::PersonalDisplaySnapshot> display = std::nullopt);
    Status removeRecording(const QString &playlistId, const QString &itemId);
    Status moveRecording(const QString &playlistId, const QString &itemId,
                         int destinationIndex);

private:
    Status apply(const std::function<Status(vinyl::VinylLibrary &,
                                            QVector<vinyl::Playlist> &)> &fn);
    int indexOfPlaylist(const QVector<vinyl::Playlist> &playlists,
                        const QString &playlistId) const;
    PersonalBytesPort &m_port;
    std::optional<vinyl::ProfileScopeId> m_profile;
    std::optional<vinyl::VinylLibrary> m_library;
    QVector<vinyl::Playlist> m_playlists;
    BytesSnapshot m_disk;
    int m_loadedVersion = 0;
};

} // namespace artist_personal
