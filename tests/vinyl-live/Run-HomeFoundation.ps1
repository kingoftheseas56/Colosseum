param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$EvidenceDir,
    [Parameter(Mandatory)][string]$ProfileTag,
    [ValidateSet('live','playback','restore','offline','inspect')][string]$Phase='live',
    [string]$QtDir='C:\Qt\6.11.1\msvc2022_64',
    [string]$PythonPath='',
    [string[]]$RuntimeBin=@(),
    [switch]$LeaveOpen
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$app=Join-Path $BuildDir 'colosseum.exe'
if(!(Test-Path -LiteralPath $app)){throw 'Build the actual Colosseum app first.'}
if($ProfileTag -notmatch '^VinylQual[A-Za-z0-9_-]+$'){throw 'Use a disposable VinylQual profile tag.'}
$null=New-Item -ItemType Directory -Path $EvidenceDir -Force
$EvidenceDir=(Resolve-Path -LiteralPath $EvidenceDir).Path
$prefix=Join-Path $EvidenceDir $Phase
if(Test-Path -LiteralPath ($prefix+'.receipt.json')){throw 'Preserve prior receipts; choose a new evidence directory.'}
$driver=Join-Path $PSScriptRoot 'HomeFoundationFlow.qml'
$env:PATH=(@($BuildDir,(Join-Path $QtDir 'bin'))+$RuntimeBin+@($env:PATH))-join ';'
$env:QT_PLUGIN_PATH=Join-Path $QtDir 'plugins'
$env:QML_IMPORT_PATH=Join-Path $QtDir 'qml'
$env:QT_FORCE_STDERR_LOGGING='1'
$env:QT_QUICK_CONTROLS_STYLE='Basic'
$env:QSG_RHI_BACKEND='opengl'
$env:COLOSSEUM_APPDATA_TAG=$ProfileTag
if($PythonPath){$env:COLOSSEUM_VINYL_PYTHON=$PythonPath}
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
if($Phase -eq 'offline'){
    $env:HTTP_PROXY='http://127.0.0.1:9';$env:HTTPS_PROXY=$env:HTTP_PROXY
    $env:ALL_PROXY=$env:HTTP_PROXY;$env:NO_PROXY='localhost,127.0.0.1,::1'
}
$values=@($driver,$Phase,$EvidenceDir)
if(@($values|Where-Object {$_ -match '["\r\n]'}).Count){throw 'Unsupported launch argument.'}
$arguments=@($values|ForEach-Object {'"'+$_+'"'})
$sources=@(& git -C $repo ls-files -m -o --exclude-standard | ForEach-Object {
    $path=Join-Path $repo $_
    if(Test-Path -LiteralPath $path -PathType Leaf){[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}}
})
$sources|ConvertTo-Json -Depth 3|Set-Content -LiteralPath ($prefix+'.sources.json') -Encoding utf8
$receipt=[ordered]@{phase=$Phase;profile=$ProfileTag;sourceHead=(& git -C $repo rev-parse HEAD).Trim();
    appSha256=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash;
    driverSha256=(Get-FileHash -LiteralPath $driver -Algorithm SHA256).Hash;
    sourcesSha256=(Get-FileHash -LiteralPath ($prefix+'.sources.json') -Algorithm SHA256).Hash;
    startedUtc=[DateTime]::UtcNow.ToString('o');offlineProxy=($Phase -eq 'offline')}
$p=Start-Process -FilePath $app -ArgumentList $arguments -WorkingDirectory $repo -WindowStyle Normal -PassThru -RedirectStandardOutput ($prefix+'.stdout.log') -RedirectStandardError ($prefix+'.stderr.log')
$receipt.pid=$p.Id;$receipt.processStartUtc=$p.StartTime.ToUniversalTime().ToString('o')
$receipt|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($prefix+'.receipt.json') -Encoding utf8
Write-Output "Started Home $Phase PID=$($p.Id) profile=$ProfileTag"
if($LeaveOpen){exit 0}
$deadline=[DateTime]::UtcNow.AddSeconds(480)
while(!$p.WaitForExit(1000)){
    if([DateTime]::UtcNow -gt $deadline){$p.Kill();throw "Owned Home phase deadline: $Phase"}
}
$p.Refresh();$receipt.exitCode=$p.ExitCode;$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o')
$output=[IO.File]::ReadAllText($prefix+'.stdout.log')+[IO.File]::ReadAllText($prefix+'.stderr.log')
$receipt.passed=$p.ExitCode -eq 0 -and $output.Contains('VINYL_FOUNDATION_PASS') -and !$output.Contains('VINYL_FOUNDATION_FAIL')
$receipt|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($prefix+'.receipt.json') -Encoding utf8
if(!$receipt.passed){throw "Actual Home phase failed: $Phase. See $prefix.stderr.log"}
Write-Output "PASS Home $Phase"
