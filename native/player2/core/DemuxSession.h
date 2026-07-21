#pragma once

#include "Player2Types.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <atomic>
#include <mutex>
#include <thread>

namespace Colosseum::Player2 {

class D3D11VideoPipeline;
class AudioPipeline;

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
    void setVideoPipeline(D3D11VideoPipeline *pipeline) noexcept;
    void setAudioPipeline(AudioPipeline *pipeline) noexcept;
    bool running() const noexcept;

signals:
    void opened(quint64 generation, const DemuxMetadata &metadata);
    void packetObserved(quint64 generation, const DemuxPacketInfo &packet);
    void ended(quint64 generation, DemuxEndReason reason, const Player2Error &error);

private:
    static int interrupt(void *opaque);
    void run(PlaybackRequest request, quint64 generation);
    void joinWorker();
    void postOpened(quint64 generation, DemuxMetadata metadata);
    void postPacket(quint64 generation, DemuxPacketInfo packet);
    void postEnded(quint64 generation, DemuxEndReason reason, Player2Error error);

    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_running{false};
    std::atomic<quint64> m_activeGeneration{0};
    std::atomic<D3D11VideoPipeline *> m_videoPipeline{nullptr};
    std::atomic<AudioPipeline *> m_audioPipeline{nullptr};
    std::mutex m_workerMutex;
    std::thread m_worker;
};

} // namespace Colosseum::Player2

Q_DECLARE_METATYPE(Colosseum::Player2::DemuxEndReason)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxStreamInfo)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxMetadata)
Q_DECLARE_METATYPE(Colosseum::Player2::DemuxPacketInfo)
