param(
  [Parameter(Mandatory=$true)][string]$PreviousInstaller,
  [Parameter(Mandatory=$true)][string]$Installer
)
$ErrorActionPreference='Stop'
function Require([bool]$ok,[string]$message) { if (!$ok) { throw "REAL_RESTART_COMPONENT_FAIL: $message" } }
function Sha([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
Require ($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_ENVIRONMENT -eq 'github-hosted') 'requires disposable GitHub-hosted Windows runner'
Require ((Sha $PreviousInstaller) -eq '34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e') 'published 1.1.8 installer digest'
Require ((Sha $Installer) -eq '3aea853978853958dc8e47fec0fa72a4514f4510d13323cdfe99bbff78e7a2a9') 'final 1.1.9 installer digest'
$installRoot=Join-Path $env:LOCALAPPDATA 'Programs\Colosseum'
$regPath='Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum'
$backup="$installRoot.__update-old"
Require (!(Test-Path $installRoot) -and !(Test-Path $regPath) -and !(Test-Path $backup)) 'clean hosted installation required'
$env:COLOSSEUM_APPDATA_TAG="preflight-restart119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_LANISTA_PIPE="PreflightRestart119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0'
$appExe=Join-Path $installRoot 'native\build-msvc\colosseum.exe'
$log=Join-Path $env:RUNNER_TEMP 'preflight-119-nsis-real-restart.log'
$waiter=$null
$update=$null
try {
  $prior=Start-Process -FilePath $PreviousInstaller -ArgumentList '/S' -Wait -PassThru
  Require ($prior.ExitCode -eq 0) "1.1.8 install exit $($prior.ExitCode)"
  Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.8') 'real prior version registered'
  # A short-lived helper expires NATURALLY. It proves the *production NSIS wait*
  # but is deliberately NOT described as old Colosseum natural shutdown.
  $waiter=Start-Process -FilePath $env:ComSpec -ArgumentList '/c ping 127.0.0.1 -n 16 > nul' -PassThru
  Require (!$waiter.HasExited) 'wait helper started and alive'
  $update=Start-Process -FilePath $Installer -ArgumentList @('/UPDATE=1',"/WAITPID=$($waiter.Id)",'/TARGETVERSION=1.1.9','/RESTART=1',"/LOG=$log") -PassThru
  Start-Sleep -Seconds 2
  $update.Refresh()
  Require (!$update.HasExited) 'production installer alive before wait PID exits'
  Require ($waiter.WaitForExit(45000)) 'naturally ending test helper exceeded wait'
  Require ($waiter.ExitCode -eq 0) 'naturally ending helper exit'
  Require ($update.WaitForExit(120000)) 'NSIS update process timeout'
  Require ($update.ExitCode -eq 0) "NSIS production update exit $($update.ExitCode)"
  Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.9') 'updated installation registry version'
  Require ((Sha $appExe) -eq '0067926231764af1a8005e22ccdee869a8424d84a190aa5aedeae370e403746b') 'updated binary is exact signed-payload binary'
  Require (Test-Path $log) 'actual production NSIS update log exists'
  $logText=Get-Content -LiteralPath $log -Raw
  Require ($logText.Contains('SWAP success old=')) 'real NSIS swap was logged'
  Require ($logText.Contains('RELAUNCH --update-result=success')) 'real NSIS relaunch branch was executed'
  # NSIS compiled without COLOSSEUM_UPDATE_TEST_MODE really Execs the updated app;
  # source 7792bd1d acks healthy boot after the first QQuickWindow frame.
  $realApp=$null
  $cleaned=$false
  for($i=0;$i -lt 90;$i++) {
    $processes=@(Get-CimInstance Win32_Process -Filter "Name = 'colosseum.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.Equals($appExe,[StringComparison]::OrdinalIgnoreCase) })
    if($processes.Count -gt 0) { $realApp=$processes[0] }
    if($null -ne $realApp -and !(Test-Path $backup)) { $cleaned=$true; break }
    Start-Sleep -Seconds 1
  }
  Require ($null -ne $realApp) 'new installed application actually spawned from NSIS'
  Require ($cleaned) 'first-frame healthy acknowledgement removed exact backup'
  Require (!(Test-Path "$installRoot.__update-new")) 'NSIS staging sibling removed'
  Require (!(Test-Path "$log.launch")) 'this is NOT synthetic NSIS test mode'
  Start-Sleep -Seconds 3
  $stillRunning=Get-CimInstance Win32_Process -Filter "ProcessId = $($realApp.ProcessId)"
  Require ($null -ne $stillRunning) 'NSIS-relaunched app still running after healthy ack'
  Write-Host "PREFLIGHT_REAL_NSIS_RESTART_OK version=1.1.9 restartedPid=$($realApp.ProcessId) installerSha=$(Sha $Installer) naturalWaitHelper=true oldColosseumNaturalExit=NOT_TESTED nativeDiscovery=NOT_TESTED"
}
finally {
  if($null -ne $waiter) { try { $waiter.Refresh(); if(!$waiter.HasExited) { Stop-Process -Id $waiter.Id -Force -ErrorAction SilentlyContinue } } catch {} }
  if($null -ne $update) { try { $update.Refresh(); if(!$update.HasExited) { Stop-Process -Id $update.Id -Force -ErrorAction SilentlyContinue } } catch {} }
  @(Get-CimInstance Win32_Process -Filter "Name = 'colosseum.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.Equals($appExe,[StringComparison]::OrdinalIgnoreCase) }) | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  if(Test-Path (Join-Path $installRoot 'uninstall.exe')) {
    try { $un=Start-Process -FilePath (Join-Path $installRoot 'uninstall.exe') -ArgumentList '/S' -Wait -PassThru; Write-Host "RESTART_CLEANUP_UNINSTALL_EXIT=$($un.ExitCode)" } catch { Write-Warning $_ }
  }
}