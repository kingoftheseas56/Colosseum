param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$QtDir,
    [Parameter(Mandatory)][string]$EvidenceDir,
    [Parameter(Mandatory)][string]$ProfileTag,
    [Parameter(Mandatory)][string]$Phase,
    [string]$AudioPath='',
    [ValidateSet('home','presentation','learning')][string]$Journey='home',
    [string[]]$RuntimeBin=@(),
    [switch]$Offline,
    [switch]$Unavailable,
    [switch]$LeaveOpen
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$app=Join-Path $BuildDir 'colosseum.exe'
if(!(Test-Path -LiteralPath $app)){throw 'Build the actual Colosseum app first.'}
if($ProfileTag -notmatch '^VinylQual[A-Za-z0-9_-]+$'){throw 'Use a disposable VinylQual-prefixed profile tag.'}
if($Phase -notmatch '^[A-Za-z0-9_-]+$'){throw 'Use a single phase name without path separators.'}
$null=New-Item -ItemType Directory -Path $EvidenceDir -Force
$EvidenceDir=(Resolve-Path -LiteralPath $EvidenceDir).Path
$drivers=@{home='PersonalizedHomeFlow.qml';presentation='PersonalizedHomePresentationFlow.qml';learning='RecommendationLearningFlow.qml'}
$driver=Join-Path $PSScriptRoot $drivers[$Journey]
$env:PATH=(@($BuildDir,(Join-Path $QtDir 'bin'))+$RuntimeBin+@($env:PATH))-join ';'
$env:QT_PLUGIN_PATH=Join-Path $QtDir 'plugins'
$env:QML_IMPORT_PATH=Join-Path $QtDir 'qml'
$env:QT_FORCE_STDERR_LOGGING='1'
$env:QT_QUICK_CONTROLS_STYLE='Basic'
$env:QSG_RHI_BACKEND='opengl'
$env:COLOSSEUM_APPDATA_TAG=$ProfileTag
$env:COLOSSEUM_VINYL_PYTHON=if($Unavailable){Join-Path $EvidenceDir 'unavailable-python.exe'}else{Join-Path $repo 'scripts/vinyl/runtime/python.exe'}
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
if($Offline){
    # Process-local network failure injection. Never disable the machine's adapters.
    $env:HTTP_PROXY='http://127.0.0.1:9';$env:HTTPS_PROXY=$env:HTTP_PROXY
    $env:ALL_PROXY=$env:HTTP_PROXY;$env:NO_PROXY='localhost,127.0.0.1,::1'
}
$prefix=Join-Path $EvidenceDir $Phase
if(Test-Path -LiteralPath ($prefix+'.receipt.json')){throw 'Use a fresh evidence directory/phase; preserve prior receipts.'}
$values=if($Journey -eq 'home'){@($driver,$Phase,$EvidenceDir,$AudioPath)}else{@($driver,$AudioPath,$EvidenceDir,$Phase)}
if(@($values|Where-Object {$_ -match '["\r\n]'}).Count){throw 'Unsupported quote/newline in app arguments.'}
$arguments=@($values|ForEach-Object {'"'+$_+'"'})
$head=(& git -C $repo rev-parse HEAD).Trim()
$receipt=[ordered]@{phase=$Phase;journey=$Journey;profile=$ProfileTag;sourceHead=$head;nativeHomeSha256=(Get-FileHash -LiteralPath (Join-Path $repo 'native/vinyl/VinylRecommendationHome.cpp') -Algorithm SHA256).Hash;driverSha256=(Get-FileHash -LiteralPath $driver -Algorithm SHA256).Hash;appSha256=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash;offlineProxy=[bool]$Offline;unavailableHelper=[bool]$Unavailable;startedUtc=[DateTime]::UtcNow.ToString('o')}
# QML overrides retain the launch directory. Content's shared local assets use
# the source resource root, just as the normal Main startup layout does.
$p=Start-Process -FilePath $app -ArgumentList $arguments -WorkingDirectory $repo -WindowStyle Normal -PassThru -RedirectStandardOutput ($prefix+'.stdout.log') -RedirectStandardError ($prefix+'.stderr.log')
$receipt.pid=$p.Id;$receipt.processStartUtc=$p.StartTime.ToUniversalTime().ToString('o')
$receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($prefix+'.receipt.json') -Encoding utf8
Write-Output "Started $Phase PID=$($p.Id) profile=$ProfileTag"
if($LeaveOpen){exit 0}
$deadline=[DateTime]::UtcNow.AddSeconds(240)
while(!$p.WaitForExit(1000)){
    if([DateTime]::UtcNow -gt $deadline){
        # Only the exact process handle this invocation created.
        $p.Kill();throw "Owned app phase deadline: $Phase"
    }
}
$p.Refresh();$receipt.exitCode=$p.ExitCode;$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o')
$output=[IO.File]::ReadAllText($prefix+'.stdout.log')+[IO.File]::ReadAllText($prefix+'.stderr.log')
$marker=@{home='VINYL_PERSONALIZED';presentation='VINYL_HOME_FINISH';learning='VINYL_LEARNING'}[$Journey]
$receipt.passed=$p.ExitCode -eq 0 -and $output.Contains($marker+'_PASS') -and !$output.Contains($marker+'_FAIL')
$receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($prefix+'.receipt.json') -Encoding utf8
if(!$receipt.passed){throw "Actual app phase failed: $Phase; inspect $prefix.stderr.log"}
Write-Output "PASS $Phase"
