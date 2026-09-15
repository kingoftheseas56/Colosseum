param([string]$BuildRoot = (Join-Path $PSScriptRoot 'mutation-build'))

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../../..')).Path
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$environment = & cmd.exe /d /c ('"' + $vsDevCmd + '" -arch=amd64 -host_arch=amd64 >nul && set')
foreach ($line in $environment) {
    $parts = $line -split '=', 2
    if ($parts.Count -eq 2) { Set-Item -LiteralPath ('Env:' + $parts[0]) -Value $parts[1] }
}
$cmake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctest = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$adapterPath = Join-Path $repo 'native/colosseum_server_v1/src/transport/LibTorrent2Adapter.cpp'
$ledgerPath = Join-Path $repo 'native/colosseum_server_v1/src/transport/ActionLedger.cpp'
$adapter = Get-Content -Raw -LiteralPath $adapterPath
$ledger = Get-Content -Raw -LiteralPath $ledgerPath
$mutations = @(
    @{ Name='request-generation'; Target='adapter';
       Pattern='\s+\|\| request->ownership\.generation == 0\s+\|\| request->ownership\.generation != generation_';
       Replacement=''; Signal='stale or zero request generation was accepted' },
    @{ Name='cancel-generation'; Target='both';
       Pattern='\s+if \(cancel->ownership\.generation == 0\s+\|\| cancel->ownership\.generation != generation_\) return false;';
       Replacement=''; LedgerPattern='\s+&& left\.generation == right\.generation'; LedgerReplacement='';
       Signal='stale or zero cancel generation was accepted' },
    @{ Name='pause-connect-gate'; Target='adapter';
       Pattern='if \(stats_\.paused\) \{\s+deferredConnects_\.push_back\(action\);';
       Replacement="if (false) {`n                deferredConnects_.push_back(action);";
       Signal='paused outbound connect escaped before resume' },
    @{ Name='paused-statistic'; Target='adapter';
       Pattern='\s+stats_\.paused = action\.paused;'; Replacement='';
       Signal='paused statistic did not publish' },
    @{ Name='resume-network-drain'; Target='adapter';
       Pattern='if \(!stats_\.paused\) connects\.swap\(deferredConnects_\);';
       Replacement='if (false) connects.swap(deferredConnects_);';
       Signal='endpoint ownership deadline' }
)

foreach ($mutation in $mutations) {
    $caseRoot = Join-Path $BuildRoot $mutation.Name
    $sourceRoot = Join-Path $caseRoot 'source'
    New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
    $mutatedAdapter = [regex]::Replace($adapter, $mutation.Pattern, $mutation.Replacement, 1)
    if ($mutatedAdapter -eq $adapter) { throw "K10-G mutation did not match: $($mutation.Name)" }
    $mutatedLedger = $ledger
    if ($mutation.Target -eq 'both') {
        $mutatedLedger = [regex]::Replace(
            $ledger, $mutation.LedgerPattern, $mutation.LedgerReplacement, 1)
        if ($mutatedLedger -eq $ledger) { throw "K10-G ledger mutation did not match: $($mutation.Name)" }
    }
    $adapterMutationPath = Join-Path $sourceRoot 'LibTorrent2Adapter.cpp'
    $ledgerMutationPath = Join-Path $sourceRoot 'ActionLedger.cpp'
    Set-Content -LiteralPath $adapterMutationPath -Value $mutatedAdapter -Encoding utf8 -NoNewline
    Set-Content -LiteralPath $ledgerMutationPath -Value $mutatedLedger -Encoding utf8 -NoNewline
    $configureOutput = (& $cmake -S $PSScriptRoot -B $caseRoot -G Ninja -DCMAKE_BUILD_TYPE=Release `
        ('-DK10_G_ADAPTER_SOURCE=' + $adapterMutationPath) `
        ('-DK10_G_LEDGER_SOURCE=' + $ledgerMutationPath) 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "K10-G mutation configure failed: $($mutation.Name)`n$configureOutput"
    }
    $buildOutput = (& $cmake --build $caseRoot --target server1_k10_native_transport_test 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "K10-G mutation build failed: $($mutation.Name)`n$buildOutput"
    }
    $output = (& $ctest --test-dir $caseRoot -R '^K10-G-mutation-behavior$' --output-on-failure 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0) { throw "K10-G mutation escaped: $($mutation.Name)" }
    if ($output -notmatch [regex]::Escape($mutation.Signal)) {
        throw "K10-G mutation failed for the wrong reason: $($mutation.Name)`n$output"
    }
    Write-Output "K10G_MUTATION_REJECTED $($mutation.Name) signal=$($mutation.Signal)"
}
Write-Output "K10-G behavioral mutations PASS count=$($mutations.Count)/$($mutations.Count)"
