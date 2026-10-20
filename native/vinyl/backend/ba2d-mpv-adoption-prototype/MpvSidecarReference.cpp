#include "MpvSidecarReference.h"

#include <mpv/client.h>
#include <limits>

namespace vinyl {

std::optional<qint64> currentMpvPlaylistEntryId(mpv_handle *client)
{
    if (!client)
        return std::nullopt;

    mpv_node node{};
    if (mpv_get_property(client, "playlist", MPV_FORMAT_NODE, &node) < 0)
        return std::nullopt;

    std::optional<qint64> onlyId;
    int seen = 0;

    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list)
                continue;

            std::optional<qint64> id;
            bool current = false;
            for (int j = 0; j < entry.u.list->num; ++j) {
                const char *key = entry.u.list->keys[j];
                if (!key)
                    continue;
                const mpv_node &value = entry.u.list->values[j];
                const QString name = QString::fromUtf8(key);
                if (name == QLatin1String("id")
                    && value.format == MPV_FORMAT_INT64) {
                    id = value.u.int64;
                } else if (name == QLatin1String("current")
                           && value.format == MPV_FORMAT_FLAG) {
                    current = value.u.flag != 0;
                }
            }

            if (!id.has_value())
                continue;
            ++seen;
            onlyId = id;
            if (current) {
                mpv_free_node_contents(&node);
                return id;
            }
        }
    }

    mpv_free_node_contents(&node);
    return seen == 1 ? onlyId : std::nullopt;
}

bool MpvPlaylistEntryWorkLedger::bind(
    qint64 playlistEntryId,
    PlaybackWorkToken token)
{
    if (playlistEntryId < 0)
        return false;

    for (Entry &entry : m_entries) {
        if (entry.playlistEntryId == playlistEntryId) {
            entry.token = std::move(token);
            return true;
        }
    }
    m_entries.append(Entry(playlistEntryId, std::move(token)));
    return true;
}

std::optional<PlaybackWorkToken> MpvPlaylistEntryWorkLedger::tokenFor(
    qint64 playlistEntryId) const
{
    for (const Entry &entry : m_entries) {
        if (entry.playlistEntryId == playlistEntryId)
            return entry.token;
    }
    return std::nullopt;
}

bool MpvPlaylistEntryWorkLedger::forget(qint64 playlistEntryId)
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).playlistEntryId == playlistEntryId) {
            m_entries.removeAt(i);
            return true;
        }
    }
    return false;
}

void SerializedMpvSeekState::bindWork(PlaybackWorkToken token)
{
    if (m_boundWork.has_value() && *m_boundWork == token) return;
    if (token.generation() <= m_highestBoundGeneration) return;
    m_highestBoundGeneration = token.generation();
    m_boundWork = std::move(token);
    m_highestSequence = 0;
    m_deadlineMs = 0;
    m_active.reset();
    m_queued.reset();
    m_activeSawSeekStart = false;
 
}

void SerializedMpvSeekState::clear() noexcept
{
    m_boundWork.reset();
    m_active.reset();
    m_queued.reset();
    m_activeSawSeekStart = false;
    m_highestSequence = 0;
    m_deadlineMs = 0;
}

bool SerializedMpvSeekState::owns(const PlaybackWorkToken &token) const noexcept
{
    return m_boundWork.has_value() && *m_boundWork == token;
}

MpvSeekSubmitResult SerializedMpvSeekState::submit(
    PlaybackSeekCommand command, qint64 monotonicMs)
{
    if (!owns(command.workToken()) || command.targetMs() < 0 || command.sequence() <= m_highestSequence
        || monotonicMs < 0 || monotonicMs > std::numeric_limits<qint64>::max() - 10000)
        return {};

    m_highestSequence = command.sequence();
    if (!m_active.has_value()) {
        m_active = command;
        m_deadlineMs = monotonicMs + 10000;
        m_activeSawSeekStart = false;
        return {MpvSeekSubmitDisposition::DispatchNow, std::move(command)};
    }

    m_queued = std::move(command);
    return {MpvSeekSubmitDisposition::QueuedLatest, std::nullopt};
}

bool SerializedMpvSeekState::observeSeekStart(
    const PlaybackWorkToken &token) noexcept
{
    if (!owns(token)
        || !m_active.has_value()
        || m_active->workToken() != token) {
        return false;
    }

    m_activeSawSeekStart = true;
    return true;
}

std::optional<MpvSeekAdvance> SerializedMpvSeekState::observePlaybackRestart(
    const PlaybackWorkToken &token,
    std::optional<qint64> observedTrackPositionMs,
    qint64 monotonicMs)
{
    if (!owns(token)
        || !m_active.has_value()
        || m_active->workToken() != token
        || !m_activeSawSeekStart
        || !observedTrackPositionMs.has_value() || *observedTrackPositionMs < 0
        || monotonicMs < 0 || monotonicMs >= m_deadlineMs
        || monotonicMs > std::numeric_limits<qint64>::max() - 10000) {
        return std::nullopt;
    }

    const qint64 observed = *observedTrackPositionMs;
    const qint64 target = m_active->targetMs();
    // Restart events also occur during initial load; require actual settled target evidence.
    if (observed > target ? observed - target > 300 : target - observed > 300)
        return std::nullopt;
    PlaybackSeekCommand completed = *m_active;
    m_active.reset();
    m_activeSawSeekStart = false;

    std::optional<PlaybackSeekCommand> next;
    if (m_queued.has_value() && m_queued->workToken() == token) {
        next = *m_queued;
        m_queued.reset();
        m_active = *next;
        m_deadlineMs = monotonicMs + 10000;
    } else {
        m_queued.reset();
    }

    return MpvSeekAdvance(std::move(completed), std::move(next));
}

std::optional<PlaybackSeekCommand> SerializedMpvSeekState::cancelWork(const PlaybackWorkToken &token)
{
    if (!owns(token)) return std::nullopt;
    auto latest = m_queued.has_value() ? m_queued : m_active;
    clear();
    return latest;
}
std::optional<PlaybackSeekCommand> SerializedMpvSeekState::failActive(const PlaybackSeekCommand &command)
{
    if (!owns(command.workToken()) || !m_active.has_value()
        || command.sequence() != m_active->sequence()) return std::nullopt;
    return cancelWork(command.workToken());
}
std::optional<PlaybackSeekCommand> SerializedMpvSeekState::expire(qint64 monotonicMs)
{
    if (!m_active.has_value() || monotonicMs < m_deadlineMs) return std::nullopt;
    return cancelWork(m_active->workToken());
}

} // namespace vinyl
