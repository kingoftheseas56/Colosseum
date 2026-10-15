[CmdletBinding()]
param(
    [string]$Scene = "",
    [string]$RemotionRoot = "",
    [string]$Output = "",
    [switch]$CaptureOnly,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    throw "QML trailer: $Message"
}

function Resolve-AbsolutePath([string]$Path, [string]$Base) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Require-File([string]$Path, [string]$Label) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        Fail "$Label not found: $Path"
    }
}

function Require-Directory([string]$Path, [string]$Label) {
    if (!(Test-Path -LiteralPath $Path -PathType Container)) {
        Fail "$Label not found: $Path"
    }
}

function Write-JsonFile([string]$Path, $Value) {
    $dir = Split-Path -Parent $Path
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $json = $Value | ConvertTo-Json -Depth 32
    [IO.File]::WriteAllText($Path, $json, (New-Object Text.UTF8Encoding($false)))
}

function Get-RelativePathCompat([string]$BasePath, [string]$TargetPath) {
    $base = [IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'
    $target = [IO.Path]::GetFullPath($TargetPath)
    $baseUri = New-Object Uri($base)
    $targetUri = New-Object Uri($target)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/', '\')
}

function Invoke-LanistaScene {
    param(
        [string]$Lanista,
        [string[]]$Arguments,
        [string]$FixtureRoot,
        [string]$LogPath,
        [int]$MaxInfraAttempts = 3
    )

    $hadFixture = Test-Path Env:COLOSSEUM_TRAILER_FIXTURE_ROOT
    $oldFixture = $env:COLOSSEUM_TRAILER_FIXTURE_ROOT

    try {
        if ([string]::IsNullOrWhiteSpace($FixtureRoot)) {
            Remove-Item Env:COLOSSEUM_TRAILER_FIXTURE_ROOT -ErrorAction SilentlyContinue
        } else {
            $env:COLOSSEUM_TRAILER_FIXTURE_ROOT = $FixtureRoot
        }

        for ($attempt = 1; $attempt -le $MaxInfraAttempts; $attempt++) {
            $stamp = Get-Date -Format o
            $lines = @("[$stamp] attempt $attempt/$MaxInfraAttempts", "command: $Lanista $($Arguments -join ' ')")
            $result = & $Lanista @Arguments 2>&1
            $exitCode = $LASTEXITCODE
            $lines += @($result | ForEach-Object { [string]$_ })
            $lines += "exitCode=$exitCode"
            $lines | Add-Content -LiteralPath $LogPath -Encoding UTF8
            $result | ForEach-Object { Write-Host $_ }

            if ($exitCode -eq 0) {
                $sessionId = ""
                foreach ($line in $result) {
                    if ([string]$line -match '^SESSION\s+([^\s]+)\s+pipe=') {
                        $sessionId = $Matches[1]
                        break
                    }
                }
                return [pscustomobject]@{ ExitCode = 0; SessionId = $sessionId }
            }

            if ($exitCode -eq 4 -and $attempt -lt $MaxInfraAttempts) {
                Write-Warning "Lanista infrastructure failure for scene. Retrying in 4 seconds."
                Start-Sleep -Seconds 4
                continue
            }

            return [pscustomobject]@{ ExitCode = $exitCode; SessionId = "" }
        }
    }
    finally {
        if ($hadFixture) {
            $env:COLOSSEUM_TRAILER_FIXTURE_ROOT = $oldFixture
        } else {
            Remove-Item Env:COLOSSEUM_TRAILER_FIXTURE_ROOT -ErrorAction SilentlyContinue
        }
    }
}

function Resolve-RemotionRoot([string]$Requested, [string]$ColosseumRoot) {
    if (![string]::IsNullOrWhiteSpace($Requested)) {
        $resolved = Resolve-AbsolutePath $Requested $ColosseumRoot
        Require-Directory $resolved "Remotion root"
        return $resolved
    }

    $repoParent = Split-Path -Parent $ColosseumRoot
    $repoGrandParent = Split-Path -Parent $repoParent
    $candidates = @(
        (Join-Path $repoParent 'Colosseum-Trailer-Remotion'),
        (Join-Path $repoGrandParent 'Colosseum-Trailer-Remotion')
    )
    if ($env:USERPROFILE) {
        $candidates += Join-Path $env:USERPROFILE 'Desktop\Colosseum-Trailer-Remotion'
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    Fail "Remotion root not found. Pass -RemotionRoot explicitly."
}

function Assert-LanistaTrailerContract([string]$Lanista) {
    $help = (& $Lanista --help 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { Fail "lanista --help failed" }
    foreach ($flag in @('--capture-width', '--capture-height', '--capture-out')) {
        if (!$help.Contains($flag)) {
            Fail "Lanista does not expose $flag yet. Implement Task 3 before running trailer capture."
        }
    }
}

function Copy-SessionEvidence([string]$Root, [string]$SessionId, [string]$Destination) {
    if ([string]::IsNullOrWhiteSpace($SessionId)) { return }
    $source = Join-Path $Root ("artifacts\lanista-sessions\{0}" -f $SessionId)
    if (!(Test-Path -LiteralPath $source -PathType Container)) { return }
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    Copy-Item -LiteralPath $source -Destination $Destination -Recurse -Force
}

function Get-SceneClip([string]$SceneOut, [string]$SceneId) {
    $clips = @(Get-ChildItem -LiteralPath $SceneOut -File -Filter *.mp4 -ErrorAction SilentlyContinue)
    if ($clips.Count -ne 1) {
        Fail "scene '$SceneId' must produce exactly one MP4, got $($clips.Count) in $SceneOut"
    }
    if ($clips[0].Length -lt 1000) { Fail "scene '$SceneId' MP4 is unexpectedly small" }
    return $clips[0].FullName
}

function Invoke-SceneQa {
    param(
        [string]$QaScript,
        [string]$SceneId,
        [string]$Clip,
        [string]$Scenario,
        [string]$Fixture,
        [string]$RunRoot,
        [string]$Colosseum,
        [int]$Width,
        [int]$Height,
        [int]$TargetFps,
        [string]$RepoRoot
    )

    if (!(Test-Path -LiteralPath $QaScript -PathType Leaf)) {
        return $null
    }

    $args = @{
        SceneId = $SceneId
        Clip = $Clip
        Scenario = $Scenario
        RunRoot = $RunRoot
        Colosseum = $Colosseum
        LanistaHeader = (Join-Path $RepoRoot 'native\tools\LanistaCapture.h')
        LanistaSource = (Join-Path $RepoRoot 'native\tools\lanista.cpp')
        ExpectedWidth = $Width
        ExpectedHeight = $Height
        TargetFps = $TargetFps
    }
    if (![string]::IsNullOrWhiteSpace($Fixture)) { $args.Fixture = $Fixture }

    & $QaScript @args
    if ($LASTEXITCODE -ne 0) { Fail "QA failed for scene '$SceneId'" }

    $resultPath = Join-Path $RunRoot ("qa\{0}\result.json" -f $SceneId)
    Require-File $resultPath "QA result for $SceneId"
    $qa = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if (!$qa.accepted) { Fail "QA rejected scene '$SceneId'" }
    return $qa
}

function Get-DefaultOutput([string]$RemotionRoot) {
    return Join-Path $RemotionRoot 'renders\Colosseum-QML-Trailer-Final.mp4'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = [IO.Path]::GetFullPath((Join-Path $scriptDir '..'))
$manifestPath = Join-Path $root 'tests\lanista_capture\trailer\manifest.json'
$qaScript = Join-Path $root 'tests\lanista_capture\trailer\qa_scene.ps1'
$buildDir = Join-Path $root 'native\build-msvc'
$colosseum = Join-Path $buildDir 'colosseum.exe'
$lanista = Join-Path $buildDir 'lanista.exe'
$ffmpeg = Join-Path $buildDir 'tools\ffmpeg.exe'

Write-Host "Colosseum QML trailer pipeline"
Write-Host "repo: $root"

if (!$SkipBuild) {
    Require-Directory $buildDir "native build directory"
    Write-Host "Building Colosseum + Lanista..."
    & cmake --build $buildDir --config RelWithDebInfo --target colosseum lanista
    if ($LASTEXITCODE -ne 0) { Fail "native build failed" }
}

Require-File $colosseum "Colosseum executable"
Require-File $lanista "Lanista executable"
Require-File $ffmpeg "bundled FFmpeg"
Require-File $manifestPath "trailer manifest"
Assert-LanistaTrailerContract $lanista

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'colosseum.trailer.v1') {
    Fail "unsupported manifest schema '$($manifest.schema)'"
}

$captureWidth = [int]$manifest.capture.width
$captureHeight = [int]$manifest.capture.height
$targetFps = [int]$manifest.capture.targetFps
if ($captureWidth -ne 2560 -or $captureHeight -ne 1440) {
    Fail "trailer manifest must capture at exactly 2560x1440"
}
if ($targetFps -le 0) { Fail "manifest targetFps must be positive" }

$allScenes = @($manifest.scenes)
if ($allScenes.Count -eq 0) { Fail "manifest has no scenes" }
$duplicateIds = @($allScenes | Group-Object id | Where-Object Count -gt 1)
if ($duplicateIds.Count -gt 0) {
    Fail "duplicate scene id(s): $($duplicateIds.Name -join ', ')"
}

if (![string]::IsNullOrWhiteSpace($Scene)) {
    $selected = @($allScenes | Where-Object { $_.id -eq $Scene })
    if ($selected.Count -ne 1) { Fail "unknown scene '$Scene'" }
} else {
    $selected = @($allScenes | Where-Object { $_.enabled -eq $true })
}
if ($selected.Count -eq 0) { Fail "no scenes selected" }

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runRoot = Join-Path $root ("artifacts\trailer-runs\{0}" -f $timestamp)
$scenesRoot = Join-Path $runRoot 'scenes'
$sessionsRoot = Join-Path $runRoot 'sessions'
$qaRoot = Join-Path $runRoot 'qa'
New-Item -ItemType Directory -Force -Path $scenesRoot, $sessionsRoot, $qaRoot | Out-Null
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $runRoot 'manifest.json') -Force

$runRecord = [ordered]@{
    schema = 'colosseum.trailer-run.v1'
    startedAt = (Get-Date).ToString('o')
    finishedAt = $null
    repoRoot = $root
    sceneFilter = $Scene
    captureOnly = [bool]$CaptureOnly
    capture = [ordered]@{ width = $captureWidth; height = $captureHeight; targetFps = $targetFps }
    scenes = @()
    status = 'running'
}
Write-JsonFile (Join-Path $runRoot 'run.json') $runRecord

$sceneRecords = @()

try {
    foreach ($entry in $selected) {
        $sceneId = [string]$entry.id
        if ($sceneId -notmatch '^[a-z0-9][a-z0-9-]{0,63}$') {
            Fail "unsafe scene id '$sceneId'"
        }

        Write-Host "`n=== scene: $sceneId ==="
        $scenario = Resolve-AbsolutePath ([string]$entry.scenario) $root
        Require-File $scenario "scenario for $sceneId"

        $seed = ""
        if ($entry.PSObject.Properties.Name -contains 'seed' -and ![string]::IsNullOrWhiteSpace([string]$entry.seed)) {
            $seed = Resolve-AbsolutePath ([string]$entry.seed) $root
            Require-Directory $seed "seed for $sceneId"
        }

        $fixture = ""
        if ($entry.PSObject.Properties.Name -contains 'fixture' -and ![string]::IsNullOrWhiteSpace([string]$entry.fixture)) {
            $fixture = Resolve-AbsolutePath ([string]$entry.fixture) $root
            Require-Directory $fixture "fixture for $sceneId"
        }

        $sceneOut = Join-Path $scenesRoot $sceneId
        New-Item -ItemType Directory -Force -Path $sceneOut | Out-Null
        $sceneLog = Join-Path $sceneOut 'lanista.log'

        $lanistaArgs = @(
            'session', 'run', $scenario,
            '--exe', $colosseum,
            '--qml', (Join-Path $root 'qml\Main.qml'),
            '--drive',
            '--ready-ms', '60000',
            '--capture-width', [string]$captureWidth,
            '--capture-height', [string]$captureHeight,
            '--capture-out', $sceneOut
        )
        if ($seed) { $lanistaArgs += @('--seed', $seed) }

        $run = Invoke-LanistaScene -Lanista $lanista -Arguments $lanistaArgs -FixtureRoot $fixture -LogPath $sceneLog
        if ($run.ExitCode -ne 0) {
            Fail "scene '$sceneId' failed with Lanista exit $($run.ExitCode)"
        }

        Copy-SessionEvidence -Root $root -SessionId $run.SessionId -Destination (Join-Path $sessionsRoot $sceneId)
        $clip = Get-SceneClip -SceneOut $sceneOut -SceneId $sceneId
        $qa = Invoke-SceneQa -QaScript $qaScript -SceneId $sceneId -Clip $clip -Scenario $scenario -Fixture $fixture -RunRoot $runRoot -Colosseum $colosseum -Width $captureWidth -Height $captureHeight -TargetFps $targetFps -RepoRoot $root

        if ($null -eq $qa -and !$CaptureOnly) {
            Fail "scene QA helper is missing. Implement Task 8 before full trailer render: $qaScript"
        }
        if ($null -eq $qa) {
            Write-Warning "QA helper not present yet. Raw capture is preserved because -CaptureOnly was requested."
        }

        $record = [ordered]@{
            id = $sceneId
            clip = $clip
            scenario = $scenario
            seed = $seed
            fixture = $fixture
            sessionId = $run.SessionId
            accepted = if ($null -ne $qa) { [bool]$qa.accepted } else { $false }
            qa = if ($null -ne $qa) { Join-Path $runRoot ("qa\{0}\result.json" -f $sceneId) } else { "" }
            camera = $entry.camera
        }
        if ($null -ne $qa -and $qa.PSObject.Properties.Name -contains 'durationSeconds') {
            $record.durationSeconds = [double]$qa.durationSeconds
        }
        $sceneRecords += [pscustomobject]$record

        $runRecord.scenes = $sceneRecords
        Write-JsonFile (Join-Path $runRoot 'run.json') $runRecord
    }

    if ($CaptureOnly) {
        $runRecord.status = 'capture-complete'
        $runRecord.finishedAt = (Get-Date).ToString('o')
        $runRecord.scenes = $sceneRecords
        Write-JsonFile (Join-Path $runRoot 'run.json') $runRecord
        Write-Host "`nQML_CAPTURE_OK"
        Write-Host "scenes: $($sceneRecords.Count)"
        Write-Host "run: $runRoot"
        foreach ($r in $sceneRecords) { Write-Host ("{0}: {1}" -f $r.id, $r.clip) }
        exit 0
    }

    $unaccepted = @($sceneRecords | Where-Object { !$_.accepted })
    if ($unaccepted.Count -gt 0) {
        Fail "unaccepted scene(s) block Remotion staging: $($unaccepted.id -join ', ')"
    }

    $remotion = Resolve-RemotionRoot $RemotionRoot $root
    $generated = Join-Path $remotion 'public\qml-trailer\generated'
    New-Item -ItemType Directory -Force -Path $generated | Out-Null

    Get-ChildItem -LiteralPath $generated -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.mp4', '.json') } |
        Remove-Item -Force

    $stagedScenes = @()
    foreach ($r in $sceneRecords) {
        if (!($r.PSObject.Properties.Name -contains 'durationSeconds')) {
            Fail "QA result for '$($r.id)' did not provide durationSeconds"
        }
        $dest = Join-Path $generated ("{0}.mp4" -f $r.id)
        Copy-Item -LiteralPath $r.clip -Destination $dest -Force

        $sourceEntry = $allScenes | Where-Object { $_.id -eq $r.id } | Select-Object -First 1
        $durationFrames = [Math]::Max(1, [int][Math]::Ceiling(([double]$r.durationSeconds) * 30.0))
        $stage = [ordered]@{
            id = $r.id
            src = ("qml-trailer/generated/{0}.mp4" -f $r.id)
            durationFrames = $durationFrames
            source = [ordered]@{ width = $captureWidth; height = $captureHeight }
            camera = $sourceEntry.camera
        }
        if ($sourceEntry.PSObject.Properties.Name -contains 'title' -and $null -ne $sourceEntry.title) {
            $stage.title = $sourceEntry.title
        }
        $stagedScenes += [pscustomobject]$stage
    }

    $stagedManifest = [ordered]@{
        schema = 'colosseum.qml-trailer.v1'
        width = 1920
        height = 1080
        fps = 30
        generatedAt = (Get-Date).ToString('o')
        sourceRun = $runRoot
        scenes = $stagedScenes
    }
    $stagedManifestPath = Join-Path $generated 'manifest.json'
    Write-JsonFile $stagedManifestPath $stagedManifest

    $manifestTest = Join-Path $remotion 'tests\qml-trailer-manifest.test.mjs'
    $structureTest = Join-Path $remotion 'tests\qml-trailer-structure.test.mjs'
    $renderScript = Join-Path $remotion 'render-qml-trailer.ps1'
    Require-File $manifestTest "Remotion QML trailer manifest test"
    Require-File $structureTest "Remotion QML trailer structure test"
    Require-File $renderScript "Remotion QML trailer render script"

    Write-Host "`nRunning Remotion trailer tests..."
    Push-Location $remotion
    try {
        & node --test $manifestTest $structureTest
        if ($LASTEXITCODE -ne 0) { Fail "Remotion QML trailer tests failed" }
    }
    finally { Pop-Location }

    if ([string]::IsNullOrWhiteSpace($Output)) {
        $Output = Get-DefaultOutput $remotion
    } else {
        $Output = Resolve-AbsolutePath $Output $remotion
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

    Write-Host "`nRendering final trailer..."
    & $renderScript -Output $Output -RunRoot $runRoot
    if ($LASTEXITCODE -ne 0) { Fail "Remotion render failed" }
    Require-File $Output "final QML trailer"
    if ((Get-Item -LiteralPath $Output).Length -lt 100000) {
        Fail "final trailer is unexpectedly small: $Output"
    }

    $sha = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
    $finalResult = Join-Path $remotion 'renders\qml-trailer-final-verify\result.json'
    $duration = "unknown"
    if (Test-Path -LiteralPath $finalResult -PathType Leaf) {
        $verified = Get-Content -LiteralPath $finalResult -Raw | ConvertFrom-Json
        if (!$verified.accepted) { Fail "final Remotion verification rejected the MP4" }
        if ($verified.PSObject.Properties.Name -contains 'durationSeconds') {
            $duration = [string]$verified.durationSeconds
        }
    }

    $contact = Join-Path $remotion 'renders\qml-trailer-final-verify\contact.jpg'
    $runRecord.status = 'complete'
    $runRecord.finishedAt = (Get-Date).ToString('o')
    $runRecord.scenes = $sceneRecords
    $runRecord.final = [ordered]@{
        path = $Output
        sha256 = $sha
        durationSeconds = $duration
        resolution = '1920x1080'
        fps = 30
        qa = if (Test-Path -LiteralPath $contact) { $contact } else { "" }
    }
    Write-JsonFile (Join-Path $runRoot 'run.json') $runRecord

    Write-Host "`nQML_TRAILER_OK"
    Write-Host "source scenes: $($sceneRecords.Count)/$($sceneRecords.Count) accepted"
    Write-Host "master: $Output"
    Write-Host "duration: $duration"
    Write-Host "resolution: 1920x1080"
    Write-Host "fps: 30"
    Write-Host "sha256: $sha"
    if (Test-Path -LiteralPath $contact) { Write-Host "qa: $contact" }
}
catch {
    $runRecord.status = 'failed'
    $runRecord.finishedAt = (Get-Date).ToString('o')
    $runRecord.error = $_.Exception.Message
    $runRecord.scenes = $sceneRecords
    Write-JsonFile (Join-Path $runRoot 'run.json') $runRecord
    Write-Error $_
    exit 1
}
