param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][string]$PreviousInstaller
)
$ErrorActionPreference = 'Stop'
function Require([bool]$condition, [string]$message) {
    if (!$condition) { throw "RELEASE_UPGRADE_FAIL: $message" }
}
# Run only on a fresh GitHub-hosted runner. Never use the user's install/registry.
Require ($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_ENVIRONMENT -eq 'github-hosted') 'clean hosted runner required'
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\Colosseum'
$regPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum'
Require (!(Test-Path $installRoot) -and !(Test-Path $regPath)) 'existing install or registry metadata'
Require ((Get-FileHash $Installer).Hash.ToLowerInvariant() -eq $ExpectedSha256.ToLowerInvariant()) 'new installer digest'
Require ((Get-FileHash $PreviousInstaller).Hash.ToLowerInvariant() -eq '34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e') 'published 1.1.8 installer digest'
$prior = Start-Process $PreviousInstaller -ArgumentList '/S' -PassThru -Wait
Require ($prior.ExitCode -eq 0) '1.1.8 install exit'
Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.8') 'prior registry version'
$env:COLOSSEUM_APPDATA_TAG = "upgrade119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_LANISTA_PIPE = "upgrade119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_AUTOMATION_FOCUS_SEAMS = '0'
$exe = Join-Path $installRoot 'native\build-msvc\colosseum.exe'
$oldExeSha = (Get-FileHash $exe).Hash
$profile = Join-Path $env:APPDATA "Brotherhood\Colosseum-dltest-$env:COLOSSEUM_APPDATA_TAG"
$old = Start-Process $exe -WorkingDirectory $installRoot -PassThru -RedirectStandardOutput "$env:RUNNER_TEMP\prior.stdout.log" -RedirectStandardError "$env:RUNNER_TEMP\prior.stderr.log"
Start-Sleep -Seconds 20
$old.Refresh(); Require (!$old.HasExited) '1.1.8 startup'
Require (Test-Path $profile) 'prior app created private profile'
# Save durable prior settings and an explicit user-state sentinel outside the payload.
$sentinel = Join-Path $profile 'upgrade-preservation.json'
'{"library":"owned-fixture","progress":37,"preference":"keep"}' | Set-Content $sentinel
$preserved = @{}
Get-ChildItem $profile -Recurse -File | Where-Object { $_.Extension -in @('.ini', '.conf') -or $_.FullName -eq $sentinel } | ForEach-Object { $preserved[$_.FullName] = (Get-FileHash $_.FullName).Hash }
$updateLog = Join-Path $env:RUNNER_TEMP 'real-upgrade.log'
# Exact PID wait: installer must stay alive before the old app is closed.
$upgrade = Start-Process $Installer -ArgumentList @('/UPDATE=1',"/WAITPID=$($old.Id)",'/TARGETVERSION=1.1.9','/RESTART=0',"/LOG=$updateLog") -PassThru
Start-Sleep -Seconds 3
$upgrade.Refresh(); Require (!$upgrade.HasExited) 'updater waits for old process'
Stop-Process -Id $old.Id -Force
Require ($upgrade.WaitForExit(120000)) 'upgrade timeout'
Require ($upgrade.ExitCode -eq 0) "upgrade exit=$($upgrade.ExitCode)"
Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.9') 'updated registry version'
Require ((Get-FileHash $exe).Hash -ne $oldExeSha) 'new binary installed'
Require (Test-Path "$installRoot.__update-old\native\build-msvc\colosseum.exe") 'old backup exists before healthy boot'
foreach ($path in $preserved.Keys) { Require ((Get-FileHash $path).Hash -eq $preserved[$path]) "prior state changed: $path" }
$version = (& $exe --version | Out-String).Trim()
Require ($LASTEXITCODE -eq 0 -and $version -eq 'Colosseum 1.1.9') 'installed version probe'
# Exercise the exact healthy-boot acknowledgement arguments; NSIS relaunch syntax is
# separately covered by update_matrix.ps1. /RESTART=0 lets this test own the process.
$new = Start-Process $exe -WorkingDirectory $installRoot -ArgumentList @('--update-result=success',"--update-backup=$installRoot.__update-old") -PassThru -RedirectStandardOutput "$env:RUNNER_TEMP\upgraded.stdout.log" -RedirectStandardError "$env:RUNNER_TEMP\upgraded.stderr.log"
Start-Sleep -Seconds 25
$new.Refresh(); Require (!$new.HasExited) 'upgraded app startup'
Require (!(Test-Path "$installRoot.__update-old")) 'healthy boot removes exact old backup'
Require ((Get-Content $sentinel -Raw) -match '"progress":37') 'state survives upgraded boot'
Stop-Process -Id $new.Id -Force
Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
$uninstall = Start-Process (Join-Path $installRoot 'uninstall.exe') -ArgumentList '/S' -PassThru -Wait
Require ($uninstall.ExitCode -eq 0) 'uninstall exit'
for ($i=0; $i -lt 30 -and (Test-Path $installRoot); $i++) { Start-Sleep -Seconds 1 }
Require (!(Test-Path $installRoot) -and !(Test-Path $regPath)) 'uninstall payload and registry cleanup'
Require (Test-Path $sentinel) 'uninstall preserves user profile'
Write-Host "RELEASE_UPGRADE_OK from=1.1.8 to=1.1.9 preservedFiles=$($preserved.Count) SHA256=$ExpectedSha256"
