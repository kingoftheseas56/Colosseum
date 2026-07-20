#pragma once

#include "slot_ring.h"

#include <QtCore/QSize>
#include <QtCore/QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class SharedBridge
{
public:
    struct Snapshot
    {
        QString graphicsApi = QStringLiteral("Unknown");
        QString qtAdapter;
        QString producerAdapter;
        bool adapterMatch = false;
        bool sharedFences = false;
        std::uint64_t generated = 0;
        std::uint64_t presented = 0;
        std::uint64_t producerStarved = 0;
        std::uint64_t cpuTransfers = 0;
        std::uint64_t deviceErrors = 0;
        std::uint64_t producerFence = 0;
        std::uint64_t consumerFence = 0;
        QString source = QStringLiteral("synthetic");
        QString codec;
        QString hardwareFormat;
        QString inputFormat;
        QString sourceSize;
        QString sourceError;
        std::uint64_t decoded = 0;
        std::uint64_t converted = 0;
        std::uint64_t dropped = 0;
        std::uint64_t late = 0;
        std::uint64_t repeated = 0;
        bool softwareFallback = false;
    };

    static constexpr int Width = 1920;
    static constexpr int Height = 1080;

    bool initializeConsumer(ID3D11Device *qtDevice, QString &error);
    void shutdown();

    std::optional<std::size_t> claimProducerSlot();
    bool fillSynthetic(std::size_t slot, std::uint64_t sequence, double phase);
    bool convertVideoFrame(std::size_t slot, ID3D11Texture2D *input, UINT arraySlice,
                           std::uint64_t sequence, int width, int height,
                           DXGI_FORMAT inputFormat, double framesPerSecond,
                           bool bt709, bool fullRange, QString &error);
    std::optional<SlotRing::ConsumerSelection> acquireLatestForConsumer();
    bool waitForProducer(std::uint64_t sequence);
    bool afterFrameSubmitted(std::size_t retiringSlot);

    ID3D11Texture2D *consumerTexture(std::size_t slot) const;
    QSize textureSize() const { return QSize(Width, Height); }
    Snapshot snapshot() const;
    void notePresented() { ++m_presented; }
    void noteProducerStarved() { ++m_producerStarved; }
    void noteDecoded() { ++m_decoded; }
    void noteDropped() { ++m_dropped; }
    void noteLate() { ++m_late; }
    void noteRepeated() { ++m_repeated; }
    ID3D11Device *producerDevice() const { return m_producerDevice.Get(); }
    void setSourceInfo(const QString &source, const QString &codec, const QString &hardwareFormat,
                       const QString &inputFormat, int width, int height, bool softwareFallback);
    void setSourceError(const QString &error);

private:
    struct TextureSlot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> producerTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> producerTarget;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> consumerTexture;
    };

    static bool adapterInfo(ID3D11Device *device, LUID &luid, QString &description);
    bool createProducerOnQtAdapter(ID3D11Device *qtDevice, QString &error);
    bool createSharedTextures(QString &error);
    bool createSharedFences(QString &error);
    bool ensureVideoProcessor(int width, int height, DXGI_FORMAT inputFormat,
                              double framesPerSecond, QString &error);

    mutable std::mutex m_lifecycleMutex;
    SlotRing m_ring;
    std::array<TextureSlot, SlotRing::SlotCount> m_slots;

    Microsoft::WRL::ComPtr<ID3D11Device> m_producerDevice;
    Microsoft::WRL::ComPtr<ID3D11Device5> m_producerDevice5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_producerContext;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_producerContext1;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> m_producerContext4;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_videoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_videoEnumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_videoProcessor;
    std::array<Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView>, SlotRing::SlotCount> m_videoOutputs;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    DXGI_FORMAT m_videoInputFormat = DXGI_FORMAT_UNKNOWN;

    Microsoft::WRL::ComPtr<ID3D11Device> m_consumerDevice;
    Microsoft::WRL::ComPtr<ID3D11Device5> m_consumerDevice5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_consumerContext;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> m_consumerContext4;

    Microsoft::WRL::ComPtr<ID3D11Fence> m_producerFence;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_producerFenceOnConsumer;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_consumerFence;
    Microsoft::WRL::ComPtr<ID3D11Fence> m_consumerFenceOnProducer;

    QString m_qtAdapter;
    QString m_producerAdapter;
    bool m_initialized = false;
    bool m_adapterMatch = false;
    bool m_sharedFences = false;

    std::atomic<std::uint64_t> m_generated{0};
    std::atomic<std::uint64_t> m_presented{0};
    std::atomic<std::uint64_t> m_producerStarved{0};
    std::atomic<std::uint64_t> m_deviceErrors{0};
    std::atomic<std::uint64_t> m_producerFenceValue{0};
    std::atomic<std::uint64_t> m_consumerFenceValue{0};
    std::atomic<std::uint64_t> m_decoded{0};
    std::atomic<std::uint64_t> m_converted{0};
    std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<std::uint64_t> m_late{0};
    std::atomic<std::uint64_t> m_repeated{0};
    mutable std::mutex m_statusMutex;
    QString m_source = QStringLiteral("synthetic");
    QString m_codec;
    QString m_hardwareFormat;
    QString m_inputFormat;
    QString m_sourceSize;
    QString m_sourceError;
    bool m_softwareFallback = false;
};
