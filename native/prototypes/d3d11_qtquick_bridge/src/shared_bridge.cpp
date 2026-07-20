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
    m_producerContext1.Reset();
    m_producerContext.Reset();
    m_producerDevice5.Reset();
    m_producerDevice.Reset();
    m_initialized = false;
}
