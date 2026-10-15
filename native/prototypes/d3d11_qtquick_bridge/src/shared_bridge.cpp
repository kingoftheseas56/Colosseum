#include "shared_bridge.h"

#include <QtCore/QDebug>

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

QString hrText(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'));
}

bool sameLuid(const LUID &a, const LUID &b)
{
    return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

} // namespace

bool SharedBridge::adapterInfo(ID3D11Device *device, LUID &luid, QString &description)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) {
        return false;
    }
    luid = desc.AdapterLuid;
    description = QString::fromWCharArray(desc.Description);
    return true;
}

bool SharedBridge::createProducerOnQtAdapter(ID3D11Device *qtDevice, QString &error)
{
    ComPtr<IDXGIDevice> qtDxgi;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(qtDevice->QueryInterface(IID_PPV_ARGS(&qtDxgi))) || FAILED(qtDxgi->GetAdapter(&adapter))) {
        error = QStringLiteral("Cannot resolve Qt's DXGI adapter");
        return false;
    }

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    const HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                                         levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                         &m_producerDevice, &selected, &m_producerContext);
    if (FAILED(hr)) {
        error = QStringLiteral("D3D11 producer device creation failed: %1").arg(hrText(hr));
        return false;
    }
    if (FAILED(m_producerDevice.As(&m_producerDevice5)) ||
        FAILED(m_producerContext.As(&m_producerContext1)) ||
        FAILED(m_producerContext.As(&m_producerContext4))) {
        error = QStringLiteral("Producer device lacks D3D11.4 fence interfaces");
        return false;
    }
    return true;
}

bool SharedBridge::createSharedTextures(QString &error)
{
    for (auto &slot : m_slots) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = Width;
        desc.Height = Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        // Match Kodi's true-shared decoder-surface contract: a legacy DXGI shared
        // handle plus a separate D3D11 fence. NT handles require keyed-mutex
        // ownership as well, which produced an invalid/black half-owned surface.
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        HRESULT hr = m_producerDevice->CreateTexture2D(&desc, nullptr, &slot.producerTexture);
        if (FAILED(hr)) {
            error = QStringLiteral("Shared texture creation failed: %1").arg(hrText(hr));
            return false;
        }
        hr = m_producerDevice->CreateRenderTargetView(slot.producerTexture.Get(), nullptr,
                                                       &slot.producerTarget);
        if (FAILED(hr)) {
            error = QStringLiteral("Shared render-target creation failed: %1").arg(hrText(hr));
            return false;
        }

        ComPtr<IDXGIResource> resource;
        HANDLE handle = nullptr;
        if (FAILED(slot.producerTexture.As(&resource)) ||
            FAILED(hr = resource->GetSharedHandle(&handle))) {
            error = QStringLiteral("Texture handle creation failed: %1").arg(hrText(hr));
            return false;
        }
        hr = m_consumerDevice->OpenSharedResource(handle, IID_PPV_ARGS(&slot.consumerTexture));
        if (FAILED(hr)) {
            error = QStringLiteral("Qt device could not open shared texture: %1").arg(hrText(hr));
            return false;
        }
    }
    return true;
}

bool SharedBridge::createSharedFences(QString &error)
{
    HRESULT hr = m_producerDevice5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                                 IID_PPV_ARGS(&m_producerFence));
    if (FAILED(hr)) {
        error = QStringLiteral("Producer CreateFence failed: %1").arg(hrText(hr));
        return false;
    }
    HANDLE producerHandle = nullptr;
    hr = m_producerFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &producerHandle);
    if (FAILED(hr)) {
        error = QStringLiteral("Producer fence handle failed: %1").arg(hrText(hr));
        return false;
    }
    hr = m_consumerDevice5->OpenSharedFence(producerHandle,
                                            IID_PPV_ARGS(&m_producerFenceOnConsumer));
    CloseHandle(producerHandle);
    if (FAILED(hr)) {
        error = QStringLiteral("Qt OpenSharedFence failed: %1").arg(hrText(hr));
        return false;
    }

    hr = m_consumerDevice5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                        IID_PPV_ARGS(&m_consumerFence));
    if (FAILED(hr)) {
        error = QStringLiteral("Consumer CreateFence failed: %1").arg(hrText(hr));
        return false;
    }
    HANDLE consumerHandle = nullptr;
    hr = m_consumerFence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &consumerHandle);
    if (FAILED(hr)) {
        error = QStringLiteral("Consumer fence handle failed: %1").arg(hrText(hr));
        return false;
    }
    hr = m_producerDevice5->OpenSharedFence(consumerHandle,
                                            IID_PPV_ARGS(&m_consumerFenceOnProducer));
    CloseHandle(consumerHandle);
    if (FAILED(hr)) {
        error = QStringLiteral("Producer OpenSharedFence failed: %1").arg(hrText(hr));
        return false;
    }
    m_sharedFences = true;
    return true;
}

bool SharedBridge::initializeConsumer(ID3D11Device *qtDevice, QString &error)
{
    std::scoped_lock lock(m_lifecycleMutex);
    if (m_initialized)
        return true;
    if (!qtDevice) {
        error = QStringLiteral("Qt returned a null D3D11 device");
        return false;
    }

    m_consumerDevice = qtDevice;
    m_consumerDevice->GetImmediateContext(&m_consumerContext);
    if (FAILED(m_consumerDevice.As(&m_consumerDevice5)) ||
        FAILED(m_consumerContext.As(&m_consumerContext4))) {
        error = QStringLiteral("Qt device lacks D3D11.4 fence interfaces");
        return false;
    }
    if (!createProducerOnQtAdapter(qtDevice, error))
        return false;

    LUID qtLuid{}, producerLuid{};
    if (!adapterInfo(m_consumerDevice.Get(), qtLuid, m_qtAdapter) ||
        !adapterInfo(m_producerDevice.Get(), producerLuid, m_producerAdapter)) {
        error = QStringLiteral("Could not read adapter identities");
        return false;
    }
    m_adapterMatch = sameLuid(qtLuid, producerLuid);
    if (!m_adapterMatch) {
        error = QStringLiteral("Producer and Qt adapter LUIDs differ");
        return false;
    }
    if (!createSharedTextures(error) || !createSharedFences(error))
        return false;

    m_initialized = true;
    qInfo().noquote() << "BRIDGE_INIT qtAdapter=" << m_qtAdapter
                      << "producerAdapter=" << m_producerAdapter
                      << "sharedFences=true";
    return true;
}

std::optional<std::size_t> SharedBridge::claimProducerSlot()
{
    if (m_consumerFenceOnProducer)
        m_ring.markConsumerFenceComplete(m_consumerFenceOnProducer->GetCompletedValue());
    return m_ring.claimForProducer();
}

bool SharedBridge::fillSynthetic(std::size_t slot, std::uint64_t sequence, double phase)
{
    if (!m_initialized || slot >= m_slots.size())
        return false;

    const float base[] = {
        static_cast<float>(0.04 + 0.03 * std::sin(phase)),
        static_cast<float>(0.07 + 0.03 * std::sin(phase + 2.0)),
        static_cast<float>(0.12 + 0.04 * std::sin(phase + 4.0)),
        1.0f
    };
    m_producerContext->ClearRenderTargetView(m_slots[slot].producerTarget.Get(), base);

    const int barWidth = 180;
    const int x = static_cast<int>((Width + barWidth) * (0.5 + 0.5 * std::sin(phase))) - barWidth;
    const D3D11_RECT bar{std::max(0, x), 80, std::min(Width, x + barWidth), Height - 80};
    const float cyan[] = {0.03f, 0.78f, 0.92f, 1.0f};
    if (bar.right > bar.left)
        m_producerContext1->ClearView(m_slots[slot].producerTarget.Get(), cyan, &bar, 1);

    const int scanY = static_cast<int>((Height - 24) * (0.5 + 0.5 * std::sin(phase * 1.7)));
    const D3D11_RECT scan{0, scanY, Width, scanY + 24};
    const float amber[] = {0.95f, 0.43f, 0.06f, 1.0f};
    m_producerContext1->ClearView(m_slots[slot].producerTarget.Get(), amber, &scan, 1);

    const HRESULT hr = m_producerContext4->Signal(m_producerFence.Get(), sequence);
    if (FAILED(hr) || !m_ring.publishProduced(slot, sequence)) {
        ++m_deviceErrors;
        return false;
    }
    m_producerFenceValue = sequence;
    ++m_generated;
    return true;
}

bool SharedBridge::ensureVideoProcessor(int width, int height, DXGI_FORMAT inputFormat,
                                        double framesPerSecond, QString &error)
{
    if (m_videoProcessor && width == m_videoWidth && height == m_videoHeight &&
        inputFormat == m_videoInputFormat)
        return true;

    m_videoOutputs = {};
    m_videoProcessor.Reset();
    m_videoEnumerator.Reset();
    if (!m_videoDevice && FAILED(m_producerDevice.As(&m_videoDevice))) {
        error = QStringLiteral("Producer device has no ID3D11VideoDevice");
        return false;
    }
    if (!m_videoContext && FAILED(m_producerContext.As(&m_videoContext))) {
        error = QStringLiteral("Producer context has no ID3D11VideoContext");
        return false;
    }

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = static_cast<UINT>(width);
    content.InputHeight = static_cast<UINT>(height);
    content.OutputWidth = Width;
    content.OutputHeight = Height;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    const UINT rate = static_cast<UINT>(std::max(1.0, std::round(framesPerSecond * 1000.0)));
    content.InputFrameRate = {rate, 1000};
    content.OutputFrameRate = content.InputFrameRate;

    HRESULT hr = m_videoDevice->CreateVideoProcessorEnumerator(&content, &m_videoEnumerator);
    if (FAILED(hr)) {
        error = QStringLiteral("CreateVideoProcessorEnumerator failed: %1").arg(hrText(hr));
        return false;
    }
    UINT flags = 0;
    if (FAILED(m_videoEnumerator->CheckVideoProcessorFormat(inputFormat, &flags)) ||
        !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
        error = QStringLiteral("Video processor rejects decoder input format %1").arg(inputFormat);
        return false;
    }
    if (FAILED(m_videoEnumerator->CheckVideoProcessorFormat(DXGI_FORMAT_R8G8B8A8_UNORM, &flags)) ||
        !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        error = QStringLiteral("Video processor cannot output Qt-compatible RGBA8");
        return false;
    }
    hr = m_videoDevice->CreateVideoProcessor(m_videoEnumerator.Get(), 0, &m_videoProcessor);
    if (FAILED(hr)) {
        error = QStringLiteral("CreateVideoProcessor failed: %1").arg(hrText(hr));
        return false;
    }
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputDesc.Texture2D.MipSlice = 0;
        hr = m_videoDevice->CreateVideoProcessorOutputView(m_slots[i].producerTexture.Get(),
                                                           m_videoEnumerator.Get(), &outputDesc,
                                                           &m_videoOutputs[i]);
        if (FAILED(hr)) {
            error = QStringLiteral("CreateVideoProcessorOutputView failed: %1").arg(hrText(hr));
            return false;
        }
    }
    m_videoWidth = width;
    m_videoHeight = height;
    m_videoInputFormat = inputFormat;
    return true;
}

bool SharedBridge::convertVideoFrame(std::size_t slot, ID3D11Texture2D *input, UINT arraySlice,
                                     std::uint64_t sequence, int width, int height,
                                     DXGI_FORMAT inputFormat, double framesPerSecond,
                                     bool bt709, bool fullRange, QString &error)
{
    if (!m_initialized || !input || slot >= m_slots.size() ||
        !ensureVideoProcessor(width, height, inputFormat, framesPerSecond, error))
        return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
    inputDesc.FourCC = 0;
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = arraySlice;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = m_videoDevice->CreateVideoProcessorInputView(input, m_videoEnumerator.Get(),
                                                               &inputDesc, &inputView);
    if (FAILED(hr)) {
        error = QStringLiteral("CreateVideoProcessorInputView failed: %1").arg(hrText(hr));
        return false;
    }

    const RECT sourceRect{0, 0, width, height};
    const RECT targetRect{0, 0, Width, Height};
    m_videoContext->VideoProcessorSetStreamSourceRect(m_videoProcessor.Get(), 0, TRUE, &sourceRect);
    m_videoContext->VideoProcessorSetStreamDestRect(m_videoProcessor.Get(), 0, TRUE, &targetRect);
    m_videoContext->VideoProcessorSetOutputTargetRect(m_videoProcessor.Get(), TRUE, &targetRect);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
    inputColor.YCbCr_Matrix = bt709 ? 1 : 0;
    inputColor.Nominal_Range = fullRange ? 2 : 1;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
    outputColor.RGB_Range = 0;
    outputColor.Nominal_Range = 2;
    m_videoContext->VideoProcessorSetStreamColorSpace(m_videoProcessor.Get(), 0, &inputColor);
    m_videoContext->VideoProcessorSetOutputColorSpace(m_videoProcessor.Get(), &outputColor);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    hr = m_videoContext->VideoProcessorBlt(m_videoProcessor.Get(), m_videoOutputs[slot].Get(),
                                           0, 1, &stream);
    if (FAILED(hr)) {
        error = QStringLiteral("VideoProcessorBlt failed: %1").arg(hrText(hr));
        return false;
    }
    hr = m_producerContext4->Signal(m_producerFence.Get(), sequence);
    if (FAILED(hr) || !m_ring.publishProduced(slot, sequence)) {
        error = QStringLiteral("Publishing converted frame failed: %1").arg(hrText(hr));
        ++m_deviceErrors;
        return false;
    }
    m_producerFenceValue = sequence;
    ++m_generated;
    ++m_converted;
    return true;
}

void SharedBridge::setSourceInfo(const QString &source, const QString &codec,
                                 const QString &hardwareFormat, const QString &inputFormat,
                                 int width, int height, bool softwareFallback)
{
    std::scoped_lock lock(m_statusMutex);
    m_source = source;
    m_codec = codec;
    m_hardwareFormat = hardwareFormat;
    m_inputFormat = inputFormat;
    m_sourceSize = width > 0 && height > 0
        ? QStringLiteral("%1x%2").arg(width).arg(height) : QString();
    m_softwareFallback = softwareFallback;
}

void SharedBridge::setSourceError(const QString &error)
{
    std::scoped_lock lock(m_statusMutex);
    m_sourceError = error;
}

std::optional<SlotRing::ConsumerSelection> SharedBridge::acquireLatestForConsumer()
{
    return m_ring.acquireLatestForConsumer();
}

bool SharedBridge::waitForProducer(std::uint64_t sequence)
{
    const HRESULT hr = m_consumerContext4->Wait(m_producerFenceOnConsumer.Get(), sequence);
    if (FAILED(hr)) {
        ++m_deviceErrors;
        return false;
    }
    return true;
}

bool SharedBridge::afterFrameSubmitted(std::size_t retiringSlot)
{
    const std::uint64_t value = ++m_consumerFenceValue;
    const HRESULT hr = m_consumerContext4->Signal(m_consumerFence.Get(), value);
    if (FAILED(hr) || !m_ring.retireAfterConsumerSubmission(retiringSlot, value)) {
        ++m_deviceErrors;
        return false;
    }
    return true;
}

ID3D11Texture2D *SharedBridge::consumerTexture(std::size_t slot) const
{
    return slot < m_slots.size() ? m_slots[slot].consumerTexture.Get() : nullptr;
}

SharedBridge::Snapshot SharedBridge::snapshot() const
{
    Snapshot result;
    result.graphicsApi = QStringLiteral("Direct3D11");
    result.qtAdapter = m_qtAdapter;
    result.producerAdapter = m_producerAdapter;
    result.adapterMatch = m_adapterMatch;
    result.sharedFences = m_sharedFences;
    result.generated = m_generated.load();
    result.presented = m_presented.load();
    result.producerStarved = m_producerStarved.load();
    result.deviceErrors = m_deviceErrors.load();
    result.producerFence = m_producerFenceValue.load();
    result.consumerFence = m_consumerFenceValue.load();
    result.decoded = m_decoded.load();
    result.converted = m_converted.load();
    result.dropped = m_dropped.load();
    result.late = m_late.load();
    result.repeated = m_repeated.load();
    {
        std::scoped_lock lock(m_statusMutex);
        result.source = m_source;
        result.codec = m_codec;
        result.hardwareFormat = m_hardwareFormat;
        result.inputFormat = m_inputFormat;
        result.sourceSize = m_sourceSize;
        result.sourceError = m_sourceError;
        result.softwareFallback = m_softwareFallback;
    }
    return result;
}

void SharedBridge::shutdown()
{
    std::scoped_lock lock(m_lifecycleMutex);
    for (auto &slot : m_slots)
        slot = TextureSlot{};
    m_consumerFenceOnProducer.Reset();
    m_consumerFence.Reset();
    m_producerFenceOnConsumer.Reset();
    m_producerFence.Reset();
    m_consumerContext4.Reset();
    m_consumerContext.Reset();
    m_consumerDevice5.Reset();
    m_consumerDevice.Reset();
    m_producerContext4.Reset();
    m_videoOutputs = {};
    m_videoProcessor.Reset();
    m_videoEnumerator.Reset();
    m_videoContext.Reset();
    m_videoDevice.Reset();
    m_producerContext1.Reset();
    m_producerContext.Reset();
    m_producerDevice5.Reset();
    m_producerDevice.Reset();
    m_initialized = false;
}
