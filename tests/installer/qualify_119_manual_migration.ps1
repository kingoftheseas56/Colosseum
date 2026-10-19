param(
  [Parameter(Mandatory=$true)][string]$PreviousInstaller,
  [Parameter(Mandatory=$true)][string]$Installer
)
$ErrorActionPreference='Stop'
function Require([bool]$ok,[string]$message) { if (!$ok) { throw "MANUAL_MIGRATION_FAIL: $message" } }
function Sha([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
Require ($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_ENVIRONMENT -eq 'github-hosted') 'requires disposable GitHub-hosted Windows runner'
Require ((Sha $PreviousInstaller) -eq '34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e') '1.1.8 public installer digest'
Require ((Sha $Installer) -eq '3aea853978853958dc8e47fec0fa72a4514f4510d13323cdfe99bbff78e7a2a9') '1.1.9 final installer digest'
$installRoot=Join-Path $env:LOCALAPPDATA 'Programs\Colosseum'
$regPath='Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum'
$backup="$installRoot.__update-old"
Require (!(Test-Path $installRoot) -and !(Test-Path $regPath) -and !(Test-Path $backup)) 'no pre-existing Colosseum install or update backup'
$tag="preflight-manual119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_APPDATA_TAG=$tag
$env:COLOSSEUM_LANISTA_PIPE="PreflightManual119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0'
$profile=Join-Path $env:APPDATA "Brotherhood\Colosseum-dltest-$tag"
$appExe=Join-Path $installRoot 'native\build-msvc\colosseum.exe'
$sentinel=Join-Path $profile 'preflight-migration-state.json'
$old=$null
$new=$null
try {
  $install=Start-Process -FilePath $PreviousInstaller -ArgumentList '/S' -Wait -PassThru
  Require ($install.ExitCode -eq 0) "1.1.8 install exit $($install.ExitCode)"
  Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.8') 'genuine old installed version'
  $oldExeSha=Sha $appExe
  $old=Start-Process -FilePath $appExe -WorkingDirectory $installRoot -PassThru -RedirectStandardOutput "$env:RUNNER_TEMP\manual-old.stdout.log" -RedirectStandardError "$env:RUNNER_TEMP\manual-old.stderr.log"
  Start-Sleep -Seconds 20
  $old.Refresh()
  Require (!$old.HasExited) '1.1.8 real app still running after 20s'
  Require (Test-Path $profile) 'actual 1.1.8 app created isolated profile'
  # The manual installer contract presumes the user already closed the old app.
  # We stop only this CI-owned process; this is NOT native natural-exit evidence.
  Stop-Process -Id $old.Id -Force
  $old.WaitForExit(12000) | Out-Null
  '{"library":"synthetic-preservation","progress":37,"preference":"kept"}' | Set-Content -LiteralPath $sentinel -NoNewline
  $sentinelHash=Sha $sentinel
  $preserved=@{}
  Get-ChildItem $profile -Recurse -File | Where-Object { $_.Extension -in @('.ini','.conf') } | ForEach-Object { $preserved[$_.FullName]=Sha $_.FullName }
  $retired=Join-Path $installRoot 'qml\preflight-retired-migration-fixture.qml'
  'import QtQuick; Item {}' | Set-Content -LiteralPath $retired -NoNewline
  Require (Test-Path $retired) 'owned retired-QML fixture placed in old payload'
  $migrate=Start-Process -FilePath $Installer -ArgumentList '/S' -Wait -PassThru
  Require ($migrate.ExitCode -eq 0) "ordinary 1.1.9 installer exit $($migrate.ExitCode)"
  Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.9') 'ordinary install registry migration'
  Require ((Sha $appExe) -eq '0067926231764af1a8005e22ccdee869a8424d84a190aa5aedeae370e403746b') 'installed executable matches signed-payload binary'
  Require ((Sha $appExe) -ne $oldExeSha) 'actual executable was replaced'
  Require (!(Test-Path $retired)) 'obsolete packaged QML tree was replaced'
  $qmlFingerprint=Join-Path $installRoot 'native\build-msvc\qml-build.manifest'
  Require (Test-Path $qmlFingerprint) 'installed QML build fingerprint present'
  Require ((Get-Content $qmlFingerprint -Raw) -match 'qmlTreeSha256=9eb900bf765768b587cdc3a60a13817326829c42074c03807d00b80106c4d1fb') 'installed QML fingerprint from exact source'
  Require ((Sha $sentinel) -eq $sentinelHash) 'persisted profile sentinel unchanged during normal installer migration'
  foreach($path in $preserved.Keys) { Require ((Sha $path) -eq $preserved[$path]) "old settings changed during migration: $path" }
  Require ((& $appExe --version | Out-String).Trim() -eq 'Colosseum 1.1.9' -and $LASTEXITCODE -eq 0) 'new native app version'
  $new=Start-Process -FilePath $appExe -WorkingDirectory $installRoot -PassThru -RedirectStandardOutput "$env:RUNNER_TEMP\manual-new.stdout.log" -RedirectStandardError "$env:RUNNER_TEMP\manual-new.stderr.log"
  Start-Sleep -Seconds 25
  $new.Refresh()
  Require (!$new.HasExited) 'upgraded real app remains running 25s'
  Require ((Sha $sentinel) -eq $sentinelHash) 'profile sentinel preserved through restart'
  Require (!(Test-Path $backup)) 'ordinary manual migration never creates updater backup'
  Write-Host "PREFLIGHT_MANUAL_119_OK from=1.1.8 to=1.1.9 migratedQML=true preservedSettings=$($preserved.Count) installedSha=$(Sha $appExe)"
}
finally {
  foreach($process in @($old,$new)) {
    if($null -ne $process) { try { $process.Refresh(); if(!$process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch {} }
  }
  if(Test-Path (Join-Path $installRoot 'uninstall.exe')) {
    try { $un=Start-Process -FilePath (Join-Path $installRoot 'uninstall.exe') -ArgumentList '/S' -Wait -PassThru; Write-Host "MANUAL_CLEANUP_UNINSTALL_EXIT=$($un.ExitCode)" } catch { Write-Warning $_ }
  }
}