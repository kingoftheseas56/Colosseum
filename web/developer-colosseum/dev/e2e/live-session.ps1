param(
    [Parameter(Mandatory = $true)][string]$SeedDir,
    [Parameter(Mandatory = $true)][string]$Route,
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Tag = ("theatre-title-v1-" + [guid]::NewGuid().ToString('N')),
    [string]$Pipe = ("ColosseumLanistaTheatreTitle-" + [guid]::NewGuid().ToString('N')),
    [string]$Exe,
    [string]$QtBin,
    [string]$MpvBin,
    [string]$LibMpvBin,
    [string]$ProbeFile,
    [string]$RecordDir
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $out -Force | Out-Null
if (-not $Exe) { $Exe = Join-Path $repo 'native\build-msvc\colosseum.exe' }
$lanista = Join-Path (Split-Path $Exe -Parent) 'lanista.exe'
if (-not (Test-Path -LiteralPath $Exe)) { throw "Missing app: $Exe" }
if (-not (Test-Path -LiteralPath $lanista)) { throw "Missing Lanista CLI: $lanista" }
if (-not (Test-Path -LiteralPath $SeedDir -PathType Container)) { throw "Missing seed: $SeedDir" }
if ([string]::IsNullOrWhiteSpace($Tag) -or $Tag -notmatch '^[A-Za-z0-9-]+$') { throw 'A nonempty alphanumeric tag is required' }
if ([string]::IsNullOrWhiteSpace($Pipe) -or $Pipe -eq 'ColosseumLanista') { throw 'Refusing the default or empty Lanista pipe' }
$appDataRoot = Join-Path $env:APPDATA ("Brotherhood\Colosseum-dltest-$Tag")
if (Test-Path -LiteralPath $appDataRoot) { throw "Tagged root already exists: $appDataRoot" }

# The seed is private and lives outside Git. Copy only into the unique tagged root.
New-Item -ItemType Directory -Path $appDataRoot | Out-Null
Copy-Item -Path (Join-Path $SeedDir '*') -Destination $appDataRoot -Recurse -Force
$tcp = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$tcp.Start()
$port = $tcp.LocalEndpoint.Port
$tcp.Stop()
$oldPath = $env:PATH
$oldTag = $env:COLOSSEUM_APPDATA_TAG
$oldPipe = $env:COLOSSEUM_LANISTA_PIPE
$oldDrive = $env:COLOSSEUM_LANISTA_DRIVE
$oldWeb = $env:COLOSSEUM_WEBUI
$oldDebug = $env:QTWEBENGINE_REMOTE_DEBUGGING
$oldDev = $env:COLOSSEUM_DEV
$oldRecord = $env:COLOSSEUM_WEBUI_RECORD
$app = $null
try {
    $runtime = @($QtBin, $MpvBin, $LibMpvBin) | Where-Object { $_ }
    if ($runtime.Count) { $env:PATH = ($runtime -join ';') + ';' + $env:PATH }
    $env:COLOSSEUM_APPDATA_TAG = $Tag
    $env:COLOSSEUM_LANISTA_PIPE = $Pipe
    $env:COLOSSEUM_LANISTA_DRIVE = '1'
    $env:COLOSSEUM_WEBUI = '1'
    $env:COLOSSEUM_DEV = '1'
    if ($RecordDir) {
        New-Item -ItemType Directory -Path $RecordDir -Force | Out-Null
        $env:COLOSSEUM_WEBUI_RECORD = (Resolve-Path -LiteralPath $RecordDir).Path
    }
    $env:QTWEBENGINE_REMOTE_DEBUGGING = [string]$port
    $app = Start-Process -FilePath $Exe -PassThru -WorkingDirectory $repo -RedirectStandardOutput (Join-Path $out "$Label-stdout.log") -RedirectStandardError (Join-Path $out "$Label-stderr.log")
    @{
        pid = $app.Id; pipe = $Pipe; tag = $Tag; port = $port
        appDataRoot = $appDataRoot; seed = (Resolve-Path $SeedDir).Path
        exe = (Resolve-Path $Exe).Path; route = ($Route | ConvertFrom-Json)
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out "$Label-session.json")
    $attachArgs = @((Join-Path $PSScriptRoot 'attach.mjs'), $port, $Route, $Label)
    if ($ProbeFile) { $attachArgs += (Resolve-Path -LiteralPath $ProbeFile).Path }
    & node @attachArgs *>&1 | Tee-Object -FilePath (Join-Path $out "$Label-attach.log")
    $attachExit = $LASTEXITCODE
    & $lanista get-state --pipe $Pipe | Set-Content -LiteralPath (Join-Path $out "$Label-state.json")
    if ($LASTEXITCODE -ne 0) { throw "get-state failed: $LASTEXITCODE" }
    $state = Get-Content -LiteralPath (Join-Path $out "$Label-state.json") -Raw | ConvertFrom-Json
    if ($state.appDataRoot -notlike "*$Tag*") { throw 'Lanista did not confirm tagged AppData' }
    if ($attachExit -ne 0) { throw "attach failed: $attachExit" }
} finally {
    if ($app -and -not $app.HasExited) {
        [void]$app.CloseMainWindow()
        if (-not $app.WaitForExit(10000)) {
            Write-Warning "App $($app.Id) stayed in its tray after window close; terminating only this isolated process"
            $app.Kill()
            [void]$app.WaitForExit(5000)
        }
    }
    $env:PATH = $oldPath
    $env:COLOSSEUM_APPDATA_TAG = $oldTag
    $env:COLOSSEUM_LANISTA_PIPE = $oldPipe
    $env:COLOSSEUM_LANISTA_DRIVE = $oldDrive
    $env:COLOSSEUM_WEBUI = $oldWeb
    $env:QTWEBENGINE_REMOTE_DEBUGGING = $oldDebug
    $env:COLOSSEUM_DEV = $oldDev
    $env:COLOSSEUM_WEBUI_RECORD = $oldRecord
}
