#include "D3D11VideoPipeline.h"

#include "ColorHdrPolicy.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace Colosseum::Player2 {
namespace {

QString hrText(HRESULT value)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(value)),
                                      8, 16, QLatin1Char('0'));
}

bool sameLuid(const LUID &left, const LUID &right)
{
    return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
}

QString formatName(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_NV12)
        return QStringLiteral("NV12");
    if (format == DXGI_FORMAT_P010)
        return QStringLiteral("P010");
    return QStringLiteral("DXGI_%1").arg(static_cast<int>(format));
}

} // namespace

D3D11VideoPipeline::D3D11VideoPipeline() = default;
D3D11VideoPipeline::~D3D11VideoPipeline() { shutdown(); }

bool D3D11VideoPipeline::adapterInfo(ID3D11Device *device, LUID *luid, QString *description)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC descriptor{};
    if (!device || !luid || !description ||
        FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&descriptor))) {
        return false;
    }
    *luid = descriptor.AdapterLuid;
    *description = QString::fromWCharArray(descriptor.Description);
    return true;
}

void D3D11VideoPipeline::setError(const QString &error)
{
    std::scoped_lock lock(m_mutex);
    m_error = error;
}

bool D3D11VideoPipeline::createProducerDevice(ID3D11Device *qtDevice, QString *error)
{
    ComPtr<IDXGIDevice> qtDxgi;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(qtDevice->QueryInterface(IID_PPV_ARGS(&qtDxgi))) ||
        FAILED(qtDxgi->GetAdapter(&adapter))) {
        *error = QStringLiteral("Cannot resolve Qt's DXGI adapter");
        return false;
    }
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    const HRESULT result = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_producerDevice, &selected,
        &m_producerContext);
    if (FAILED(result)) {
        *error = QStringLiteral("D3D11 producer device creation failed: %1").arg(hrText(result));
        return false;
    }
    if (FAILED(m_producerDevice.As(&m_producerDevice5)) ||
        FAILED(m_producerContext.As(&m_producerContext4))) {
        *error = QStringLiteral("Producer device lacks D3D11.4 fence interfaces");
        return false;
    }
    return true;
}

bool D3D11VideoPipeline::createSharedTextures(QString *error)
{
    for (TextureSlot &slot : m_slots) {
        D3D11_TEXTURE2D_DESC descriptor{};
        descriptor.Width = OutputWidth;
        descriptor.Height = OutputHeight;
        descriptor.MipLevels = 1;
        descriptor.ArraySize = 1;
        descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descriptor.SampleDesc.Count = 1;
        descriptor.Usage = D3D11_USAGE_DEFAULT;
        descriptor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        descriptor.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        HRESULT result = m_producerDevice->CreateTexture2D(&descriptor, nullptr,
                                                            &slot.producerTexture);
        if (FAILED(result)) {
            *error = QStringLiteral("Shared texture creation failed: %1").arg(hrText(result));
            return false;
        }
        result = m_producerDevice->CreateRenderTargetView(slot.producerTexture.Get(), nullptr,
                                                           &slot.producerTarget);
        if (FAILED(result)) {
            *error = QStringLiteral("Shared render target creation failed: %1")
                         .arg(hrText(result));
            return false;
        }
        ComPtr<IDXGIResource> resource;
        HANDLE handle = nullptr;
        if (FAILED(slot.producerTexture.As(&resource)) ||
            FAILED(result = resource->GetSharedHandle(&handle))) {
            *error = QStringLiteral("Texture handle creation failed: %1").arg(hrText(result));
            return false;
        }
        result = m_consumerDevice->OpenSharedResource(handle,
                                                       IID_PPV_ARGS(&slot.consumerTexture));
        if (FAILED(result)) {
            *error = QStringLiteral("Qt device could not open shared texture: %1")
                         .arg(hrText(result));
            return false;
        }
    }
    return true;
}

bool D3D11VideoPipeline::createSharedFences(QString *error)
{
    HRESULT result = m_producerDevice5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                                     IID_PPV_ARGS(&m_producerFence));
    HANDLE handle = nullptr;
    if (FAILED(result) || FAILED(result = m_producerFence->CreateSharedHandle(
                                     nullptr, GENERIC_ALL, nullptr, &handle))) {
        *error = QStringLiteral("Producer fence creation failed: %1").arg(hrText(result));
        return false;
    }
    result = m_consumerDevice5->OpenSharedFence(handle,
                                                IID_PPV_ARGS(&m_producerFenceOnConsumer));
    CloseHandle(handle);
    if (FAILED(result)) {
        *error = QStringLiteral("Qt OpenSharedFence failed: %1").arg(hrText(result));
        return false;
    }

    result = m_consumerDevice5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                            IID_PPV_ARGS(&m_consumerFence));
    handle = nullptr;
    if (FAILED(result) || FAILED(result = m_consumerFence->CreateSharedHandle(
                                     nullptr, GENERIC_ALL, nullptr, &handle))) {
        *error = QStringLiteral("Consumer fence creation failed: %1").arg(hrText(result));
        return false;
    }
    result = m_producerDevice5->OpenSharedFence(handle,
                                                IID_PPV_ARGS(&m_consumerFenceOnProducer));
    CloseHandle(handle);
    if (FAILED(result)) {
        *error = QStringLiteral("Producer OpenSharedFence failed: %1").arg(hrText(result));
        return false;
    }
    m_sharedFences = true;
    return true;
}

bool D3D11VideoPipeline::initialize(ID3D11Device *qtDevice, QString *error)
{
    QString localError;
    if (!error)
        error = &localError;
    if (m_initialized)
        return true;
    // A fresh device starts healthy: clear any device-lost latch from a prior (now torn-down) device
    // so a stale flag can never mislabel a later ordinary decode failure as a device loss.
    m_deviceLost.store(false, std::memory_order_release);
    m_deviceLostReason.store(DeviceLostReason::None, std::memory_order_release);
    if (!qtDevice) {
        *error = QStringLiteral("Qt returned a null D3D11 device");
        setError(*error);
        return false;
    }
    m_consumerDevice = qtDevice;
    m_consumerDevice->GetImmediateContext(&m_consumerContext);
    if (FAILED(m_consumerDevice.As(&m_consumerDevice5)) ||
        FAILED(m_consumerContext.As(&m_consumerContext4))) {
        *error = QStringLiteral("Qt device lacks D3D11.4 fence interfaces");
        setError(*error);
        return false;
    }
    if (!createProducerDevice(qtDevice, error)) {
        setError(*error);
        return false;
    }
    LUID qtLuid{}, producerLuid{};
    QString qtAdapter;
    QString producerAdapter;
    if (!adapterInfo(m_consumerDevice.Get(), &qtLuid, &qtAdapter) ||
        !adapterInfo(m_producerDevice.Get(), &producerLuid, &producerAdapter)) {
        *error = QStringLiteral("Could not read adapter identities");
        setError(*error);
        return false;
    }
    const bool adapterMatch = sameLuid(qtLuid, producerLuid);
    if (!adapterMatch) {
        *error = QStringLiteral("Producer and Qt adapter LUIDs differ");
        setError(*error);
        return false;
    }
    if (!createSharedTextures(error) || !createSharedFences(error)) {
        setError(*error);
        return false;
    }
    {
        std::scoped_lock lock(m_mutex);
        m_qtAdapter = qtAdapter;
        m_producerAdapter = producerAdapter;
        m_adapterMatch = adapterMatch;
        m_initialized = true;
    }
    return true;
}

AVBufferRef *D3D11VideoPipeline::createDecoderDeviceContext(QString *error) const
{
    ComPtr<ID3D11Device> producerDevice;
    {
        std::scoped_lock lock(m_mutex);
        producerDevice = m_producerDevice;
    }
    if (!m_initialized || !producerDevice) {
        if (error)
            *error = QStringLiteral("Video pipeline is not initialized");
        return nullptr;
    }
    AVBufferRef *hardware = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hardware) {
        if (error)
            *error = QStringLiteral("Could not allocate D3D11VA hardware context");
        return nullptr;
    }
    auto *context = reinterpret_cast<AVHWDeviceContext *>(hardware->data);
    auto *d3d = reinterpret_cast<AVD3D11VADeviceContext *>(context->hwctx);
    d3d->device = producerDevice.Get();
    d3d->device->AddRef();
    const int result = av_hwdevice_ctx_init(hardware);
    if (result < 0) {
        av_buffer_unref(&hardware);
        if (error)
            *error = QStringLiteral("Could not initialize D3D11VA hardware context");
        return nullptr;
    }
    return hardware;
}

bool D3D11VideoPipeline::ensureVideoProcessor(int width, int height, DXGI_FORMAT format,
                                               QString *error)
{
    if (m_videoProcessor && width == m_videoWidth && height == m_videoHeight &&
        format == m_videoInputFormat) {
        return true;
    }
    for (TextureSlot &slot : m_slots)
        slot.outputView.Reset();
    m_videoProcessor.Reset();
    m_videoEnumerator.Reset();
    if ((!m_videoDevice && FAILED(m_producerDevice.As(&m_videoDevice))) ||
        (!m_videoContext && FAILED(m_producerContext.As(&m_videoContext)))) {
        *error = QStringLiteral("Producer device lacks video processor interfaces");
        return false;
    }
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = static_cast<UINT>(width);
    content.InputHeight = static_cast<UINT>(height);
    content.OutputWidth = OutputWidth;
    content.OutputHeight = OutputHeight;
    content.InputFrameRate = {24, 1};
    content.OutputFrameRate = content.InputFrameRate;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    HRESULT result = m_videoDevice->CreateVideoProcessorEnumerator(&content,
                                                                    &m_videoEnumerator);
    if (FAILED(result)) {
        *error = QStringLiteral("CreateVideoProcessorEnumerator failed: %1")
                     .arg(hrText(result));
        return false;
    }
    UINT support = 0;
    if (FAILED(m_videoEnumerator->CheckVideoProcessorFormat(format, &support)) ||
        !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) ||
        FAILED(m_videoEnumerator->CheckVideoProcessorFormat(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                             &support)) ||
        !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        *error = QStringLiteral("Video processor format contract is unsupported");
        return false;
    }
    if (FAILED(result = m_videoDevice->CreateVideoProcessor(m_videoEnumerator.Get(), 0,
                                                             &m_videoProcessor))) {
        *error = QStringLiteral("CreateVideoProcessor failed: %1").arg(hrText(result));
        return false;
    }
    for (TextureSlot &slot : m_slots) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC descriptor{};
        descriptor.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (FAILED(result = m_videoDevice->CreateVideoProcessorOutputView(
                       slot.producerTexture.Get(), m_videoEnumerator.Get(), &descriptor,
                       &slot.outputView))) {
            *error = QStringLiteral("CreateVideoProcessorOutputView failed: %1")
                         .arg(hrText(result));
            return false;
        }
    }
    m_videoWidth = width;
    m_videoHeight = height;
    m_videoInputFormat = format;
    return true;
}

bool D3D11VideoPipeline::submitDecodedFrame(AVFrame *frame, VideoFrameToken token,
                                             QString *error)
{
    QString localError;
    if (!error)
        error = &localError;
    if (!m_initialized || !frame || frame->format != AV_PIX_FMT_D3D11) {
        *error = QStringLiteral("Decoded frame is not an initialized D3D11 hardware frame");
        setError(*error);
        return false;
    }
    if (m_consumerFenceOnProducer)
        m_ring.markConsumerFenceComplete(m_consumerFenceOnProducer->GetCompletedValue());
    const auto slotIndex = m_ring.claimForProducer();
    if (!slotIndex)
        return false;

    auto *input = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    const UINT arraySlice = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
    D3D11_TEXTURE2D_DESC inputDescriptor{};
    input->GetDesc(&inputDescriptor);
    if (!ensureVideoProcessor(frame->width, frame->height, inputDescriptor.Format, error)) {
        setError(*error);
        m_ring.cancelProducer(*slotIndex);
        ++m_deviceErrors;
        return false;
    }
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDescriptor{};
    inputViewDescriptor.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDescriptor.Texture2D.ArraySlice = arraySlice;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT result = m_videoDevice->CreateVideoProcessorInputView(
        input, m_videoEnumerator.Get(), &inputViewDescriptor, &inputView);
    if (FAILED(result)) {
        *error = QStringLiteral("CreateVideoProcessorInputView failed: %1").arg(hrText(result));
        setError(*error);
        m_ring.cancelProducer(*slotIndex);
        ++m_deviceErrors;
        return false;
    }
    const RECT source{0, 0, frame->width, frame->height};
    const RECT target{0, 0, static_cast<LONG>(OutputWidth), static_cast<LONG>(OutputHeight)};
    m_videoContext->VideoProcessorSetStreamSourceRect(m_videoProcessor.Get(), 0, TRUE, &source);
    m_videoContext->VideoProcessorSetStreamDestRect(m_videoProcessor.Get(), 0, TRUE, &target);
    m_videoContext->VideoProcessorSetOutputTargetRect(m_videoProcessor.Get(), TRUE, &target);
    // Colour/HDR decision (matrix, range, untagged-HD fallback, HDR->SDR tone-map) is a pure policy.
    const ColorConversion conversion = resolveColorConversion(
        frame->colorspace, frame->color_range, frame->height, frame->color_trc,
        frame->color_primaries, 8);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
    inputColor.YCbCr_Matrix = conversion.inputYCbCrMatrix();
    inputColor.Nominal_Range = conversion.inputNominalRange();
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
    outputColor.RGB_Range = 0; // full-range RGB output (preserves the frozen prototype's setting)
    outputColor.Nominal_Range = 2;
    m_videoContext->VideoProcessorSetStreamColorSpace(m_videoProcessor.Get(), 0, &inputColor);
    m_videoContext->VideoProcessorSetOutputColorSpace(m_videoProcessor.Get(), &outputColor);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    result = m_videoContext->VideoProcessorBlt(m_videoProcessor.Get(),
                                               m_slots[*slotIndex].outputView.Get(), 0, 1,
                                               &stream);
    // Stamp the frame with the persistent producer fence value (monotonic across reopen); the
    // per-media token.sequence restarts each open and would signal the fence backward.
    token.sequence = ++m_producerFenceValue;
    if (FAILED(result) ||
        FAILED(result = m_producerContext4->Signal(m_producerFence.Get(), token.sequence)) ||
        !m_ring.publishProduced(*slotIndex, token)) {
        *error = QStringLiteral("GPU frame publication failed: %1").arg(hrText(result));
        setError(*error);
        m_ring.cancelProducer(*slotIndex);
        noteHresult(result);
        ++m_deviceErrors;
        return false;
    }
    {
        std::scoped_lock lock(m_mutex);
        m_hardwareFormat = QStringLiteral("d3d11va");
        m_inputFormat = formatName(inputDescriptor.Format);
        m_colorConversion = conversion.describe();
    }
    ++m_submitted;
    return true;
}

bool D3D11VideoPipeline::submitSyntheticFrame(VideoFrameToken token, double phase,
                                               QString *error)
{
    QString localError;
    if (!error)
        error = &localError;
    error->clear();
    if (!m_initialized) {
        *error = QStringLiteral("Video pipeline is not initialized");
        return false;
    }
    if (m_consumerFenceOnProducer)
        m_ring.markConsumerFenceComplete(m_consumerFenceOnProducer->GetCompletedValue());
    const auto slot = m_ring.claimForProducer();
    if (!slot)
        return false;

    const float color[] = {
        static_cast<float>(0.025 + 0.018 * (1.0 + std::sin(phase))),
        static_cast<float>(0.075 + 0.040 * (1.0 + std::sin(phase + 2.1))),
        static_cast<float>(0.130 + 0.055 * (1.0 + std::sin(phase + 4.2))),
        1.0f
    };
    m_producerContext->ClearRenderTargetView(m_slots[*slot].producerTarget.Get(), color);
    token.sequence = ++m_producerFenceValue; // persistent fence value; see submitDecodedFrame
    HRESULT result = m_producerContext4->Signal(m_producerFence.Get(), token.sequence);
    if (FAILED(result) || !m_ring.publishProduced(*slot, token)) {
        m_ring.cancelProducer(*slot);
        *error = QStringLiteral("Synthetic frame publication failed: %1").arg(hrText(result));
        setError(*error);
        noteHresult(result);
        ++m_deviceErrors;
        return false;
    }
    {
        std::scoped_lock lock(m_mutex);
        m_hardwareFormat = QStringLiteral("D3D11 synthetic");
        m_inputFormat = QStringLiteral("RGBA8");
    }
    ++m_submitted;
    return true;
}

std::optional<D3D11VideoPipeline::PresentationFrame>
D3D11VideoPipeline::acquirePresentationFrame(quint64 generation)
{
    const auto selection = m_ring.acquireLatestForConsumer(generation);
    if (!selection)
        return std::nullopt;
    return PresentationFrame{selection->slot, selection->token, selection->retiringSlot};
}

std::optional<D3D11VideoPipeline::PresentationFrame>
D3D11VideoPipeline::acquirePresentationFrame()
{
    const auto selection = m_ring.acquireLatestForConsumer();
    if (!selection)
        return std::nullopt;
    return PresentationFrame{selection->slot, selection->token, selection->retiringSlot};
}

std::optional<VideoFrameToken>
D3D11VideoPipeline::acquireLatestForPresentation(quint64 generation)
{
    if (const auto frame = acquirePresentationFrame(generation))
        return frame->token;
    return std::nullopt;
}

bool D3D11VideoPipeline::waitForProducer(quint64 sequence)
{
    if (!m_consumerContext4 ||
        FAILED(m_consumerContext4->Wait(m_producerFenceOnConsumer.Get(), sequence))) {
        ++m_deviceErrors;
        return false;
    }
    return true;
}

bool D3D11VideoPipeline::retireAfterRendering(std::size_t slot)
{
    const quint64 value = ++m_consumerFenceValue;
    if (!m_consumerContext4 || FAILED(m_consumerContext4->Signal(m_consumerFence.Get(), value)) ||
        !m_ring.retireAfterConsumerSubmission(slot, value)) {
        ++m_deviceErrors;
        return false;
    }
    return true;
}

void D3D11VideoPipeline::retirePresentedFrame(quint64 consumerFenceValue)
{
    m_ring.markConsumerFenceComplete(consumerFenceValue);
}

void D3D11VideoPipeline::flush(quint64 nextGeneration)
{
    m_ring.flush(nextGeneration);
    m_scheduledLateDrops.store(0, std::memory_order_release);
    m_lastAvErrorUs.store(0, std::memory_order_release);
    std::scoped_lock lock(m_timingMutex);
    m_schedulingAbsoluteErrorsUs.clear();
}
ID3D11Texture2D *D3D11VideoPipeline::consumerTexture(std::size_t slot) const
{
    return slot < m_slots.size() ? m_slots[slot].consumerTexture.Get() : nullptr;
}
QSize D3D11VideoPipeline::textureSize() const
{
    return QSize(static_cast<int>(OutputWidth), static_cast<int>(OutputHeight));
}
void D3D11VideoPipeline::noteDecoded() { ++m_decoded; }
void D3D11VideoPipeline::noteSchedulingDecision(qint64 timingErrorUs, bool dropped)
{
    m_lastAvErrorUs.store(timingErrorUs, std::memory_order_relaxed);
    {
        std::scoped_lock lock(m_timingMutex);
        m_schedulingAbsoluteErrorsUs.push_back(std::llabs(timingErrorUs));
    }
    if (dropped)
        ++m_scheduledLateDrops;
}
qint64 D3D11VideoPipeline::schedulingP95AbsoluteErrorUs() const
{
    std::vector<qint64> samples;
    {
        std::scoped_lock lock(m_timingMutex);
        samples = m_schedulingAbsoluteErrorsUs;
    }
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>((samples.size() - 1) * 0.95)];
}
void D3D11VideoPipeline::notePresented() { ++m_presented; }

D3D11VideoPipeline::Diagnostics D3D11VideoPipeline::diagnostics() const
{
    std::scoped_lock lock(m_mutex);
    return Diagnostics{m_qtAdapter, m_producerAdapter, m_adapterMatch, m_sharedFences,
                       m_decoded.load(), m_submitted.load(), m_presented.load(),
                       m_ring.producerStarvationCount(), m_scheduledLateDrops.load(),
                       m_lastAvErrorUs.load(), 0,
                       m_deviceErrors.load(),
                       m_hardwareFormat, m_inputFormat, m_colorConversion,
                       m_deviceLost.load(), m_error};
}

void D3D11VideoPipeline::noteHresult(long hr)
{
    bool removed = hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
    if (!removed && m_producerDevice)
        removed = FAILED(m_producerDevice->GetDeviceRemovedReason());
    if (removed) {
        m_deviceLostReason.store(DeviceLostReason::VideoDeviceRemoved, std::memory_order_release);
        m_deviceLost.store(true, std::memory_order_release);
    }
}

bool D3D11VideoPipeline::deviceLost() const noexcept
{
    return m_deviceLost.load(std::memory_order_acquire);
}

DeviceLostReason D3D11VideoPipeline::deviceLostReason() const noexcept
{
    return m_deviceLostReason.load(std::memory_order_acquire);
}

void D3D11VideoPipeline::shutdown()
{
    for (TextureSlot &slot : m_slots)
        slot = TextureSlot{};
    m_consumerFenceOnProducer.Reset();
    m_consumerFence.Reset();
    m_producerFenceOnConsumer.Reset();
    m_producerFence.Reset();
    m_videoProcessor.Reset();
    m_videoEnumerator.Reset();
    m_videoContext.Reset();
    m_videoDevice.Reset();
    m_consumerContext4.Reset();
    m_consumerContext.Reset();
    m_consumerDevice5.Reset();
    m_consumerDevice.Reset();
    m_producerContext4.Reset();
    m_producerContext.Reset();
    m_producerDevice5.Reset();
    m_producerDevice.Reset();
    m_deviceLost.store(false, std::memory_order_release);
    m_deviceLostReason.store(DeviceLostReason::None, std::memory_order_release);
    m_initialized = false;
}

} // namespace Colosseum::Player2
