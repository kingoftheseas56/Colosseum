#include "WASAPIAudioSink.h"

#include <QtCore/QStringLiteral>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <thread>

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {

AudioBufferQueue::AudioBufferQueue(int maximumFrames)
    : m_maximumFrames(std::max(1, maximumFrames))
{
}

bool AudioBufferQueue::enqueue(const AudioBuffer &buffer, quint64 generation)
{
    std::scoped_lock lock(m_mutex);
    if (generation != m_generation || buffer.frameCount <= 0 ||
        buffer.format.channels <= 0 ||
        buffer.bytes.size() < buffer.frameCount * buffer.format.channels *
                                  static_cast<int>(sizeof(float)) ||
        m_depthFrames + buffer.frameCount > m_maximumFrames) {
        return false;
    }
    m_entries.push_back(Entry{buffer, 0});
    m_depthFrames += buffer.frameCount;
    return true;
}

bool AudioBufferQueue::enqueueBlocking(const AudioBuffer &buffer, quint64 generation)
{
    if (buffer.frameCount <= 0 || buffer.frameCount > m_maximumFrames ||
        buffer.format.channels <= 0 ||
        buffer.bytes.size() < buffer.frameCount * buffer.format.channels *
                                  static_cast<int>(sizeof(float))) {
        return false;
    }
    std::unique_lock lock(m_mutex);
    m_capacityChanged.wait(lock, [this, &buffer, generation] {
        return generation != m_generation ||
               m_depthFrames + buffer.frameCount <= m_maximumFrames;
    });
    if (generation != m_generation)
        return false;
    m_entries.push_back(Entry{buffer, 0});
    m_depthFrames += buffer.frameCount;
    return true;
}

int AudioBufferQueue::read(float *destination, int requestedFrames, int channels)
{
    if (!destination || requestedFrames <= 0 || channels <= 0)
        return 0;
    std::unique_lock lock(m_mutex);
    std::fill_n(destination, requestedFrames * channels, 0.0f);
    int copied = 0;
    while (copied < requestedFrames && !m_entries.empty()) {
        Entry &entry = m_entries.front();
        if (entry.buffer.format.channels != channels) {
            m_depthFrames -= entry.buffer.frameCount - entry.offsetFrames;
            m_entries.pop_front();
            continue;
        }
        const int available = entry.buffer.frameCount - entry.offsetFrames;
        const int count = std::min(available, requestedFrames - copied);
        const auto *source = reinterpret_cast<const float *>(entry.buffer.bytes.constData()) +
            entry.offsetFrames * channels;
        std::memcpy(destination + copied * channels, source,
                    count * channels * sizeof(float));
        if (copied == 0) {
            m_lastReadMediaPositionUs = entry.buffer.ptsUs +
                static_cast<qint64>((entry.offsetFrames * 1'000'000.0) /
                                    entry.buffer.format.sampleRate);
        }
        entry.offsetFrames += count;
        copied += count;
        m_depthFrames -= count;
        if (entry.offsetFrames == entry.buffer.frameCount)
            m_entries.pop_front();
    }
    if (copied < requestedFrames)
        ++m_underruns;
    lock.unlock();
    if (copied > 0)
        m_capacityChanged.notify_all();
    return copied;
}

void AudioBufferQueue::flush(quint64 generation)
{
    {
        std::scoped_lock lock(m_mutex);
        m_entries.clear();
        m_depthFrames = 0;
        m_generation = generation;
        m_underruns = 0;
        m_lastReadMediaPositionUs = 0;
    }
    m_capacityChanged.notify_all();
}

int AudioBufferQueue::depthFrames() const
{
    std::scoped_lock lock(m_mutex);
    return m_depthFrames;
}

quint64 AudioBufferQueue::generation() const
{
    std::scoped_lock lock(m_mutex);
    return m_generation;
}

quint64 AudioBufferQueue::underrunCount() const
{
    std::scoped_lock lock(m_mutex);
    return m_underruns;
}

qint64 AudioBufferQueue::lastReadMediaPositionUs() const
{
    std::scoped_lock lock(m_mutex);
    return m_lastReadMediaPositionUs;
}

using Microsoft::WRL::ComPtr;

namespace {

QString hrText(HRESULT result)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(result)),
             8, 16, QLatin1Char('0'));
}

qint64 queryPerformanceCounter()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

} // namespace

class WASAPIAudioSink::Impl
{
public:
    ~Impl() { stop(); }

    bool open(const AudioFormat &requested, QString *error)
    {
        if (m_open.load(std::memory_order_acquire) && requested.sampleRate == m_format.sampleRate &&
            requested.channels == m_format.channels) {
            return true;
        }
        stop();
        m_format = requested;
        m_queue = std::make_unique<AudioBufferQueue>(requested.sampleRate * 2);
        m_queue->flush(m_generation.load(std::memory_order_acquire));
        m_stop.store(false, std::memory_order_release);
        m_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_shutdownEvent) {
            if (error)
                *error = QStringLiteral("Could not create WASAPI shutdown event");
            return false;
        }
        m_controlEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_controlEvent) {
            CloseHandle(m_shutdownEvent);
            m_shutdownEvent = nullptr;
            if (error)
                *error = QStringLiteral("Could not create WASAPI control event");
            return false;
        }
        {
            std::scoped_lock lock(m_initMutex);
            m_initDone = false;
            m_initSuccess = false;
            m_initError.clear();
        }
        m_worker = std::thread(&Impl::run, this);
        std::unique_lock lock(m_initMutex);
        if (!m_initCondition.wait_for(lock, std::chrono::seconds(5), [this] { return m_initDone; })) {
            lock.unlock();
            stop();
            if (error)
                *error = QStringLiteral("WASAPI initialization timed out");
            return false;
        }
        if (!m_initSuccess) {
            const QString failure = m_initError;
            lock.unlock();
            stop();
            if (error)
                *error = failure;
            return false;
        }
        return true;
    }

    void stop()
    {
        m_stop.store(true, std::memory_order_release);
        if (m_shutdownEvent)
            SetEvent(m_shutdownEvent);
        if (m_worker.joinable())
            m_worker.join();
        if (m_shutdownEvent) {
            CloseHandle(m_shutdownEvent);
            m_shutdownEvent = nullptr;
        }
        if (m_controlEvent) {
            CloseHandle(m_controlEvent);
            m_controlEvent = nullptr;
        }
        m_paused.store(false, std::memory_order_release);
        m_open.store(false, std::memory_order_release);
        m_clockValid.store(false, std::memory_order_release);
    }

    void finishInitialization(bool success, const QString &error = {})
    {
        {
            std::scoped_lock lock(m_initMutex);
            m_initDone = true;
            m_initSuccess = success;
            m_initError = error;
        }
        m_initCondition.notify_one();
    }

    void run()
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = SUCCEEDED(comResult);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            finishInitialization(false,
                                 QStringLiteral("COM initialization failed: %1").arg(hrText(comResult)));
            return;
        }
        struct CoGuard
        {
            bool active = false;
            ~CoGuard() { if (active) CoUninitialize(); }
        } coGuard{uninitializeCom};

        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> renderClient;
        ComPtr<IAudioClock> audioClock;
        ComPtr<ISimpleAudioVolume> volume;
        HANDLE audioEvent = nullptr;
        auto fail = [this, &audioEvent](const QString &message) {
            if (audioEvent)
                CloseHandle(audioEvent);
            finishInitialization(false, message);
        };

        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (FAILED(result) ||
            FAILED(result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) ||
            FAILED(result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                             &client))) {
            fail(QStringLiteral("Default WASAPI endpoint failed: %1").arg(hrText(result)));
            return;
        }

        LPWSTR rawDeviceId = nullptr;
        if (SUCCEEDED(device->GetId(&rawDeviceId)) && rawDeviceId) {
            std::scoped_lock lock(m_statusMutex);
            m_deviceName = QString::fromWCharArray(rawDeviceId);
            CoTaskMemFree(rawDeviceId);
        }

        WAVEFORMATEXTENSIBLE wave{};
        wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wave.Format.nChannels = static_cast<WORD>(m_format.channels);
        wave.Format.nSamplesPerSec = static_cast<DWORD>(m_format.sampleRate);
        wave.Format.wBitsPerSample = 32;
        wave.Format.nBlockAlign = static_cast<WORD>(m_format.channels * sizeof(float));
        wave.Format.nAvgBytesPerSec = wave.Format.nSamplesPerSec * wave.Format.nBlockAlign;
        wave.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wave.Samples.wValidBitsPerSample = 32;
        wave.dwChannelMask = m_format.channels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wave.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &wave.Format, nullptr);
        UINT32 endpointFrames = 0;
        if (FAILED(result) || FAILED(result = client->GetBufferSize(&endpointFrames))) {
            fail(QStringLiteral("WASAPI shared-mode initialization failed: %1").arg(hrText(result)));
            return;
        }
        audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!audioEvent || FAILED(result = client->SetEventHandle(audioEvent)) ||
            FAILED(result = client->GetService(IID_PPV_ARGS(&renderClient))) ||
            FAILED(result = client->GetService(IID_PPV_ARGS(&audioClock))) ||
            FAILED(result = client->GetService(IID_PPV_ARGS(&volume)))) {
            fail(QStringLiteral("WASAPI service setup failed: %1").arg(hrText(result)));
            return;
        }

        BYTE *initial = nullptr;
        if (SUCCEEDED(renderClient->GetBuffer(endpointFrames, &initial)))
            renderClient->ReleaseBuffer(endpointFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(result = client->Start())) {
            fail(QStringLiteral("WASAPI start failed: %1").arg(hrText(result)));
            return;
        }

        m_endpointFrames.store(static_cast<int>(endpointFrames), std::memory_order_release);
        m_open.store(true, std::memory_order_release);
        finishInitialization(true);

        HANDLE handles[] = {m_shutdownEvent, audioEvent, m_controlEvent};
        bool audioStopped = false;
        while (!m_stop.load(std::memory_order_acquire)) {
            const DWORD waitResult = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0)
                break;

            // Real transport pause: stop the endpoint (freezes playback, retains the queue) and
            // resume it on unpause. The audio clock is only valid while the endpoint renders.
            const bool wantPaused = m_paused.load(std::memory_order_acquire);
            if (wantPaused && !audioStopped) {
                client->Stop();
                audioStopped = true;
                m_clockValid.store(false, std::memory_order_release);
                continue;
            }
            if (!wantPaused && audioStopped) {
                client->Start();
                audioStopped = false;
                continue;
            }
            if (audioStopped || waitResult != WAIT_OBJECT_0 + 1)
                continue;

            volume->SetMasterVolume(std::clamp(m_volume.load(std::memory_order_acquire), 0.0f, 1.0f), nullptr);
            volume->SetMute(m_muted.load(std::memory_order_acquire) ? TRUE : FALSE, nullptr);
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding)) || padding >= endpointFrames)
                continue;
            const UINT32 available = endpointFrames - padding;
            BYTE *destination = nullptr;
            if (FAILED(renderClient->GetBuffer(available, &destination)))
                continue;
            const int copied = m_queue->read(reinterpret_cast<float *>(destination),
                                             static_cast<int>(available), m_format.channels);
            renderClient->ReleaseBuffer(available,
                                        copied == 0 ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
            if (copied > 0) {
                UINT64 devicePosition = 0;
                UINT64 deviceQpc100ns = 0;
                const bool endpointClockAdvanced =
                    SUCCEEDED(audioClock->GetPosition(&devicePosition, &deviceQpc100ns)) &&
                    devicePosition > 0 && deviceQpc100ns > 0;
                const qint64 paddingUs = static_cast<qint64>(
                    static_cast<long double>(padding) * 1'000'000.0L / m_format.sampleRate);
                m_clockMediaUs.store(std::max<qint64>(0,
                    m_queue->lastReadMediaPositionUs() - paddingUs), std::memory_order_release);
                m_clockQpc.store(queryPerformanceCounter(), std::memory_order_release);
                m_clockValid.store(endpointClockAdvanced, std::memory_order_release);
            }
        }

        client->Stop();
        m_open.store(false, std::memory_order_release);
        CloseHandle(audioEvent);
    }

    AudioFormat m_format;
    std::unique_ptr<AudioBufferQueue> m_queue;
    std::thread m_worker;
    HANDLE m_shutdownEvent = nullptr;
    HANDLE m_controlEvent = nullptr;
    std::atomic_bool m_stop{false};
    std::atomic_bool m_open{false};
    std::atomic_bool m_paused{false};
    std::atomic<float> m_volume{1.0f};
    std::atomic_bool m_muted{false};
    std::atomic<quint64> m_generation{0};
    std::atomic<int> m_endpointFrames{0};
    std::atomic<qint64> m_clockMediaUs{0};
    std::atomic<qint64> m_clockQpc{0};
    std::atomic_bool m_clockValid{false};
    mutable std::mutex m_statusMutex;
    QString m_deviceName;
    std::mutex m_initMutex;
    std::condition_variable m_initCondition;
    bool m_initDone = false;
    bool m_initSuccess = false;
    QString m_initError;
};

WASAPIAudioSink::WASAPIAudioSink()
    : m_impl(std::make_unique<Impl>())
{
}

WASAPIAudioSink::~WASAPIAudioSink() = default;

bool WASAPIAudioSink::open(const AudioFormat &format, QString *error)
{
    return m_impl->open(format, error);
}

int WASAPIAudioSink::write(const AudioBuffer &buffer, quint64 generation, QString *error)
{
    if (!m_impl->m_queue || !m_impl->m_queue->enqueueBlocking(buffer, generation)) {
        if (error) {
            *error = !m_impl->m_queue || generation != m_impl->m_queue->generation()
                ? QStringLiteral("Audio generation rejected")
                : QStringLiteral("Audio buffer exceeds WASAPI queue capacity");
        }
        return 0;
    }
    return buffer.frameCount;
}

AudioClockSnapshot WASAPIAudioSink::clock() const
{
    return AudioClockSnapshot{m_impl->m_clockMediaUs.load(std::memory_order_acquire),
                              m_impl->m_clockQpc.load(std::memory_order_acquire),
                              m_impl->m_clockValid.load(std::memory_order_acquire)};
}

void WASAPIAudioSink::flush(quint64 generation)
{
    m_impl->m_generation.store(generation, std::memory_order_release);
    if (m_impl->m_queue)
        m_impl->m_queue->flush(generation);
    m_impl->m_clockValid.store(false, std::memory_order_release);
}

void WASAPIAudioSink::setPaused(bool paused)
{
    m_impl->m_paused.store(paused, std::memory_order_release);
    if (m_impl->m_controlEvent)
        SetEvent(m_impl->m_controlEvent);
}

void WASAPIAudioSink::setVolume(float linear)
{
    m_impl->m_volume.store(std::clamp(linear, 0.0f, 1.0f), std::memory_order_release);
}

void WASAPIAudioSink::setMuted(bool muted)
{
    m_impl->m_muted.store(muted, std::memory_order_release);
}

int WASAPIAudioSink::queueDepthFrames() const
{
    return m_impl->m_queue ? m_impl->m_queue->depthFrames() : 0;
}

QString WASAPIAudioSink::deviceName() const
{
    std::scoped_lock lock(m_impl->m_statusMutex);
    return m_impl->m_deviceName;
}

quint64 WASAPIAudioSink::underrunCount() const
{
    return m_impl->m_queue ? m_impl->m_queue->underrunCount() : 0;
}

} // namespace Colosseum::Player2
