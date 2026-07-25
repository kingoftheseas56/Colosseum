#pragma once

#include "Player2Types.h"
#include "player2/network/HttpMediaSource.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace Colosseum::Player2 {

class D3D11VideoPipeline;
class AudioPipeline;
class FrameScheduler;
class PlaybackClock;
class PacketQueue;

enum class DemuxEndReason
{
    EndOfFile,
    Cancelled,
    Failed
};

struct DemuxStreamInfo
{
    int index = -1;
    QString type;
    QString codec;
    QString language;
    QString title;
    bool isDefault = false;
    bool isForced = false;
};

struct DemuxChapter
{
    int index = -1;
    qint64 startUs = 0;
    qint64 endUs = 0;
    QString title;
};

struct DemuxMetadata
{
    qint64 durationUs = 0;
    int chapterCount = 0;
    QList<DemuxStreamInfo> streams;
    QList<DemuxChapter> chapters;
    QVariantMap tags;
};

// A timed subtitle product. Text cues carry styled/plain text; bitmap cues (PGS/DVD) carry the
// region rectangle and RGBA image bytes. Both carry the generation so stale cues never render.
struct SubtitleCue
{
    quint64 generation = 0;
    int streamIndex = -1;
    qint64 startUs = 0;
    qint64 endUs = 0;
    QString text;      // text subtitles (plain, ASS dialogue stripped to text)
    bool bitmap = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int canvasWidth = 0;   // bitmap cues: the video-frame size the x/y/w/h are composed against
    int canvasHeight = 0;
    QByteArray rgba;   // bitmap cues only, tightly packed width*height*4
};

struct DemuxPacketInfo
{
    int streamIndex = -1;
    qint64 ptsUs = 0;
    qint64 durationUs = 0;
    int size = 0;
    bool keyFrame = false;
};

class DemuxSession final : public QObject
{
    Q_OBJECT

public:
    explicit DemuxSession(QObject *parent = nullptr);
    ~DemuxSession() override;

    void open(const PlaybackRequest &request, quint64 generation);
    void cancel();
    // Transport commands. Each carries the already-advanced single generation; the worker performs
    // the flush/seek/track swap as its next action, so the generation barrier stays authoritative.
    void requestSeek(qint64 targetUs, quint64 generation, bool resumePlaying);
    void requestFrameStep(int frames, quint64 generation);
    void requestSelectAudioTrack(int streamIndex, quint64 generation);
    void requestSelectSubtitleTrack(int streamIndex); // does not flush A/V, so keeps the generation
    void requestNormalizationMode(int mode);
    void requestSpeed(double speed);
    void requestPause();
    void requestResume();
    // Live A/V offset (mpv audio-delay parity): shifts the audio buffer pts reported to the master
    // clock, so video is scheduled against the offset. Positive delays audio relative to video.
    void setAudioDelay(qint64 delayUs) noexcept;
    void setVideoPipeline(D3D11VideoPipeline *pipeline) noexcept;
    void setAudioPipeline(AudioPipeline *pipeline) noexcept;
    void setTiming(PlaybackClock *clock, FrameScheduler *scheduler) noexcept;
    bool running() const noexcept;

signals:
    void opened(quint64 generation, const DemuxMetadata &metadata);
    void packetObserved(quint64 generation, const DemuxPacketInfo &packet);
    void ended(quint64 generation, DemuxEndReason reason, const Player2Error &error);
    void seekCompleted(quint64 generation, double actualSeconds);
    void audioTrackChanged(quint64 generation, int streamIndex);
    void subtitleTrackChanged(quint64 generation, int streamIndex);
    void subtitleCue(quint64 generation, const SubtitleCue &cue);
    void audioNormalizationChanged(quint64 generation, int mode);
    // Honest network transport state for streamed sources, as a NetworkState value cast to int
    // (int keeps the queued cross-thread signal free of metatype registration).
    void networkStateChanged(quint64 generation, int state);

private:
    enum class CommandType {
        Seek, FrameStep, SelectAudioTrack, SelectSubtitleTrack, Normalization, Speed, Pause, Resume
    };
    struct Command
    {
        CommandType type = CommandType::Pause;
        qint64 targetUs = 0;
        int frames = 0;
        int streamIndex = -1;
        int normalizationMode = 0;
        double speed = 1.0;
        quint64 generation = 0;
        bool resumePlaying = true;
    };

    // Does this command reposition the stream (and therefore re-establish a known-good read
    // position by itself)? Only these may abandon a parked network read. Cutting a read off
    // mid-packet can leave the container's private cursor advanced past a sample, and NOTHING but a
    // seek puts that right — so a Pause or a track swap that interrupted a read would corrupt the
    // stream with no repair to follow. Both of these apply a seek.
    static constexpr bool repositions(CommandType type) noexcept
    {
        return type == CommandType::Seek || type == CommandType::FrameStep;
    }

    static int interrupt(void *opaque);
    // Custom AVIO callbacks routing FFmpeg's byte reads/seeks through an HttpMediaSource.
    static int avioRead(void *opaque, uint8_t *buffer, int size);
    static int64_t avioSeek(void *opaque, int64_t offset, int whence);
    void run(PlaybackRequest request, quint64 generation);
    void joinWorker();
    void enqueueCommand(const Command &command);
    void postOpened(quint64 generation, DemuxMetadata metadata);
    void postPacket(quint64 generation, DemuxPacketInfo packet);
    void postEnded(quint64 generation, DemuxEndReason reason, Player2Error error);
    void postSeekCompleted(quint64 generation, double actualSeconds);
    void postAudioTrackChanged(quint64 generation, int streamIndex);
    void postSubtitleTrackChanged(quint64 generation, int streamIndex);
    void postSubtitleCue(quint64 generation, SubtitleCue cue);
    void postAudioNormalizationChanged(quint64 generation, int mode);
    void postNetworkState(NetworkState state);

    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_running{false};
    std::atomic_bool m_commandPending{false};
    // How many QUEUED-but-unprocessed commands reposition the stream. This, not m_commandPending, is
    // what may abandon a parked network read: it is exact (incremented on enqueue, decremented the
    // instant the worker takes the command off the queue), so it can never be left true for a
    // command that was already handled — which would abort a read with no seek behind it to repair
    // the container. It is a COUNT rather than a flag so two seeks in flight cannot cancel to zero.
    std::atomic<int> m_pendingRepositions{0};
    std::atomic_bool m_paused{false};
    std::atomic<qint64> m_audioDelayUs{0};
    std::atomic<quint64> m_activeGeneration{0};
    std::atomic<D3D11VideoPipeline *> m_videoPipeline{nullptr};
    std::atomic<AudioPipeline *> m_audioPipeline{nullptr};
    std::atomic<PlaybackClock *> m_playbackClock{nullptr};
    std::atomic<FrameScheduler *> m_frameScheduler{nullptr};
    std::mutex m_commandMutex;
    std::condition_variable m_commandCv;
    std::deque<Command> m_commands;
    std::mutex m_workerMutex;
    std::thread m_worker;
    // Active streaming source (null for local files). Held as a shared_ptr so cancel() can unblock a
    // blocked AVIO read from the GUI thread while the worker still owns the object.
    std::mutex m_httpMutex;
    std::shared_ptr<HttpMediaSource> m_httpSource;
    // The run-local audio packet queue, exposed so cancel()/enqueueCommand() (called off the demux
    // thread) can interrupt a demux BLOCKED pushing into a full audio queue — otherwise a Pause/Seek/
    // Cancel arriving while the queue is full (e.g. audio worker back-pressured on a paused sink) would
    // never be serviced. Null except while a run owns the queue.
    std::mutex m_audioQueueMutex;
    PacketQueue *m_audioQueueForInterrupt = nullptr;
    // The video queue normally backpressures too; expose its run-local interrupt handle so
    // Pause/Seek/Cancel cannot strand the demux while waiting for consumer progress.
    std::mutex m_videoQueueMutex;
    PacketQueue *m_videoQueueForInterrupt = nullptr;
};

} // namespace Colosseum::Player2

Q_DECLARE_METATYPE(Colosseum::Player2::DemuxEndReason)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxStreamInfo)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxMetadata)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxPacketInfo)
Q_DECLARE_METATYPE(Colosseum::Player2::SubtitleCue)
