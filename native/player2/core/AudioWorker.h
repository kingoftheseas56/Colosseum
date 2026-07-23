#pragma once

#include "PacketQueue.h"

#include <QtCore/QtTypes>
#include <QtCore/QString>

#include <atomic>
#include <thread>

struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVRational;

namespace Colosseum::Player2 {

class AudioPipeline;

// The audio decode worker — the THIRD thread of the read-ahead engine. It exclusively owns the audio
// decoder, its frame, and every mutable AudioPipeline decode operation (submit / flush filters /
// normalization). The demux thread only READS packets and PUSHES them into `queue()`; blocking on that
// push is what paces the demux to real-time audio (so video can drop without starving audio, and the
// demux command loop is never trapped — see PacketQueue::pushInterruptible). Because the decoder lives
// entirely on this thread, the demux never touches it: a seek is a generation change on the packets
// (the worker flushes when the popped generation moves), a track switch is a posted reconfigure the
// worker applies, and a normalization change is a posted mode the worker applies. This removes the
// data races Codex flagged (demux flushing/reallocating a decoder another thread is using).
//
// The worker calls back into a Host for the small amount of shared playback state it must consult
// (pause, seek-scan target, master arbitration) and to report a seek landing — exactly the couplings
// the old inline decode had, now behind a narrow, thread-safe seam.
class AudioWorker
{
public:
    // Shared playback state the decode loop consults. All methods are called ON the worker thread and
    // must be thread-safe (the implementations back onto the demux session's atomics).
    class Host
    {
    public:
        virtual ~Host() = default;
        virtual bool cancelled() const = 0;
        virtual bool paused() const = 0;
        virtual bool commandPending() const = 0;
        virtual qint64 audioDelayUs() const = 0;
        virtual bool audioIsMaster() const = 0;
        virtual bool decodingToTarget() const = 0;
        virtual qint64 seekTargetUs() const = 0;
        // The last decoded audio pts advanced to `ptsUs` (only meaningful while audio is master).
        virtual void onAudioPositionAdvanced(qint64 ptsUs) = 0;
        // Audio (the master) decoded to at/after the seek target: land the seek at `ptsUs`.
        virtual void onSeekLanded(qint64 ptsUs) = 0;
    };

    // `pipeline` is shared (the video thread reads its sink clock); this worker owns its DECODE path.
    // `codecpar`/`timeBase` describe the initially selected audio stream. `queue` is the audio packet
    // FIFO the demux pushes into. `host` supplies shared playback state; all outlive the worker.
    AudioWorker(AudioPipeline *pipeline, const AVCodecParameters *codecpar, AVRational timeBase,
                PacketQueue *queue, Host *host, quint64 generation);
    ~AudioWorker();

    AudioWorker(const AudioWorker &) = delete;
    AudioWorker &operator=(const AudioWorker &) = delete;

    // Build the decoder + frame and open the pipeline, then spawn the decode thread. Returns false and
    // sets *error on setup failure (no thread is spawned).
    bool start(QString *error);

    // Cancel the packet queue (unblocks the thread) and join it. Safe to call more than once.
    void stop();

    // Post a normalization mode; the worker applies it on its own thread (it owns the normalizer).
    void setNormalizationMode(int mode);

    // Post an audio-track switch: the worker reconfigures its decoder from `codecpar`/`timeBase` on
    // its own thread when it next sees the matching generation, so the demux never touches the decoder.
    void reconfigure(const AVCodecParameters *codecpar, AVRational timeBase, quint64 generation);

    bool failed() const { return m_failed.load(std::memory_order_acquire); }
    QString failureMessage() const { return m_failureMessage; }
    // True once the worker has drained its decoder + pipeline tail after end-of-stream (the demux
    // waits for this before publishing EndOfFile so audio is never cut off mid-tail).
    bool reachedEnd() const { return m_reachedEnd.load(std::memory_order_acquire); }

private:
    void run();
    bool receiveFrames(quint64 generation); // pull decoded frames → pipeline; false on hard failure

    AudioPipeline *m_pipeline;
    const AVCodecParameters *m_codecpar;
    qint64 m_timeBaseNum;
    qint64 m_timeBaseDen;
    PacketQueue *m_queue;
    Host *m_host;
    quint64 m_generation;

    AVCodecContext *m_decoder = nullptr;
    AVFrame *m_frame = nullptr;
    std::thread m_thread;

    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_reachedEnd{false};
    QString m_failureMessage;

    // Posted controls the worker applies on its own thread (owns the decoder + normalizer).
    std::atomic<int> m_pendingNormalization{-1};                  // -1 = none
    std::atomic<const AVCodecParameters *> m_pendingReconfig{nullptr};
    std::atomic<qint64> m_pendingReconfigTimeBase{0};             // packed num<<32 | den
    std::atomic<quint64> m_pendingReconfigGeneration{0};
};

} // namespace Colosseum::Player2
