#include "VinylBoundaryValidation.h"
#include "VinylPlaylist.h"

#include <QChar>

namespace vinyl {

namespace {

bool validOpaqueId(const QString &text)
{
    if (text.isEmpty() || text.size() > 128 || text.trimmed() != text)
        return false;

    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

bool validPlaylistName(const QString &text)
{
    if (text.isEmpty() || text.size() > 256 || text.trimmed() != text)
        return false;
    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

} // namespace

QString playlistErrorCodeName(PlaylistErrorCode code)
{
    switch (code) {
    case PlaylistErrorCode::InvalidDisplaySnapshot:
        return QStringLiteral("invalid_display_snapshot");
    case PlaylistErrorCode::None:
        return QStringLiteral("none");
    case PlaylistErrorCode::InvalidPlaylistId:
        return QStringLiteral("invalid_playlist_id");
    case PlaylistErrorCode::InvalidPlaylistItemId:
        return QStringLiteral("invalid_playlist_item_id");
    case PlaylistErrorCode::InvalidPlaylistName:
        return QStringLiteral("invalid_playlist_name");
    case PlaylistErrorCode::InvalidCreatedAt:
        return QStringLiteral("invalid_created_at");
    case PlaylistErrorCode::InvalidAddedAt:
        return QStringLiteral("invalid_added_at");
    case PlaylistErrorCode::DuplicatePlaylistItemId:
        return QStringLiteral("duplicate_playlist_item_id");
    case PlaylistErrorCode::PlaylistItemNotFound:
        return QStringLiteral("playlist_item_not_found");
    case PlaylistErrorCode::IndexOutOfRange:
        return QStringLiteral("index_out_of_range");
    case PlaylistErrorCode::RecordingContextMismatch:
        return QStringLiteral("recording_context_mismatch");
    }
    return QStringLiteral("unknown");
}

PlaylistResult<PlaylistId> PlaylistId::parse(const QString &text)
{
    if (!validOpaqueId(text)) {
        return PlaylistResult<PlaylistId>::failure(
            PlaylistErrorCode::InvalidPlaylistId,
            QStringLiteral("playlistId"),
            QStringLiteral("Playlist ID must be a non-empty bounded opaque token."));
    }

    return PlaylistResult<PlaylistId>::success(PlaylistId(text));
}

PlaylistResult<PlaylistItemId> PlaylistItemId::parse(const QString &text)
{
    if (!validOpaqueId(text)) {
        return PlaylistResult<PlaylistItemId>::failure(
            PlaylistErrorCode::InvalidPlaylistItemId,
            QStringLiteral("playlistItemId"),
            QStringLiteral("Playlist item ID must be a non-empty bounded opaque token."));
    }

    return PlaylistResult<PlaylistItemId>::success(PlaylistItemId(text));
}

QString playlistProvenanceKindName(PlaylistProvenanceKind kind)
{
    switch (kind) {
    case PlaylistProvenanceKind::Manual:
        return QStringLiteral("manual");
    case PlaylistProvenanceKind::Generated:
        return QStringLiteral("generated");
    }
    return QStringLiteral("unknown");
}

PlaylistResult<PlaylistItem> PlaylistItem::create(
    PlaylistItemId itemId,
    RecordingMbid recording,
    qint64 addedAtMs,
    std::optional<ReleaseTrackCoordinate> releaseContext,
    std::optional<PersonalDisplaySnapshot> displaySnapshot)
{
    if (displaySnapshot.has_value() && !boundary::display(displaySnapshot->title, displaySnapshot->artistCredit))
        return PlaylistResult<PlaylistItem>::failure(PlaylistErrorCode::InvalidDisplaySnapshot, QStringLiteral("displaySnapshot"));
    if (addedAtMs <= 0) {
        return PlaylistResult<PlaylistItem>::failure(
            PlaylistErrorCode::InvalidAddedAt,
            QStringLiteral("addedAtMs"),
            QStringLiteral("Playlist item added-at time must be positive."));
    }
    if (releaseContext.has_value()
        && releaseContext->recordingMbid() != recording) {
        return PlaylistResult<PlaylistItem>::failure(
            PlaylistErrorCode::RecordingContextMismatch,
            QStringLiteral("releaseContext"),
            QStringLiteral("Release context Recording MBID must match the playlist item Recording MBID."));
    }

    return PlaylistResult<PlaylistItem>::success(
        PlaylistItem(std::move(itemId),
                     std::move(recording),
                     addedAtMs,
                     std::move(releaseContext),
                     std::move(displaySnapshot)));
}

PlaylistResult<Playlist> Playlist::create(
    ProfileScopeId profileId,
    PlaylistId playlistId,
    QString name,
    qint64 createdAtMs,
    PlaylistProvenanceKind provenance)
{
    if (!validPlaylistName(name)) {
        return PlaylistResult<Playlist>::failure(
            PlaylistErrorCode::InvalidPlaylistName,
            QStringLiteral("name"),
            QStringLiteral("Playlist name must be non-empty, bounded, trimmed, and contain no control characters."));
    }
    if (createdAtMs <= 0) {
        return PlaylistResult<Playlist>::failure(
            PlaylistErrorCode::InvalidCreatedAt,
            QStringLiteral("createdAtMs"),
            QStringLiteral("Playlist created-at time must be positive."));
    }
    return PlaylistResult<Playlist>::success(
        Playlist(std::move(profileId),
                 std::move(playlistId),
                 std::move(name),
                 createdAtMs,
                 provenance));
}

const PlaylistItem *Playlist::find(const PlaylistItemId &id) const
{
    const int index = indexOf(id);
    if (index < 0)
        return nullptr;
    return &m_items.at(index);
}

PlaylistMutationStatus Playlist::rename(QString name)
{
    if (!validPlaylistName(name)) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::InvalidPlaylistName,
            QStringLiteral("name"));
    }
    if (name == m_name)
        return PlaylistMutationStatus::success(false);
    m_name = std::move(name);
    return PlaylistMutationStatus::success(true);
}

PlaylistMutationStatus Playlist::append(PlaylistItem item)
{
    return insert(m_items.size(), std::move(item));
}

PlaylistMutationStatus Playlist::insert(int index, PlaylistItem item)
{
    if (index < 0 || index > m_items.size()) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::IndexOutOfRange,
            QStringLiteral("index"),
            QStringLiteral("Playlist insertion index must be within 0..size."));
    }

    const PlaylistMutationStatus validation = validateNewItem(item);
    if (!validation.ok)
        return validation;

    m_items.insert(index, std::move(item));
    return PlaylistMutationStatus::success(true);
}

PlaylistMutationStatus Playlist::remove(const PlaylistItemId &id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::PlaylistItemNotFound,
            QStringLiteral("playlistItemId"),
            QStringLiteral("Playlist item does not exist."));
    }

    m_items.removeAt(index);
    return PlaylistMutationStatus::success(true);
}

PlaylistMutationStatus Playlist::move(const PlaylistItemId &id, int destinationIndex)
{
    const int sourceIndex = indexOf(id);
    if (sourceIndex < 0) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::PlaylistItemNotFound,
            QStringLiteral("playlistItemId"),
            QStringLiteral("Playlist item does not exist."));
    }

    if (destinationIndex < 0 || destinationIndex >= m_items.size()) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::IndexOutOfRange,
            QStringLiteral("destinationIndex"),
            QStringLiteral("Playlist move destination must name an index in the final order."));
    }

    if (sourceIndex == destinationIndex)
        return PlaylistMutationStatus::success(false);

    PlaylistItem item = m_items.takeAt(sourceIndex);
    m_items.insert(destinationIndex, std::move(item));
    return PlaylistMutationStatus::success(true);
}

int Playlist::indexOf(const PlaylistItemId &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).itemId() == id)
            return i;
    }
    return -1;
}

bool Playlist::containsId(const PlaylistItemId &id) const
{
    return indexOf(id) >= 0;
}

PlaylistMutationStatus Playlist::validateNewItem(const PlaylistItem &item) const
{
    if (item.addedAtMs() < m_createdAtMs) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::InvalidAddedAt,
            QStringLiteral("addedAtMs"),
            QStringLiteral("Playlist item cannot predate its playlist."));
    }

    if (containsId(item.itemId())) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::DuplicatePlaylistItemId,
            QStringLiteral("playlistItemId"),
            QStringLiteral("Playlist item IDs must be unique within one playlist."));
    }

    if (item.releaseContext().has_value()
        && item.releaseContext()->recordingMbid() != item.recordingMbid()) {
        return PlaylistMutationStatus::failure(
            PlaylistErrorCode::RecordingContextMismatch,
            QStringLiteral("releaseContext"),
            QStringLiteral("Release context Recording MBID must match the playlist item Recording MBID."));
    }

    return PlaylistMutationStatus::success(false);
}

} // namespace vinyl
