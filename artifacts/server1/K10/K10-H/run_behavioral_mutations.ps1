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
$adapter = Get-Content -Raw -LiteralPath $adapterPath
$mutations = @(
    @{ Name='duplicate-live-block';
       Pattern='const bool duplicate = std::any_of\(activeUploads_\.begin\(\), activeUploads_\.end\(\),[\s\S]*?\n\s+\}\);';
       Replacement='const bool duplicate = false;'; Signal='live duplicate block was admitted twice' },
    @{ Name='per-peer-cap'; Pattern='peerCount >= kMaxOutstandingUploadsPerPeer';
       Replacement='false'; Signal='per-peer admission cap mismatch' },
    @{ Name='global-cap'; Pattern='activeUploads_\.size\(\) >= kMaxOutstandingUploadsGlobal';
       Replacement='false'; Signal='global cap did not reject' },
    @{ Name='advertise-replay'; Pattern='for \(const auto piece : localAdvertisedPieces_\)';
       Replacement='for (const auto piece : std::set<std::uint32_t>{})';
       Signal='on_request/on_cancel interception count mismatch' },
    @{ Name='choke-terminal'; Pattern='\s+cancelUploadsLocked\(choke->peer, 0\);';
       Replacement=''; Signal='choke did not terminalize once' },
    @{ Name='payload-accounting'; Pattern='\s+stats_\.uploadPayloadBytesFramed \+= value\.payload\.size\(\);';
       Replacement=''; Signal='upload feasibility accounting mismatch' }
)

foreach ($mutation in $mutations) {
    $caseRoot = Join-Path $BuildRoot $mutation.Name
    $sourceRoot = Join-Path $caseRoot 'source'
    New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
    $mutated = [regex]::Replace($adapter, $mutation.Pattern, $mutation.Replacement, 1)
    if ($mutated -eq $adapter) { throw "K10-H mutation did not match: $($mutation.Name)" }
    $adapterMutationPath = Join-Path $sourceRoot 'LibTorrent2Adapter.cpp'
    Set-Content -LiteralPath $adapterMutationPath -Value $mutated -Encoding utf8 -NoNewline
    $configureOutput = (& $cmake -S $PSScriptRoot -B $caseRoot -G Ninja -DCMAKE_BUILD_TYPE=Release `
        ('-DK10_H_ADAPTER_SOURCE=' + $adapterMutationPath) 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "K10-H mutation configure failed: $($mutation.Name)`n$configureOutput" }
    $buildOutput = (& $cmake --build $caseRoot --target server1_k10_native_transport_test 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "K10-H mutation build failed: $($mutation.Name)`n$buildOutput" }
    $output = (& $ctest --test-dir $caseRoot -R '^K10-H-mutation-behavior$' --output-on-failure 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0) { throw "K10-H mutation escaped: $($mutation.Name)" }
    if ($output -notmatch [regex]::Escape($mutation.Signal)) {
        throw "K10-H mutation failed for wrong reason: $($mutation.Name)`n$output"
    }
    Write-Output "K10H_MUTATION_REJECTED $($mutation.Name) signal=$($mutation.Signal)"
}
Write-Output "K10-H behavioral mutations PASS count=$($mutations.Count)/$($mutations.Count)"
