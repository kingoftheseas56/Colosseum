#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QtTypes>

#include <deque>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace Colosseum::Player2 {

struct AudioFormat
{
    int sampleRate = 48'000;
    int channels = 2;
};

struct AudioBuffer
{
    AudioFormat format;
    QByteArray bytes;
    int frameCount = 0;
    qint64 ptsUs = 0;
};

struct AudioClockSnapshot
{
    qint64 mediaPositionUs = 0;
    qint64 qpcTimestamp = 0;
    bool valid = false;
};

class IAudioSink
{
public:
    virtual ~IAudioSink() = default;
    virtual bool open(const AudioFormat &format, QString *error) = 0;
    virtual int write(const AudioBuffer &buffer, quint64 generation, QString *error) = 0;
    virtual AudioClockSnapshot clock() const = 0;
    virtual void flush(quint64 generation) = 0;
    // Real transport pause: suspend/resume the endpoint. Default no-op keeps fake sinks simple.
    virtual void setPaused(bool paused) { Q_UNUSED(paused); }
    virtual void setVolume(float linear) = 0;
    virtual void setMuted(bool muted) = 0;
    virtual int queueDepthFrames() const = 0;
    virtual QString deviceName() const = 0;
    virtual quint64 underrunCount() const = 0;
};

class AudioBufferQueue
{
public:
    explicit AudioBufferQueue(int maximumFrames);

    bool enqueue(const AudioBuffer &buffer, quint64 generation);
    bool enqueueBlocking(const AudioBuffer &buffer, quint64 generation);
    int read(float *destination, int requestedFrames, int channels);
    void flush(quint64 generation);
    int depthFrames() const;
    quint64 generation() const;
    quint64 underrunCount() const;
    qint64 lastReadMediaPositionUs() const;

private:
    struct Entry
    {
        AudioBuffer buffer;
        int offsetFrames = 0;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_capacityChanged;
    std::deque<Entry> m_entries;
    int m_maximumFrames = 0;
    int m_depthFrames = 0;
    quint64 m_generation = 0;
    quint64 m_underruns = 0;
    qint64 m_lastReadMediaPositionUs = 0;
};

class WASAPIAudioSink final : public IAudioSink
{
public:
    WASAPIAudioSink();
    ~WASAPIAudioSink() override;

    bool open(const AudioFormat &format, QString *error) override;
    int write(const AudioBuffer &buffer, quint64 generation, QString *error) override;
    AudioClockSnapshot clock() const override;
    void flush(quint64 generation) override;
    void setPaused(bool paused) override;
    void setVolume(float linear) override;
    void setMuted(bool muted) override;
    int queueDepthFrames() const override;
    QString deviceName() const override;
    quint64 underrunCount() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Colosseum::Player2
