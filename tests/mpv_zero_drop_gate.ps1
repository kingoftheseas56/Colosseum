param(
    [Parameter(Mandatory = $true)]
    [string]$Exe,

    [Parameter(Mandatory = $true)]
    [string]$Clip,

    [ValidateRange(1, 86400)]
    [int]$WarmupSeconds = 30,

    [ValidateRange(1, 86400)]
    [int]$MeasureSeconds = 300,

    [ValidateRange(1, 100)]
    [int]$Runs = 2
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Resolve-InputPath([string]$Path, [string]$BasePath) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    return (Resolve-Path -LiteralPath (Join-Path $BasePath $Path)).Path
}

$exePath = Resolve-InputPath $Exe $root
$clipPath = Resolve-InputPath $Clip $root
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$artifactDir = Join-Path $root "artifacts\mpv-zero-drop\$stamp"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

$savedPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
$savedMpv = [Environment]::GetEnvironmentVariable('COLOSSEUM_MPV', 'Process')
$savedClip = [Environment]::GetEnvironmentVariable('COLOSSEUM_ABBA_CLIP', 'Process')
$savedProbe = [Environment]::GetEnvironmentVariable('COLOSSEUM_MPV_DROP_PROBE', 'Process')
$savedQtStderr = [Environment]::GetEnvironmentVariable('QT_FORCE_STDERR_LOGGING', 'Process')
$savedQtLicense = [Environment]::GetEnvironmentVariable('QTFRAMEWORK_BYPASS_LICENSE_CHECK', 'Process')

try {
    # This worktree stages FFmpeg beside the exe, but MpvQt/mpv remain in the main-tree build.
    # Redirected GUI-process launches use CreateProcess directly, so supply the runtime search
    # path explicitly rather than relying on Explorer/App Paths behavior.
    $gitCommonDir = (& git -C $root rev-parse --git-common-dir).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'could not resolve the main Git worktree' }
    $mainRoot = Split-Path -Parent $gitCommonDir
    $runtimeDirs = @(
        (Split-Path -Parent $exePath),
        'C:\Qt\6.11.1\msvc2022_64\bin',
        (Join-Path $mainRoot 'native\build-msvc')
    ) | Where-Object { Test-Path -LiteralPath $_ }
    $env:PATH = (($runtimeDirs + @($savedPath)) -join ';')
    $env:COLOSSEUM_MPV = '1'
    $env:COLOSSEUM_ABBA_CLIP = $clipPath
    $env:COLOSSEUM_MPV_DROP_PROBE = "$WarmupSeconds,$MeasureSeconds"
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
    $timeoutMilliseconds = ($WarmupSeconds + $MeasureSeconds + 60) * 1000

    for ($run = 1; $run -le $Runs; $run++) {
        $stdoutLog = Join-Path $artifactDir "run-$run.stdout.log"
        $stderrLog = Join-Path $artifactDir "run-$run.stderr.log"
        $process = Start-Process -FilePath $exePath -ArgumentList @('qml\Main.qml') `
            -WorkingDirectory $root -PassThru `
            -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

        if (-not $process.WaitForExit($timeoutMilliseconds)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
            throw "run $run timed out after $($WarmupSeconds + $MeasureSeconds + 60) seconds"
        }

        [string]$text = Get-Content -Raw -LiteralPath $stderrLog
        $matches = [regex]::Matches($text, 'MPV_DROP_PROBE RESULT\s+(\{[^\r\n]+\})')
        if ($matches.Count -ne 1) {
            throw "run $run expected exactly one structured probe result; found $($matches.Count) (process exit $($process.ExitCode))"
        }

        $result = $matches[0].Groups[1].Value | ConvertFrom-Json
        if ([int64]$result.decoderDelta -ne 0 -or [int64]$result.outputDelta -ne 0) {
            throw "run $run dropped frames: decoder=$($result.decoderDelta), output=$($result.outputDelta)"
        }
        if ($result.videoSync -ne 'display-resample' -or $result.interpolation -ne $true) {
            throw "run $run did not use the approved mpv policy: videoSync=$($result.videoSync), interpolation=$($result.interpolation)"
        }

        Write-Output ("run {0}: decoder={1}->{2} (delta {3}), output={4}->{5} (delta {6}), " +
            "hwdec={7}, avsync={8}->{9}, position={10}->{11}, policy={12}/interpolation={13}" -f
            $run, $result.decoderStart, $result.decoderEnd, $result.decoderDelta,
            $result.outputStart, $result.outputEnd, $result.outputDelta,
            $result.hwdec, $result.avsyncStart, $result.avsyncEnd,
            $result.positionStart, $result.positionEnd,
            $result.videoSync, $result.interpolation)
    }

    Write-Output "artifacts: $artifactDir"
    Write-Output 'MPV ZERO DROP GATE: PASS'
}
finally {
    [Environment]::SetEnvironmentVariable('PATH', $savedPath, 'Process')
    [Environment]::SetEnvironmentVariable('COLOSSEUM_MPV', $savedMpv, 'Process')
    [Environment]::SetEnvironmentVariable('COLOSSEUM_ABBA_CLIP', $savedClip, 'Process')
    [Environment]::SetEnvironmentVariable('COLOSSEUM_MPV_DROP_PROBE', $savedProbe, 'Process')
    [Environment]::SetEnvironmentVariable('QT_FORCE_STDERR_LOGGING', $savedQtStderr, 'Process')
    [Environment]::SetEnvironmentVariable('QTFRAMEWORK_BYPASS_LICENSE_CHECK', $savedQtLicense, 'Process')
}
