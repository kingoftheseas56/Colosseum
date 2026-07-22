#pragma once

#include "Player2Types.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Colosseum::Player2 {

class D3D11VideoPipeline;
class AudioPipeline;
class FrameScheduler;
class PlaybackClock;

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
};

struct DemuxMetadata
{
    qint64 durationUs = 0;
    int chapterCount = 0;
    QList<DemuxStreamInfo> streams;
    QVariantMap tags;
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
    void requestNormalizationMode(int mode);
    void requestPause();
    void requestResume();
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
    void audioNormalizationChanged(quint64 generation, int mode);

private:
    enum class CommandType { Seek, FrameStep, SelectAudioTrack, Normalization, Pause, Resume };
    struct Command
    {
        CommandType type = CommandType::Pause;
        qint64 targetUs = 0;
        int frames = 0;
        int streamIndex = -1;
        int normalizationMode = 0;
        quint64 generation = 0;
        bool resumePlaying = true;
    };

    static int interrupt(void *opaque);
    void run(PlaybackRequest request, quint64 generation);
    void joinWorker();
    void enqueueCommand(const Command &command);
    void postOpened(quint64 generation, DemuxMetadata metadata);
    void postPacket(quint64 generation, DemuxPacketInfo packet);
    void postEnded(quint64 generation, DemuxEndReason reason, Player2Error error);
    void postSeekCompleted(quint64 generation, double actualSeconds);
    void postAudioTrackChanged(quint64 generation, int streamIndex);
    void postAudioNormalizationChanged(quint64 generation, int mode);

    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_running{false};
    std::atomic_bool m_commandPending{false};
    std::atomic_bool m_paused{false};
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
};

} // namespace Colosseum::Player2

Q_DECLARE_METATYPE(Colosseum::Player2::DemuxEndReason)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxStreamInfo)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxMetadata)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxPacketInfo)
