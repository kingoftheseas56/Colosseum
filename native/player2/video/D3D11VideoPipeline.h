#pragma once

#include "D3D11TextureRing.h"
#include "player2/platform/windows/DeviceRecovery.h"

#include <QtCore/QSize>
#include <QtCore/QString>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

struct AVBufferRef;
struct AVFrame;

namespace Colosseum::Player2 {

class D3D11VideoPipeline
{
public:
    struct PresentationFrame
    {
        std::size_t slot = 0;
        VideoFrameToken token;
        std::optional<std::size_t> retiringSlot;
    };

    struct Diagnostics
    {
        QString qtAdapter;
        QString producerAdapter;
        bool adapterMatch = false;
        bool sharedFences = false;
        quint64 decoded = 0;
        quint64 submitted = 0;
        quint64 presented = 0;
        quint64 producerStarved = 0;
        quint64 scheduledLateDrops = 0;
        qint64 lastAvErrorUs = 0;
        quint64 cpuTransfers = 0;
        quint64 deviceErrors = 0;
        QString hardwareFormat;
        QString inputFormat;
        QString colorConversion;
        // The SOURCE frame's dimensions, taken off the decoded AVFrame - not the ring's fixed
        // OutputWidth/OutputHeight (textureSize(), which is a constant 1920x1080 and would read as
        // a resolution for every file). Zero until a frame has actually been published, which is
        // the honest answer: "no video yet".
        int sourceWidth = 0;
        int sourceHeight = 0;
        bool deviceLost = false;
        QString error;
    };

    D3D11VideoPipeline();
    ~D3D11VideoPipeline();

    bool initialize(ID3D11Device *qtDevice, QString *error);
    AVBufferRef *createDecoderDeviceContext(QString *error) const;
    bool submitDecodedFrame(AVFrame *frame, VideoFrameToken token, QString *error);
    bool submitSyntheticFrame(VideoFrameToken token, double phase, QString *error);
    std::optional<VideoFrameToken> acquireLatestForPresentation(quint64 generation);
    void retirePresentedFrame(quint64 consumerFenceValue);
    void flush(quint64 nextGeneration);

    std::optional<PresentationFrame> acquirePresentationFrame(quint64 generation);
    // Presents the ring's CURRENT generation — the paint item follows seeks automatically.
    std::optional<PresentationFrame> acquirePresentationFrame();
    bool waitForProducer(quint64 sequence);
    bool retireAfterRendering(std::size_t slot);
    ID3D11Texture2D *consumerTexture(std::size_t slot) const;
    QSize textureSize() const;
    void noteDecoded();
    void noteSchedulingDecision(qint64 timingErrorUs, bool dropped);
    qint64 schedulingP95AbsoluteErrorUs() const;
    void notePresented();
    Diagnostics diagnostics() const;
    // True once a GPU call failed with DXGI_ERROR_DEVICE_REMOVED / _RESET. The session's recovery
    // coordinator (not the pipeline) decides whether to rebuild; the pipeline only reports the fact.
    bool deviceLost() const noexcept;
    DeviceLostReason deviceLostReason() const noexcept;
    void shutdown();

private:
    struct TextureSlot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> producerTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> producerTarget;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> consumerTexture;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
    };

    static constexpr UINT OutputWidth = 1920;
    static constexpr UINT OutputHeight = 1080;

    static bool adapterInfo(ID3D11Device *device, LUID *luid, QString *description);
    bool createProducerDevice(ID3D11Device *qtDevice, QString *error);
    bool createSharedTextures(QString *error);
    bool createSharedFences(QString *error);
    bool ensureVideoProcessor(int width, int height, DXGI_FORMAT format, QString *error);
    void setError(const QString &error);
    void noteHresult(long hr); // flag a device-removed / device-reset HRESULT as a device-lost fact

    mutable std::mutex m_mutex;
    D3D11TextureRing m_ring{1};
    std::array<TextureSlot, D3D11TextureRing::SlotCount> m_slots;

    Microsoft::WRL::ComPtr<ID3D11Device> m_producerDevice;
    Microsoft::WRL::ComPtr<ID3D11Device5> m_producerDevice5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_producerContext;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> m_producerContext4;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_videoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_videoEnumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_videoProcessor;

    Microsoft::WRL::ComPtr<ID3D11Device> m_consumerDevice;
    Microsoft::WRL::ComPtr<ID3D11Device5> m_consumerDevice5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_consumerContext;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> m_consumerContext4;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_producerFence;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_producerFenceOnConsumer;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_consumerFence;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_consumerFenceOnProducer;

    int m_videoWidth = 0;
    int m_videoHeight = 0;
    DXGI_FORMAT m_videoInputFormat = DXGI_FORMAT_UNKNOWN;
    std::atomic_bool m_initialized{false};
    bool m_adapterMatch = false;
    bool m_sharedFences = false;
    QString m_qtAdapter;
    QString m_producerAdapter;
    QString m_hardwareFormat;
    QString m_inputFormat;
    QString m_colorConversion;
    // Guarded by m_mutex, alongside m_inputFormat: m_videoWidth/m_videoHeight above are the video
    // processor's cached input size and are touched only on the producer thread, so diagnostics()
    // (GUI thread) must not read them directly.
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    QString m_error;
    std::atomic<quint64> m_decoded{0};
    std::atomic<quint64> m_submitted{0};
    std::atomic<quint64> m_presented{0};
    std::atomic<quint64> m_scheduledLateDrops{0};
    std::atomic<qint64> m_lastAvErrorUs{0};
    mutable std::mutex m_timingMutex;
    std::vector<qint64> m_schedulingAbsoluteErrorsUs;
    std::atomic<quint64> m_deviceErrors{0};
    std::atomic<quint64> m_consumerFenceValue{0};
    // Persistent, monotonic across reopen — a fence value must never move backward, but the per-media
    // token.sequence restarts at 1 each open, so the producer fence uses this instead.
    std::atomic<quint64> m_producerFenceValue{0};
    std::atomic_bool m_deviceLost{false};
    std::atomic<DeviceLostReason> m_deviceLostReason{DeviceLostReason::None};
};

} // namespace Colosseum::Player2
