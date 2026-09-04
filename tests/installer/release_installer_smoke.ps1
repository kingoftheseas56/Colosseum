param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][string]$ExpectedVersion
)

$ErrorActionPreference = "Stop"
$installRoot = Join-Path $env:LOCALAPPDATA "Programs\Colosseum"
$regPath = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum"
$desktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "Colosseum.lnk"
$startLink = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Colosseum.lnk"
$stdout = Join-Path $env:RUNNER_TEMP "colosseum-release-smoke.stdout.log"
$stderr = Join-Path $env:RUNNER_TEMP "colosseum-release-smoke.stderr.log"

function Require([bool]$condition, [string]$message) {
    if (!$condition) { throw "RELEASE_INSTALLER_SMOKE_FAIL: $message" }
}

$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$actualSha = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
Require ($actualSha -eq $ExpectedSha256.ToLowerInvariant()) "SHA256 mismatch: $actualSha"
$bytes = (Get-Item -LiteralPath $installerPath).Length
Require ($bytes -le 300MB) "installer exceeds 300 MiB: $bytes"
Require (!(Test-Path -LiteralPath $installRoot)) "runner already has Colosseum installed"
Require (!(Test-Path -LiteralPath $regPath)) "runner already has Colosseum uninstall metadata"

$install = Start-Process -FilePath $installerPath -ArgumentList "/S" -Wait -PassThru
Require ($install.ExitCode -eq 0) "silent install exit=$($install.ExitCode)"
Require (Test-Path -LiteralPath $regPath) "uninstall registry entry missing"
$reg = Get-ItemProperty -LiteralPath $regPath
Require ($reg.DisplayName -eq "Colosseum") "DisplayName=$($reg.DisplayName)"
Require ($reg.DisplayVersion -eq $ExpectedVersion) "DisplayVersion=$($reg.DisplayVersion)"
Require ($reg.InstallLocation -eq $installRoot) "InstallLocation=$($reg.InstallLocation)"

$required = @(
    "uninstall.exe",
    "native\build-msvc\colosseum.exe",
    "native\build-msvc\Qt6Core.dll",
    "native\build-msvc\platforms\qwindows.dll",
    "native\build-msvc\imageformats\qwebp.dll",
    "native\build-msvc\QtWebEngineProcess.exe",
    "native\build-msvc\MpvQt.dll",
    "native\build-msvc\libmpv-2.dll",
    "native\build-msvc\tools\ffmpeg.exe",
    "native\build-msvc\tools\ffprobe.exe"
)
foreach ($relative in $required) {
    Require (Test-Path -LiteralPath (Join-Path $installRoot $relative)) "missing runtime file: $relative"
}

$forbidden = Get-ChildItem -LiteralPath $installRoot -Recurse -File | Where-Object {
    $_.Name -match '^(?i:server\.js|stremio-runtime\.exe)$' -or
    $_.FullName -match '(?i)[\\/]stream_server([\\/]|$)'
}
Require ($forbidden.Count -eq 0) "retired external stream runtime found in installed package: $($forbidden.FullName -join ', ')"

$appExe = Join-Path $installRoot "native\build-msvc\colosseum.exe"
$env:COLOSSEUM_APPDATA_TAG = "github-release-smoke-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_LANISTA_PIPE = "ColosseumReleaseSmoke-$env:GITHUB_RUN_ID"
$app = Start-Process -FilePath $appExe -WorkingDirectory $installRoot -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Start-Sleep -Seconds 12
$app.Refresh()
if ($app.HasExited) {
    Write-Host "--- stdout ---"
    if (Test-Path $stdout) { Get-Content $stdout -Tail 200 }
    Write-Host "--- stderr ---"
    if (Test-Path $stderr) { Get-Content $stderr -Tail 200 }
}
Require (!$app.HasExited) "installed app exited during 12-second boot smoke"
Stop-Process -Id $app.Id -Force
Start-Sleep -Seconds 2
Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -and $_.ExecutablePath.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)
} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

$uninstaller = Join-Path $installRoot "uninstall.exe"
$uninstall = Start-Process -FilePath $uninstaller -ArgumentList "/S" -Wait -PassThru
Require ($uninstall.ExitCode -eq 0) "silent uninstall exit=$($uninstall.ExitCode)"
for ($i = 0; $i -lt 30 -and (Test-Path -LiteralPath $installRoot); $i++) { Start-Sleep -Seconds 1 }
Require (!(Test-Path -LiteralPath $installRoot)) "install directory remained after uninstall"
Require (!(Test-Path -LiteralPath $regPath)) "uninstall registry entry remained"
Require (!(Test-Path -LiteralPath $desktopLink)) "desktop shortcut remained"
Require (!(Test-Path -LiteralPath $startLink)) "start-menu shortcut remained"
Write-Host "RELEASE_INSTALLER_SMOKE_OK version=$ExpectedVersion bytes=$bytes SHA256=$actualSha"
