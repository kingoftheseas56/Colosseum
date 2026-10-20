param(
 [Parameter(Mandatory)][string]$AppPath,
 [Parameter(Mandatory)][string]$QtRoot,
 [Parameter(Mandatory)][string]$EvidenceRoot,
 [string[]]$RuntimeBinDirectories=@(),
 [switch]$LeaveOpen
)
$ErrorActionPreference='Stop'
$run='vinyl-remote-'+[Guid]::NewGuid().ToString('N');$directory=Join-Path $EvidenceRoot $run
New-Item -ItemType Directory -Path $directory | Out-Null
$AppPath=(Resolve-Path -LiteralPath $AppPath).Path
$hash=(Get-FileHash -LiteralPath $AppPath).Hash
$qmlManifest=Join-Path (Split-Path $AppPath) 'qml-build.manifest'
$qmlHash=(Get-FileHash -LiteralPath $qmlManifest).Hash
$driver=Join-Path $PSScriptRoot 'RemotePlaybackFlow.qml'
$keys=@('PATH','QT_PLUGIN_PATH','QML_IMPORT_PATH','QT_QUICK_CONTROLS_STYLE','QSG_RHI_BACKEND','QT_FORCE_STDERR_LOGGING','QT_QPA_PLATFORM','COLOSSEUM_APPDATA_TAG','COLOSSEUM_VINYL_PYTHON','COLOSSEUM_ENABLE_VINYL','COLOSSEUM_DEV','COLOSSEUM_STREAM_SERVER')
$original=@{};foreach($key in $keys){$original[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$phases=@()
try{
 $env:PATH=(@((Split-Path $AppPath),(Join-Path $QtRoot 'bin'))+$RuntimeBinDirectories -join ';')+';'+$env:PATH
 $env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins';$env:QML_IMPORT_PATH=Join-Path $QtRoot 'qml'
 $env:QT_QUICK_CONTROLS_STYLE='Basic';$env:QSG_RHI_BACKEND='opengl';$env:QT_FORCE_STDERR_LOGGING='1';$env:COLOSSEUM_APPDATA_TAG=$run
 Remove-Item Env:COLOSSEUM_VINYL_PYTHON,Env:COLOSSEUM_ENABLE_VINYL,Env:COLOSSEUM_DEV,Env:QT_QPA_PLATFORM,Env:COLOSSEUM_STREAM_SERVER -ErrorAction SilentlyContinue
 foreach($phase in @('torrent','fallback')+$(if($LeaveOpen){@('review')}else{@()})){
  $stdout=Join-Path $directory "$phase.stdout.log";$stderr=Join-Path $directory "$phase.stderr.log"
  $arguments=@($driver,$phase,$directory)|ForEach-Object {'"'+[string]$_+'"'}
  $process=Start-Process -FilePath $AppPath -ArgumentList $arguments -WorkingDirectory (Split-Path $AppPath) -WindowStyle Normal -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
  @{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;app=$AppPath;sha256=$hash;phase=$phase}|ConvertTo-Json|Set-Content (Join-Path $directory "$phase.process.json") -Encoding UTF8
  Write-Output "Remote playback phase $phase ($run)"
  $deadline=[DateTime]::UtcNow.AddSeconds(340)
  $ownedRuntime=@{}
  while(!$process.WaitForExit(1000)){
   foreach($child in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" | Where-Object {$_.Name -eq 'stremio-runtime.exe'})){
    $ownedRuntime[[string]$child.ProcessId]=@{pid=$child.ProcessId;created=$child.CreationDate.ToUniversalTime().Ticks;executable=$child.ExecutablePath}
   }
   if($phase -eq 'review' -and (Get-Content $stderr -Raw -ErrorAction SilentlyContinue) -match 'VINYL_REMOTE_REVIEW_READY'){break}
   if([DateTime]::UtcNow -gt $deadline){$process.Kill();throw "Playback phase deadline: $directory"}
  }
  $process.Refresh();$log=[string](Get-Content $stdout -Raw)+[string](Get-Content $stderr -Raw)
  if($log.Contains('VINYL_REMOTE_FAIL')){throw "Playback failed: $directory"}
  if($phase -eq 'review'){
   if($process.HasExited -or !$log.Contains('VINYL_REMOTE_REVIEW_READY')){throw 'Review app is not open'}
   $phases+=@{phase=$phase;pid=$process.Id;leftOpen=$true}
  }else{
   if($process.ExitCode -ne 0){throw "Playback exit $($process.ExitCode): $directory"}
   foreach($marker in @('OUTPUT','SEEK_PAUSE_RESUME','EOF_QUEUE','FLOW')){if(!$log.Contains("VINYL_REMOTE_${marker}_OK")){throw "Missing $marker marker: $directory"}}
   foreach($child in $ownedRuntime.Values){
    $remaining=Get-CimInstance Win32_Process -Filter "ProcessId=$($child.pid)"
    if($remaining -and $remaining.CreationDate.ToUniversalTime().Ticks -eq $child.created){throw "Owned stream runtime survived application exit: $($child.pid)"}
   }
   $phases+=@{phase=$phase;exitCode=$process.ExitCode;ownedRuntimes=@($ownedRuntime.Values);ownedRuntimeShutdownVerified=($ownedRuntime.Count -gt 0)}
  }
 }
 if((Get-FileHash -LiteralPath $AppPath).Hash -ne $hash){throw 'App changed during qualification'}
 if((Get-FileHash -LiteralPath $qmlManifest).Hash -ne $qmlHash){throw 'QML manifest changed during qualification'}
 @{app=$AppPath;sha256=$hash;qmlManifestSha256=$qmlHash;driverSha256=(Get-FileHash $driver).Hash;profileTag=$run;phases=$phases}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $directory 'result.json') -Encoding UTF8
 Write-Output (Get-Content (Join-Path $directory 'result.json') -Raw)
}finally{foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$original[$key],'Process')}}
