#pragma once

#include "VinylLibrary.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class PlaylistErrorCode {
    None,
    InvalidPlaylistId,
    InvalidPlaylistItemId,
    InvalidPlaylistName,
    InvalidCreatedAt,
    InvalidAddedAt,
    DuplicatePlaylistItemId,
    PlaylistItemNotFound,
    IndexOutOfRange,
    RecordingContextMismatch,
    InvalidDisplaySnapshot,
};

struct PlaylistError final {
    PlaylistErrorCode code = PlaylistErrorCode::None;
    QString field;
    QString detail;
};

QString playlistErrorCodeName(PlaylistErrorCode code);

template <typename T>
struct PlaylistResult final {
    std::optional<T> value;
    PlaylistError error;

    bool ok() const noexcept { return value.has_value(); }

    static PlaylistResult success(T value)
    {
        PlaylistResult out;
        out.value = std::move(value);
        return out;
    }

    static PlaylistResult failure(PlaylistErrorCode code,
                                  QString field,
                                  QString detail = QString())
    {
        PlaylistResult out;
        out.error = PlaylistError{code, std::move(field), std::move(detail)};
        return out;
    }
};

struct PlaylistMutationStatus final {
    bool ok = true;
    bool changed = false;
    PlaylistError error;

    static PlaylistMutationStatus success(bool changed = true)
    {
        PlaylistMutationStatus status;
        status.changed = changed;
        return status;
    }

    static PlaylistMutationStatus failure(PlaylistErrorCode code,
                                          QString field,
                                          QString detail = QString())
    {
        PlaylistMutationStatus status;
        status.ok = false;
        status.error = PlaylistError{code, std::move(field), std::move(detail)};
        return status;
    }
};

class PlaylistId final
{
public:
    static PlaylistResult<PlaylistId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const PlaylistId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const PlaylistId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit PlaylistId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class PlaylistItemId final
{
public:
    static PlaylistResult<PlaylistItemId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const PlaylistItemId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const PlaylistItemId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit PlaylistItemId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class PlaylistProvenanceKind {
    Manual,
    Generated,
};

QString playlistProvenanceKindName(PlaylistProvenanceKind kind);

class PlaylistItem final
{
public:
    static PlaylistResult<PlaylistItem> create(
        PlaylistItemId itemId,
        RecordingMbid recording,
        qint64 addedAtMs,
        std::optional<ReleaseTrackCoordinate> releaseContext = std::nullopt,
        std::optional<PersonalDisplaySnapshot> displaySnapshot = std::nullopt);

    const PlaylistItemId &itemId() const noexcept { return m_itemId; }
    const RecordingMbid &recordingMbid() const noexcept { return m_recording; }
    qint64 addedAtMs() const noexcept { return m_addedAtMs; }
    const std::optional<ReleaseTrackCoordinate> &releaseContext() const noexcept
    {
        return m_releaseContext;
    }
    const std::optional<PersonalDisplaySnapshot> &displaySnapshot() const noexcept
    {
        return m_displaySnapshot;
    }

private:
    PlaylistItem(PlaylistItemId itemId,
                 RecordingMbid recording,
                 qint64 addedAtMs,
                 std::optional<ReleaseTrackCoordinate> releaseContext,
                 std::optional<PersonalDisplaySnapshot> displaySnapshot)
        : m_itemId(std::move(itemId))
        , m_recording(std::move(recording))
        , m_addedAtMs(addedAtMs)
        , m_releaseContext(std::move(releaseContext))
        , m_displaySnapshot(std::move(displaySnapshot))
    {
    }

    PlaylistItemId m_itemId;
    RecordingMbid m_recording;
    qint64 m_addedAtMs = 0;
    std::optional<ReleaseTrackCoordinate> m_releaseContext;
    std::optional<PersonalDisplaySnapshot> m_displaySnapshot;
};

class Playlist final
{
public:
    static PlaylistResult<Playlist> create(
        ProfileScopeId profileId,
        PlaylistId playlistId,
        QString name,
        qint64 createdAtMs,
        PlaylistProvenanceKind provenance);

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const PlaylistId &playlistId() const noexcept { return m_playlistId; }
    const QString &name() const noexcept { return m_name; }
    qint64 createdAtMs() const noexcept { return m_createdAtMs; }
    PlaylistProvenanceKind provenance() const noexcept { return m_provenance; }

    int size() const noexcept { return m_items.size(); }
    bool empty() const noexcept { return m_items.isEmpty(); }
    const QVector<PlaylistItem> &items() const noexcept { return m_items; }

    const PlaylistItem *find(const PlaylistItemId &id) const;

    PlaylistMutationStatus rename(QString name);
    PlaylistMutationStatus append(PlaylistItem item);
    PlaylistMutationStatus insert(int index, PlaylistItem item);
    PlaylistMutationStatus remove(const PlaylistItemId &id);
    PlaylistMutationStatus move(const PlaylistItemId &id, int destinationIndex);

private:
    Playlist(ProfileScopeId profileId,
             PlaylistId playlistId,
             QString name,
             qint64 createdAtMs,
             PlaylistProvenanceKind provenance)
        : m_profileId(std::move(profileId))
        , m_playlistId(std::move(playlistId))
        , m_name(std::move(name))
        , m_createdAtMs(createdAtMs)
        , m_provenance(provenance)
    {
    }

    int indexOf(const PlaylistItemId &id) const;
    bool containsId(const PlaylistItemId &id) const;
    PlaylistMutationStatus validateNewItem(const PlaylistItem &item) const;

    ProfileScopeId m_profileId;
    PlaylistId m_playlistId;
    QString m_name;
    qint64 m_createdAtMs = 0;
    PlaylistProvenanceKind m_provenance = PlaylistProvenanceKind::Manual;
    QVector<PlaylistItem> m_items;
};

} // namespace vinyl
