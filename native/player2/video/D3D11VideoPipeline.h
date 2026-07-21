#pragma once

#include "D3D11TextureRing.h"

#include <QtCore/QSize>
#include <QtCore/QString>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>

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
        quint64 cpuTransfers = 0;
        quint64 deviceErrors = 0;
        QString hardwareFormat;
        QString inputFormat;
        QString error;
    };

    D3D11VideoPipeline();
    ~D3D11VideoPipeline();

    bool initialize(ID3D11Device *qtDevice, QString *error);
    AVBufferRef *createDecoderDeviceContext(QString *error) const;
    bool submitDecodedFrame(AVFrame *frame, VideoFrameToken token, QString *error);
    std::optional<VideoFrameToken> acquireLatestForPresentation(quint64 generation);
    void retirePresentedFrame(quint64 consumerFenceValue);
    void flush(quint64 nextGeneration);

    std::optional<PresentationFrame> acquirePresentationFrame(quint64 generation);
    bool waitForProducer(quint64 sequence);
    bool retireAfterRendering(std::size_t slot);
    ID3D11Texture2D *consumerTexture(std::size_t slot) const;
    QSize textureSize() const;
    void noteDecoded();
    void notePresented();
    Diagnostics diagnostics() const;
    void shutdown();

private:
    struct TextureSlot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> producerTexture;
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
    QString m_error;
    std::atomic<quint64> m_decoded{0};
    std::atomic<quint64> m_submitted{0};
    std::atomic<quint64> m_presented{0};
    std::atomic<quint64> m_deviceErrors{0};
    std::atomic<quint64> m_consumerFenceValue{0};
};

} // namespace Colosseum::Player2
