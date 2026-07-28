param(
    [string]$Exe = '',

    [string]$Clip = '',

    [ValidateRange(1, 86400)]
    [int]$WarmupSeconds = 30,

    [ValidateRange(1, 86400)]
    [int]$MeasureSeconds = 300,

    [ValidateRange(1, 100)]
    [int]$Runs = 2,

    [string]$ValidateResultJson = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Resolve-InputPath([string]$Path, [string]$BasePath) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    return (Resolve-Path -LiteralPath (Join-Path $BasePath $Path)).Path
}

function Get-RequiredValue([object]$Result, [string]$Name) {
    $property = $Result.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "$Name is missing or null"
    }
    return $property.Value
}

function Get-RequiredFiniteNumber([object]$Result, [string]$Name) {
    $value = Get-RequiredValue $Result $Name
    $numericTypeCodes = @(
        [System.TypeCode]::SByte, [System.TypeCode]::Byte,
        [System.TypeCode]::Int16, [System.TypeCode]::UInt16,
        [System.TypeCode]::Int32, [System.TypeCode]::UInt32,
        [System.TypeCode]::Int64, [System.TypeCode]::UInt64,
        [System.TypeCode]::Single, [System.TypeCode]::Double,
        [System.TypeCode]::Decimal
    )
    if ($numericTypeCodes -notcontains [System.Type]::GetTypeCode($value.GetType())) {
        throw "$Name must be a finite number"
    }
    [double]$number = $value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) {
        throw "$Name must be a finite number"
    }
    return $number
}

function Get-RequiredCounter([object]$Result, [string]$Name) {
    [double]$number = Get-RequiredFiniteNumber $Result $Name
    if ($number -lt 0 -or [math]::Truncate($number) -ne $number -or
        $number -gt [long]::MaxValue) {
        throw "$Name must be a non-negative integer counter"
    }
    return [long]$number
}

function Get-RequiredString([object]$Result, [string]$Name) {
    $value = Get-RequiredValue $Result $Name
    if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Name must be a non-empty string"
    }
    return [string]$value
}

function Test-ProbeResult([object]$Result, [int]$RequiredMeasureSeconds) {
    # Validate every value before counter/progress arithmetic; JSON null must never coerce to zero.
    [long]$decoderStart = Get-RequiredCounter $Result 'decoderStart'
    [long]$decoderEnd = Get-RequiredCounter $Result 'decoderEnd'
    [long]$outputStart = Get-RequiredCounter $Result 'outputStart'
    [long]$outputEnd = Get-RequiredCounter $Result 'outputEnd'
    [double]$avsyncStart = Get-RequiredFiniteNumber $Result 'avsyncStart'
    [double]$avsyncEnd = Get-RequiredFiniteNumber $Result 'avsyncEnd'
    [double]$positionStart = Get-RequiredFiniteNumber $Result 'positionStart'
    [double]$positionEnd = Get-RequiredFiniteNumber $Result 'positionEnd'
    [string]$hwdec = Get-RequiredString $Result 'hwdec'
    [string]$videoSync = Get-RequiredString $Result 'videoSync'
    $interpolation = Get-RequiredValue $Result 'interpolation'
    if ($interpolation -isnot [bool]) {
        throw 'interpolation must be a JSON boolean'
    }
    if ($videoSync -ne 'display-resample' -or $interpolation -ne $true) {
        throw "did not use the approved mpv policy: videoSync=$videoSync, interpolation=$interpolation"
    }

    [long]$decoderDelta = $decoderEnd - $decoderStart
    [long]$outputDelta = $outputEnd - $outputStart
    [double]$progress = $positionEnd - $positionStart
    if ($decoderDelta -ne 0 -or $outputDelta -ne 0) {
        throw "dropped frames: decoder=$decoderDelta, output=$outputDelta"
    }
    [double]$requiredProgress = $RequiredMeasureSeconds * 0.9
    if ($progress -lt $requiredProgress) {
        throw "insufficient playback progress: $progress seconds (required at least $requiredProgress)"
    }

    return [pscustomobject]@{
        decoderStart = $decoderStart
        decoderEnd = $decoderEnd
        decoderDelta = $decoderDelta
        outputStart = $outputStart
        outputEnd = $outputEnd
        outputDelta = $outputDelta
        hwdec = $hwdec
        avsyncStart = $avsyncStart
        avsyncEnd = $avsyncEnd
        positionStart = $positionStart
        positionEnd = $positionEnd
        progress = $progress
        videoSync = $videoSync
        interpolation = [bool]$interpolation
    }
}

if (-not [string]::IsNullOrWhiteSpace($ValidateResultJson)) {
    try {
        $validationJson = if (Test-Path -LiteralPath $ValidateResultJson -PathType Leaf) {
            Get-Content -Raw -LiteralPath $ValidateResultJson
        } else {
            $ValidateResultJson
        }
        $validationInput = $validationJson | ConvertFrom-Json
    }
    catch {
        throw "invalid probe JSON: $($_.Exception.Message)"
    }
    [void](Test-ProbeResult $validationInput $MeasureSeconds)
    Write-Output 'MPV ZERO DROP RESULT VALIDATION: PASS'
    return
}

if ([string]::IsNullOrWhiteSpace($Exe) -or [string]::IsNullOrWhiteSpace($Clip)) {
    throw 'Exe and Clip are required unless ValidateResultJson is supplied'
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
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $exePath
        $startInfo.Arguments = 'qml\Main.qml'
        $startInfo.WorkingDirectory = $root
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $false
        $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true

        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            $process.Dispose()
            throw "run $run could not start the Colosseum process"
        }
        $stdoutRead = $process.StandardOutput.ReadToEndAsync()
        $stderrRead = $process.StandardError.ReadToEndAsync()

        if (-not $process.WaitForExit($timeoutMilliseconds)) {
            # Kill only the exact process instance launched above; never target by image name.
            $process.Kill()
            $process.WaitForExit()
            [System.IO.File]::WriteAllText($stdoutLog, $stdoutRead.GetAwaiter().GetResult())
            [System.IO.File]::WriteAllText($stderrLog, $stderrRead.GetAwaiter().GetResult())
            $process.Dispose()
            throw "run $run timed out after $($WarmupSeconds + $MeasureSeconds + 60) seconds"
        }

        # Complete redirected-stream draining, then inspect the exact process we launched before
        # disposing its native handle. Start-Process on Windows PowerShell loses this exit code for
        # GUI-subsystem executables, so the gate owns System.Diagnostics.Process directly.
        $process.WaitForExit()
        $process.Refresh()
        [int]$exitCode = $process.ExitCode
        [System.IO.File]::WriteAllText($stdoutLog, $stdoutRead.GetAwaiter().GetResult())
        [System.IO.File]::WriteAllText($stderrLog, $stderrRead.GetAwaiter().GetResult())
        $process.Dispose()
        if ($exitCode -ne 0) {
            throw "run $run process exited with code $exitCode"
        }
        Write-Output "run $run process exit: 0"

        [string]$text = Get-Content -Raw -LiteralPath $stderrLog
        $matches = [regex]::Matches($text, 'MPV_DROP_PROBE RESULT\s+(\{[^\r\n]+\})')
        if ($matches.Count -ne 1) {
            throw "run $run expected exactly one structured probe result; found $($matches.Count) (process exit $exitCode)"
        }

        try {
            $parsedResult = $matches[0].Groups[1].Value | ConvertFrom-Json
        }
        catch {
            throw "run $run emitted invalid probe JSON: $($_.Exception.Message)"
        }
        $result = Test-ProbeResult $parsedResult $MeasureSeconds

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
