param(
    [Parameter(Mandatory)][string]$AppPath,
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [string[]]$RuntimeBinDirectories = @()
)
$ErrorActionPreference = 'Stop'
$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$appHash = (Get-FileHash -LiteralPath $AppPath -Algorithm SHA256).Hash
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$driver = Join-Path $PSScriptRoot 'ProductionNativeFlow.qml'
$tag = 'vinyl-m0f-' + [Guid]::NewGuid().ToString('N')
$runDirectory = Join-Path $EvidenceRoot $tag
New-Item -ItemType Directory -Path $runDirectory | Out-Null

# Actual PCM files pass through the production import, Vault and player paths.
function Write-TestAudio([string]$Path, [double]$Frequency) {
    $rate = 24000; $samples = $rate * 20; $bytes = $samples * 2
    $writer = [IO.BinaryWriter]::new([IO.File]::Create($Path))
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([int](36 + $bytes))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([int]16)
        $writer.Write([short]1); $writer.Write([short]1); $writer.Write([int]$rate)
        $writer.Write([int]($rate * 2)); $writer.Write([short]2); $writer.Write([short]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([int]$bytes)
        for ($sample = 0; $sample -lt $samples; $sample++) {
            $writer.Write([short](300 * [Math]::Sin(2 * [Math]::PI * $Frequency * $sample / $rate)))
        }
    } finally { $writer.Dispose() }
}
$audioA = Join-Path $runDirectory 'flow-a.wav'; Write-TestAudio $audioA 440
$audioB = Join-Path $runDirectory 'flow-b.wav'; Write-TestAudio $audioB 660
$keys = @('PATH','QT_PLUGIN_PATH','QML_IMPORT_PATH','QT_FORCE_STDERR_LOGGING',
    'QT_QUICK_CONTROLS_STYLE','QSG_RHI_BACKEND','QT_QPA_PLATFORM',
    'COLOSSEUM_APPDATA_TAG','COLOSSEUM_ENABLE_VINYL','COLOSSEUM_DEV')
$originalEnvironment = @{}
foreach ($key in $keys) { $originalEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$results = @()
try {
    $env:PATH = ((@((Split-Path $AppPath), (Join-Path $QtRoot 'bin')) + $RuntimeBinDirectories) -join ';') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'; $env:QML_IMPORT_PATH = Join-Path $QtRoot 'qml'
    $env:QT_FORCE_STDERR_LOGGING = '1'; $env:QT_QUICK_CONTROLS_STYLE = 'Basic'; $env:QSG_RHI_BACKEND = 'opengl'
    $env:COLOSSEUM_APPDATA_TAG = $tag
    Remove-Item Env:COLOSSEUM_ENABLE_VINYL,Env:COLOSSEUM_DEV,Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    foreach ($phase in @('record','restore','empty')) {
        Write-Output "Actual app phase: $phase ($tag)"
        $stdout = Join-Path $runDirectory "$phase.stdout.log"; $stderr = Join-Path $runDirectory "$phase.stderr.log"
        $arguments = @($driver,$phase,$audioA,$audioB) | ForEach-Object { '"' + $_ + '"' }
        $process = Start-Process -FilePath $AppPath -ArgumentList $arguments -WorkingDirectory (Split-Path $AppPath) `
            -WindowStyle Normal -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        while (!$process.WaitForExit(1000)) {
            if ([DateTime]::UtcNow -gt $deadline) { $process.Kill(); throw "Actual app timed out in $phase; logs: $runDirectory" }
        }
        $process.Refresh()
        $log = [string](Get-Content -LiteralPath $stdout -Raw) + [string](Get-Content -LiteralPath $stderr -Raw)
        $markers = switch ($phase) {
            record { @('VINYL_NATIVE_COLLECTION_OK','VINYL_NATIVE_REPLACE_OK','VINYL_NATIVE_REPEAT_OK','VINYL_NATIVE_RECORD_OK') }
            restore { @('VINYL_NATIVE_RESTORE_OK','VINYL_NATIVE_RESUME_OK','VINYL_NATIVE_PRIVACY_OK','VINYL_NATIVE_CLEAR_OK') }
            empty { @('VINYL_NATIVE_EMPTY_RESTORE_OK') }
        }
        if ($process.ExitCode -ne 0 -or $log.Contains('VINYL_NATIVE_FLOW_FAIL')) { throw "Actual app failed in $phase (exit $($process.ExitCode)); logs: $runDirectory" }
        foreach ($marker in $markers) { if (!$log.Contains($marker)) { throw "Missing $marker; logs: $runDirectory" } }
        if ((Get-FileHash -LiteralPath $AppPath -Algorithm SHA256).Hash -ne $appHash) { throw 'Candidate bytes changed during qualification' }
        $results += [pscustomobject]@{ phase=$phase; exitCode=$process.ExitCode; markers=$markers }
    }
    $receipt = [pscustomobject]@{ app=$AppPath; sha256=$appHash;
        profileTag=$tag; phases=$results; evidence=$runDirectory }
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runDirectory 'result.json') -Encoding UTF8
    $receipt | ConvertTo-Json -Depth 6
} finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $originalEnvironment[$key], 'Process') }
}
