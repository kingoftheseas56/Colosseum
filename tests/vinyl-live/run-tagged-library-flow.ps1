param(
    [Parameter(Mandatory)][string]$AppPath,
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$EvidenceRoot,
    [Parameter(Mandatory)][string]$MusicFolder,
    [Parameter(Mandatory)][string]$AlbumTitle,
    [Parameter(Mandatory)][string]$Artist,
    [Parameter(Mandatory)][int]$TrackCount,
    [int]$DiscCount=1,
    [string[]]$RuntimeBinDirectories=@()
)
$ErrorActionPreference='Stop'
$AppPath=(Resolve-Path -LiteralPath $AppPath).Path
$MusicFolder=(Resolve-Path -LiteralPath $MusicFolder).Path
$appHash=(Get-FileHash -LiteralPath $AppPath -Algorithm SHA256).Hash
$run='vinyl-m1-'+[Guid]::NewGuid().ToString('N')
$directory=Join-Path $EvidenceRoot $run
New-Item -ItemType Directory -Path $directory | Out-Null
$driver=Join-Path $PSScriptRoot 'TaggedLibraryFlow.qml'
$keys=@('PATH','QT_PLUGIN_PATH','QML_IMPORT_PATH','QT_FORCE_STDERR_LOGGING','QT_QUICK_CONTROLS_STYLE',
    'QSG_RHI_BACKEND','QT_QPA_PLATFORM','COLOSSEUM_APPDATA_TAG','COLOSSEUM_ENABLE_VINYL','COLOSSEUM_DEV')
$original=@{};foreach($key in $keys){$original[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$results=@()
try {
    $env:PATH=((@((Split-Path $AppPath),(Join-Path $QtRoot 'bin'))+$RuntimeBinDirectories)-join ';')+';'+$env:PATH
    $env:QT_PLUGIN_PATH=Join-Path $QtRoot 'plugins';$env:QML_IMPORT_PATH=Join-Path $QtRoot 'qml'
    $env:QT_FORCE_STDERR_LOGGING='1';$env:QT_QUICK_CONTROLS_STYLE='Basic';$env:QSG_RHI_BACKEND='opengl'
    Remove-Item Env:COLOSSEUM_ENABLE_VINYL,Env:COLOSSEUM_DEV,Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    foreach($phase in @('record','restore','isolated')){
        $env:COLOSSEUM_APPDATA_TAG=if($phase -eq 'isolated'){$run+'-other'}else{$run}
        $stdout=Join-Path $directory "$phase.stdout.log";$stderr=Join-Path $directory "$phase.stderr.log"
        $arguments=@($driver,$phase,$MusicFolder,$AlbumTitle,$Artist,$TrackCount,$DiscCount,$directory)|ForEach-Object {'"'+([string]$_).Replace('"','\"')+'"'}
        Write-Output "Actual tagged-library phase: $phase ($run)"
        # This is an interactive app qualification; mpv's renderer needs the
        # real window exposed. Keep background build/helper processes hidden.
        $process=Start-Process -FilePath $AppPath -ArgumentList $arguments -WorkingDirectory (Split-Path $AppPath) -WindowStyle Normal -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        @{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;app=$AppPath}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $directory "$phase.process.json") -Encoding UTF8
        $deadline=[DateTime]::UtcNow.AddSeconds(240)
        while(!$process.WaitForExit(1000)){if([DateTime]::UtcNow -gt $deadline){$process.Kill();throw "App timeout in $phase; logs $directory"}}
        $process.Refresh();$log=[string](Get-Content $stdout -Raw)+[string](Get-Content $stderr -Raw)
        $markers=switch($phase){record {@('VINYL_TAGGED_IMPORT_OK','VINYL_TAGGED_PARTIAL_REIMPORT_OK','VINYL_TAGGED_ROUTES_OK','VINYL_TAGGED_PLAY_OK')};restore {@('VINYL_TAGGED_RESTORE_OK','VINYL_TAGGED_RESUME_OK')};isolated {@('VINYL_TAGGED_ISOLATION_OK')}}
        if($process.ExitCode -ne 0 -or $log.Contains('VINYL_TAGGED_FAIL')){throw "App failed in $phase (exit $($process.ExitCode)); logs $directory"}
        foreach($marker in $markers){if(!$log.Contains($marker)){throw "Missing $marker; logs $directory"}}
        if($phase -eq 'record'){foreach($capture in @('library.png','album.png')){if(!(Test-Path -LiteralPath (Join-Path $directory $capture))){throw "Missing $capture; logs $directory"}}}
        if((Get-FileHash -LiteralPath $AppPath -Algorithm SHA256).Hash -ne $appHash){throw 'Candidate bytes changed'}
        $results += [ordered]@{phase=$phase;exitCode=$process.ExitCode;markers=$markers}
    }
    $receipt=[ordered]@{appSha256=$appHash;profileTag=$run;interaction='programmatic production world import';albumTitle=$AlbumTitle;artist=$Artist;expectedTracks=$TrackCount;expectedDiscs=$DiscCount;phases=$results}
    $receipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $directory 'result.json') -Encoding UTF8
    $receipt|ConvertTo-Json -Depth 6
} finally {foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$original[$key],'Process')}}
