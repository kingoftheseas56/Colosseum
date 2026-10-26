param(
 [Parameter(Mandatory)][string]$AppPath,
 [Parameter(Mandatory)][string]$QtRoot,
 [Parameter(Mandatory)][string]$EvidenceRoot,
 [string]$MusicFolder='',
 [string[]]$RuntimeBinDirectories=@()
)
$ErrorActionPreference='Stop'
$AppPath=(Resolve-Path -LiteralPath $AppPath).Path
$run='vinyl-m2-'+[Guid]::NewGuid().ToString('N');$directory=Join-Path $EvidenceRoot $run
New-Item -ItemType Directory -Path $directory | Out-Null
$appHash=(Get-FileHash -LiteralPath $AppPath).Hash
$driver=Join-Path $PSScriptRoot 'CatalogueFlow.qml'
$keys=@('PATH','QT_PLUGIN_PATH','QML_IMPORT_PATH','QT_FORCE_STDERR_LOGGING','QT_QUICK_CONTROLS_STYLE','QSG_RHI_BACKEND','QT_QPA_PLATFORM','COLOSSEUM_APPDATA_TAG','COLOSSEUM_VINYL_PYTHON','COLOSSEUM_ENABLE_VINYL','COLOSSEUM_DEV')
$original=@{};foreach($key in $keys){$original[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$results=@()
try{
 $env:PATH=((@((Split-Path $AppPath),(Join-Path $QtRoot 'bin'))+$RuntimeBinDirectories)-join ';')+';'+$env:PATH
 $env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins';$env:QML_IMPORT_PATH=Join-Path $QtRoot 'qml'
 $env:QT_FORCE_STDERR_LOGGING='1';$env:QT_QUICK_CONTROLS_STYLE='Basic';$env:QSG_RHI_BACKEND='opengl';$env:COLOSSEUM_APPDATA_TAG=$run
 Remove-Item Env:COLOSSEUM_ENABLE_VINYL,Env:COLOSSEUM_DEV,Env:QT_QPA_PLATFORM,Env:COLOSSEUM_VINYL_PYTHON -ErrorAction SilentlyContinue
 foreach($phase in @('live','offline')){
  if($phase -eq 'offline'){$env:COLOSSEUM_VINYL_PYTHON=Join-Path $directory 'intentionally-unavailable-python.exe'}
  $stdout=Join-Path $directory "$phase.stdout.log";$stderr=Join-Path $directory "$phase.stderr.log"
  $arguments=@($driver,$phase,$directory,$MusicFolder)|ForEach-Object {'"'+[string]$_+'"'}
  Write-Output "Actual catalogue phase: $phase ($run)"
  $process=Start-Process -FilePath $AppPath -ArgumentList $arguments -WorkingDirectory (Split-Path $AppPath) -WindowStyle Normal -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
  @{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;app=$AppPath}|ConvertTo-Json|Set-Content (Join-Path $directory "$phase.process.json") -Encoding UTF8
  $deadline=[DateTime]::UtcNow.AddSeconds(360)
  while(!$process.WaitForExit(1000)){if([DateTime]::UtcNow -gt $deadline){$process.Kill();throw "Catalogue phase timed out: $directory"}}
  $process.Refresh();$log=[string](Get-Content $stdout -Raw)+[string](Get-Content $stderr -Raw)
  $markers=@('VINYL_CATALOGUE_SEARCH_OK','VINYL_CATALOGUE_ARTIST_OK','VINYL_CATALOGUE_ALBUM_OK','VINYL_CATALOGUE_SAVE_STABLE_OK')
  if($phase -eq 'live'){$markers+=@('VINYL_CATALOGUE_ARTIST_PLAY_REQUEST_OK','VINYL_CATALOGUE_NARROW_OK','VINYL_CATALOGUE_UNKNOWN_OK','VINYL_CATALOGUE_LIVE_OK')}else{$markers+='VINYL_CATALOGUE_OFFLINE_OK'}
  if($process.ExitCode -ne 0 -or $log.Contains('VINYL_CATALOGUE_FAIL')){throw "Catalogue phase failed ($($process.ExitCode)); logs: $directory"}
  foreach($marker in $markers){if(!$log.Contains($marker)){throw "Missing $marker; logs: $directory"}}
  if((Get-FileHash $AppPath).Hash -ne $appHash){throw 'Candidate changed during receiving'}
  $results += [ordered]@{phase=$phase;exitCode=$process.ExitCode;markers=$markers}
 }
 $receipt=[ordered]@{app=$AppPath;sha256=$appHash;profileTag=$run;interaction='actual Main.qml and production world commands';phases=$results}
 $receipt|ConvertTo-Json -Depth 6|Set-Content (Join-Path $directory 'result.json') -Encoding UTF8
 $receipt|ConvertTo-Json -Depth 6
}finally{foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$original[$key],'Process')}}
