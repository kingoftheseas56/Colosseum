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
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
$sourcePath = Join-Path $repo 'native/colosseum_server_v1/src/policy/SwarmPolicy.cpp'
$source = Get-Content -Raw -LiteralPath $sourcePath
$mutations = @(
    @{ Name='stale-generation'; Pattern='entry == entries_\.end\(\) \|\| entry->second\.generation != generation';
       Replacement='entry == entries_.end()'; Signal='stale generation must not expose current peer counts' },
    @{ Name='queued-count'; Pattern='case PeerLifecycleState::Queued: \+\+counts\.queued;';
       Replacement='case PeerLifecycleState::Queued: ++counts.ready;'; Signal='queued peer contributes to queued only' },
    @{ Name='handshaking-count'; Pattern='case PeerLifecycleState::Handshaking: \+\+counts\.handshaking;';
       Replacement='case PeerLifecycleState::Handshaking: ++counts.queued;'; Signal='handshaking transition moves exactly one peer' },
    @{ Name='ready-count'; Pattern='case PeerLifecycleState::Ready: \+\+counts\.ready;';
       Replacement='case PeerLifecycleState::Ready: ++counts.queued;'; Signal='each current peer contributes to one lifecycle state' }
)
foreach ($mutation in $mutations) {
    $caseRoot = Join-Path $BuildRoot $mutation.Name
    $sourceRoot = Join-Path $caseRoot 'source'
    New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
    $mutated = [regex]::Replace($source, $mutation.Pattern, $mutation.Replacement, 1)
    if ($mutated -eq $source) { throw "K05-A2 mutation did not match: $($mutation.Name)" }
    $mutationPath = Join-Path $sourceRoot 'SwarmPolicy.cpp'
    Set-Content -LiteralPath $mutationPath -Value $mutated -Encoding utf8 -NoNewline
    & $cmake -S $PSScriptRoot -B $caseRoot -G Ninja `
        ('-DK05_A2_SWARM_SOURCE=' + $mutationPath) | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "K05-A2 mutation configure failed: $($mutation.Name)" }
    & $cmake --build $caseRoot --target server1_k05_a2_test | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "K05-A2 mutation build failed: $($mutation.Name)" }
    $output = (& $ctest --test-dir $caseRoot -R '^K05-A2-mutation-behavior$' --output-on-failure 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0) { throw "K05-A2 mutation escaped: $($mutation.Name)" }
    if ($output -notmatch [regex]::Escape($mutation.Signal)) {
        throw "K05-A2 mutation failed for the wrong reason: $($mutation.Name)`n$output"
    }
    Write-Output "K05A2_MUTATION_REJECTED $($mutation.Name) signal=$($mutation.Signal)"
}
Write-Output "K05-A2 behavioral mutations PASS count=$($mutations.Count)/$($mutations.Count)"
