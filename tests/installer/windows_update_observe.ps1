param(
    [Parameter(Mandatory)][string]$InstallRoot,
    [Parameter(Mandatory)][string]$PhaseFile,
    [Parameter(Mandatory)][string]$StopFile,
    [Parameter(Mandatory)][string]$Timeline,
    [Parameter(Mandatory)][string]$ReadyFile
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Disposable hosted runner required'
}

$names = @('colosseum.exe', 'colosseum-nuvio-host.exe', 'Colosseum-1.5.0-setup.exe', 'Colosseum-1.5.1-setup.exe',
           'QtWebEngineProcess.exe', 'stremio-runtime.exe', 'ffmpeg.exe', 'ffprobe.exe',
           'MsMpEng.exe', 'SearchIndexer.exe')
$end = [DateTimeOffset]::UtcNow.AddMinutes(10)
$previous = ''
$lastWrite = [DateTimeOffset]::MinValue
while (!(Test-Path -LiteralPath $StopFile) -and [DateTimeOffset]::UtcNow -lt $end) {
    $phase = Get-Content -LiteralPath $PhaseFile -Raw
    $observed = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in $names -or
        ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($InstallRoot, [StringComparison]::OrdinalIgnoreCase))
    } | Sort-Object ProcessId | ForEach-Object {
        [ordered]@{
            pid = $_.ProcessId
            parent_pid = $_.ParentProcessId
            name = $_.Name
            image = $_.ExecutablePath
            command_line = $_.CommandLine
            creation = $_.CreationDate
        }
    })
    $state = [ordered]@{ phase = $phase; observer_cwd = (Get-Location).Path; processes = $observed }
    $compact = $state | ConvertTo-Json -Compress -Depth 5
    $now = [DateTimeOffset]::UtcNow
    if ($previous -ne $compact -or ($now - $lastWrite).TotalSeconds -ge 2) {
        $state['utc'] = $now.ToString('o')
        Add-Content -LiteralPath $Timeline -Value ($state | ConvertTo-Json -Compress -Depth 5) -Encoding utf8
        $previous = $compact
        $lastWrite = $now
    }
    if (!(Test-Path -LiteralPath $ReadyFile)) { New-Item -ItemType File -Path $ReadyFile | Out-Null }
    Start-Sleep -Milliseconds 430
}
