$ErrorActionPreference = 'Stop'

$prototype = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content -Raw (Join-Path $prototype 'CMakeLists.txt')
$slotRing = Get-Content -Raw (Join-Path $prototype 'src\slot_ring.h')
$bridgeFiles = @(
    'src\shared_bridge.h',
    'src\shared_bridge.cpp',
    'src\frame_producer.h',
    'src\frame_producer.cpp',
    'src\ffmpeg_hevc_source.h',
    'src\ffmpeg_hevc_source.cpp',
    'src\video_bridge_item.h',
    'src\video_bridge_item.cpp',
    'src\main.cpp'
)
$bridgeSource = ($bridgeFiles | ForEach-Object {
    $path = Join-Path $prototype $_
    if (Test-Path $path) { Get-Content -Raw $path }
}) -join "`n"
$qml = if (Test-Path (Join-Path $prototype 'qml\Main.qml')) {
    Get-Content -Raw (Join-Path $prototype 'qml\Main.qml')
} else { '' }

function Require-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "contract failure: $Message"
    }
}

function Reject-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "contract failure: $Message"
    }
}

Require-Text $cmake 'add_executable\(slot_ring_test' 'isolated slot-ring harness target is required'
Require-Text $slotRing 'enum class SlotState' 'slot state must be explicit'
Require-Text $slotRing 'Displaying' 'displayed slots must be represented'
Require-Text $slotRing 'Retiring' 'consumer retirement must be represented'
Require-Text $slotRing 'consumerFenceValue' 'slot reuse must depend on a consumer fence value'
Require-Text $bridgeSource 'QQuickWindow::setGraphicsApi\(QSGRendererInterface::Direct3D11\)' 'Qt Quick must select Direct3D 11'
Require-Text $bridgeSource 'QNativeInterface::QSGD3D11Texture::fromNative' 'consumer must use public D3D11 native texture import'
Require-Text $bridgeSource 'D3D11_RESOURCE_MISC_SHARED;' 'textures must use Kodi-style legacy shared handles'
Require-Text $bridgeSource 'GetSharedHandle' 'producer must export the legacy shared texture handle'
Require-Text $bridgeSource 'OpenSharedResource\(' 'consumer must open the legacy shared texture handle'
Require-Text $bridgeSource 'DXGI_FORMAT_R8G8B8A8_UNORM' 'Qt native texture import supports RGBA, not BGRA'
Require-Text $bridgeSource 'CreateFence' 'producer and consumer fences must be explicit'
Require-Text $bridgeSource 'OpenSharedFence' 'fences must cross D3D11 devices'
Require-Text $bridgeSource 'Signal\(' 'a device context must signal fences'
Require-Text $bridgeSource 'Wait\(' 'a device context must wait on fences'
Reject-Text $bridgeSource 'QQuickFramebufferObject' 'prototype must not use an offscreen Qt Quick FBO'
Reject-Text $bridgeSource 'OpenGL|QOpenGL|wgl' 'prototype must not use OpenGL interop'
Reject-Text $bridgeSource 'av_hwframe_transfer_data' 'prototype must not download decoded video to the CPU'
Reject-Text $bridgeSource 'CopyResource\(' 'Stage 1 must render directly into its shared ring slot'
Reject-Text $bridgeSource 'D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX' 'legacy shared textures plus fences must not inherit an unowned keyed mutex'
Reject-Text $bridgeSource 'DXGI_FORMAT_B8G8R8A8_UNORM' 'Qt native texture wrapper has no BGRA format parameter'
Require-Text $qml 'sequence:\s*"F11"' 'Gate A must provide a repeatable fullscreen toggle'
Require-Text $bridgeSource 'AV_HWDEVICE_TYPE_D3D11VA' 'Gate B must construct a D3D11VA hardware context'
Require-Text $bridgeSource 'AV_PIX_FMT_D3D11' 'Gate B must require D3D11 decoder frames'
Require-Text $bridgeSource 'ID3D11VideoProcessorInputView' 'Gate B must create a video-processor input view'
Require-Text $bridgeSource 'ID3D11VideoProcessorOutputView' 'Gate B must create a video-processor output view'
Require-Text $bridgeSource 'VideoProcessorBlt' 'Gate B must convert NV12/P010 on the GPU'
Require-Text $bridgeSource 'VideoProcessorSetStreamColorSpace' 'Gate B must set input color metadata explicitly'
Require-Text $bridgeSource 'AVCOL_SPC_UNSPECIFIED' 'untagged HD video must receive an explicit color-matrix fallback'
Reject-Text $bridgeSource 'sws_scale|UpdateSubresource\(' 'Gate B must not upload CPU video frames'

Write-Output 'prototype_contract_test: PASS'
