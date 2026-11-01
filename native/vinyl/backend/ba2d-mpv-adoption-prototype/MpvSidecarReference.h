#pragma once

#include "PlaybackEngineAdapter.h"

#include <QVector>
#include <QtGlobal>

#include <optional>
#include <utility>

struct mpv_handle;

namespace vinyl {

std::optional<qint64> currentMpvPlaylistEntryId(mpv_handle *client);

class MpvPlaylistEntryWorkLedger final
{
public:
    bool bind(qint64 playlistEntryId, PlaybackWorkToken token);
    std::optional<PlaybackWorkToken> tokenFor(qint64 playlistEntryId) const;
    bool forget(qint64 playlistEntryId);
    void clear() noexcept { m_entries.clear(); }
    int size() const noexcept { return m_entries.size(); }

private:
    struct Entry final {
        qint64 playlistEntryId = -1;
        PlaybackWorkToken token;

        Entry(qint64 id, PlaybackWorkToken value)
            : playlistEntryId(id), token(std::move(value)) {}
    };

    QVector<Entry> m_entries;
};

enum class MpvSeekSubmitDisposition {
    DispatchNow,
    QueuedLatest,
    RejectedStale,
};

struct MpvSeekSubmitResult final {
    MpvSeekSubmitDisposition disposition = MpvSeekSubmitDisposition::RejectedStale;
    std::optional<PlaybackSeekCommand> dispatch;
};

struct MpvSeekAdvance final {
    PlaybackSeekCommand completed;
    std::optional<PlaybackSeekCommand> nextPhysical;

    MpvSeekAdvance(PlaybackSeekCommand completedCommand,
                   std::optional<PlaybackSeekCommand> nextCommand)
        : completed(std::move(completedCommand))
        , nextPhysical(std::move(nextCommand)) {}
};

class SerializedMpvSeekState final
{
public:
    void bindWork(PlaybackWorkToken token);
    void clear() noexcept;

    MpvSeekSubmitResult submit(PlaybackSeekCommand command, qint64 monotonicMs = 0);
    // Aborts quarantine this binding: a fresh work binding is required before more physical seeks.
    std::optional<PlaybackSeekCommand> cancelWork(const PlaybackWorkToken &token);
    std::optional<PlaybackSeekCommand> failActive(const PlaybackSeekCommand &command);
    std::optional<PlaybackSeekCommand> expire(qint64 monotonicMs);
    bool observeSeekStart(const PlaybackWorkToken &token) noexcept;
    std::optional<MpvSeekAdvance> observePlaybackRestart(
        const PlaybackWorkToken &token,
        std::optional<qint64> observedTrackPositionMs = std::nullopt,
        qint64 monotonicMs = 0);

    bool hasPhysicalSeekInFlight() const noexcept { return m_active.has_value(); }
    bool hasQueuedSeek() const noexcept { return m_queued.has_value(); }
    const std::optional<PlaybackWorkToken> &boundWork() const noexcept
    {
        return m_boundWork;
    }

private:
    bool owns(const PlaybackWorkToken &token) const noexcept;

    std::optional<PlaybackWorkToken> m_boundWork;
    std::optional<PlaybackSeekCommand> m_active;
    std::optional<PlaybackSeekCommand> m_queued;
    bool m_activeSawSeekStart = false;
    quint64 m_highestSequence = 0;
    qint64 m_deadlineMs = 0;
    quint64 m_highestBoundGeneration = 0;
};

} // namespace vinyl

