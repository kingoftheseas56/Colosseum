$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$player2Root = Join-Path $repoRoot 'native\player2'

$requiredFiles = @(
    'video\D3D11TextureRing.h',
    'video\D3D11TextureRing.cpp',
    'video\D3D11VideoPipeline.h',
    'video\D3D11VideoPipeline.cpp',
    'video\Player2VideoItem.h',
    'video\Player2VideoItem.cpp'
)

foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path (Join-Path $player2Root $relativePath))) {
        throw "contract failure: missing native/player2/$($relativePath -replace '\\', '/')"
    }
}

$cmake = Get-Content -Raw (Join-Path $player2Root 'CMakeLists.txt')
$pipeline = Get-Content -Raw (Join-Path $player2Root 'video\D3D11VideoPipeline.cpp')
$pipelineHeader = Get-Content -Raw (Join-Path $player2Root 'video\D3D11VideoPipeline.h')
$item = Get-Content -Raw (Join-Path $player2Root 'video\Player2VideoItem.cpp')
$itemHeader = Get-Content -Raw (Join-Path $player2Root 'video\Player2VideoItem.h')
$videoSource = $pipeline + "`n" + $pipelineHeader + "`n" + $item + "`n" + $itemHeader

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

Require-Text $cmake 'D3D11TextureRing\.cpp' 'the real texture ring must be compiled into player2_core'
Require-Text $cmake 'D3D11VideoPipeline\.cpp' 'the real video pipeline must be compiled into player2_core'
Require-Text $cmake 'Player2VideoItem\.cpp' 'the Qt Quick item must be compiled into player2_core'
Require-Text $cmake '(?s)target_link_libraries\(player2_core.*\bd3d11\b.*\bdxgi\b' 'player2_core must link D3D11 and DXGI'

Require-Text $videoSource 'QSGRendererInterface::Direct3D11' 'the item must require the Qt Quick D3D11 backend'
Require-Text $videoSource 'QNativeInterface::QSGD3D11Texture::fromNative' 'consumer must use public D3D11 native texture import'
Require-Text $videoSource 'D3D11_RESOURCE_MISC_SHARED;' 'ring textures must use Kodi-style legacy shared handles'
Require-Text $videoSource 'GetSharedHandle' 'producer must export a shared texture handle'
Require-Text $videoSource 'OpenSharedResource\(' 'consumer must open the shared texture handle'
Require-Text $videoSource 'CreateFence' 'producer and consumer fences must be explicit'
Require-Text $videoSource 'OpenSharedFence' 'fences must cross D3D11 devices'
Require-Text $videoSource 'Signal\(' 'a D3D11 context must signal fences'
Require-Text $videoSource 'Wait\(' 'a D3D11 context must wait on fences'
Require-Text $videoSource 'AV_HWDEVICE_TYPE_D3D11VA' 'decoder must construct a D3D11VA hardware context'
Require-Text $videoSource 'AV_PIX_FMT_D3D11' 'decoded frames must remain D3D11 textures'
Require-Text $videoSource 'ID3D11VideoProcessorInputView' 'video processor input view is required'
Require-Text $videoSource 'ID3D11VideoProcessorOutputView' 'video processor output view is required'
Require-Text $videoSource 'VideoProcessorBlt' 'NV12/P010 conversion must stay on the GPU'
Require-Text $videoSource 'VideoProcessorSetStreamColorSpace' 'input color metadata must be explicit'
Require-Text $videoSource 'AVCOL_SPC_UNSPECIFIED' 'untagged HD must receive an explicit matrix fallback'
Require-Text $videoSource 'DXGI_FORMAT_R8G8B8A8_UNORM' 'Qt native texture import requires RGBA output'
Require-Text $pipelineHeader 'submitDecodedFrame\s*\(AVFrame\s*\*' 'decode delivery must use the typed frame submission boundary'
Require-Text $itemHeader 'Q_PROPERTY\(QObject\s*\*session' 'the paint item must expose only the typed session seam to QML'

Reject-Text $videoSource 'av_hwframe_transfer_data|sws_scale|UpdateSubresource\(' 'video must never download or upload through the CPU'
Reject-Text $videoSource 'QQuickFramebufferObject|QOpenGL|OpenGL|wgl' 'Player 2 video must not use the old OpenGL/FBO path'
Reject-Text $videoSource 'CopyResource\(' 'conversion must render directly into a shared ring slot'
Reject-Text $videoSource 'D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX' 'shared textures must use the proven legacy-handle plus fence contract'
Reject-Text $item 'avformat_open_input|av_read_frame|AVCodecContext|std::thread|CreateThread' 'Player2VideoItem must paint only; it cannot open media or own decode work'

Write-Output 'player2_zero_copy_contract: PASS'
