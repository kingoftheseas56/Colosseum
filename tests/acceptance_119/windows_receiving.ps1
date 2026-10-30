#requires -Version 5.1
<#
Bounded receiving-side launch/lifecycle probe. This script never installs,
uninstalls, updates a feed, obtains a private key, or drives the daily app.
Use the companion checklist for the actual manual 1.1.8 -> 1.1.9 installation.
Source-contract tests on Linux are not Windows runtime qualification.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Baseline118', 'After119', 'Restart119')][string]$Phase,
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$Lanista,
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$FixtureRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$SessionFile,
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedAppSha256,
    [string]$AppRelativePath = 'native\build-msvc\colosseum.exe',
    [string]$InstallerPath,
    [ValidatePattern('^[a-z][a-z0-9-]*$')][string]$SeedName,
    [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedInstallerSha256 = 'a4f4fe1279335769d96b5e30f9917a21fcddd8fc87ce0c9d6f2538fd6aaa57e6',
    [long]$ExpectedInstallerSize = 328380744,
    [string]$SourceSha = 'a6104f7a8d9aab9766e71f031b7908beaeca85ad',
    [string]$PackagedSourceSha = '70e0773b56fa4a35adc1c0e14a8520e4b680c27b',
    [string]$CompiledSourceSha = '47e90b3f244d9ad76b7f5bbc7607629fe358288a',
    [string]$CandidateRuntime = 'Qt 6.11.1 / MSVC 2022',
    [ValidateRange(10, 180)][int]$ReadySeconds = 90,
    [ValidateRange(10, 1800)][int]$ScenarioSeconds = 300,
    [ValidateRange(3, 60)][int]$ShutdownSeconds = 15,
    [switch]$ConfirmDisposableWindowsAccount
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'BLOCKED: this runner requires Windows.' }
if (!$ConfirmDisposableWindowsAccount) {
    throw 'BLOCKED: use a disposable Windows account/VM with no personal Colosseum state and acknowledge -ConfirmDisposableWindowsAccount.'
}
if ($SourceSha -ne 'a6104f7a8d9aab9766e71f031b7908beaeca85ad') { throw 'Approved source pin mismatch.' }
$runId = [Guid]::NewGuid().ToString('N')
$runDir = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) "$Phase-$runId"
New-Item -ItemType Directory -Path $runDir | Out-Null
$script:commands = New-Object System.Collections.Generic.List[object]
$script:owned = @{}
$script:sequence = 0
$script:app = $null
$script:appHandle = $null
$script:bridgeValidated = $false
$forced = New-Object System.Collections.Generic.List[object]
$report = [ordered]@{
    schema = 'colosseum.acceptance119.windows.v1'; phase = $Phase; status = 'FAIL'
    started_at = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString
    powershell = $PSVersionTable.PSVersion.ToString(); runtime = $CandidateRuntime
    source_sha = $SourceSha
    packaged_source_sha = $(if ($Phase -eq 'Baseline118') { $null } else { $PackagedSourceSha })
    compiled_source_sha = $(if ($Phase -eq 'Baseline118') { $null } else { $CompiledSourceSha })
    source_identity_limit = 'Source values are provenance supplied by the receiver; binary version alone does not prove a commit.'
    package_root = $PackageRoot; fixture_root = $FixtureRoot; commands = $script:commands
    assertions = (New-Object System.Collections.Generic.List[object]); evidence = $runDir
    app_sha256 = $null; installer_sha256 = $null; installer_size = $null
    forced_recovery = $forced; process_coverage_limit = 'Only observed descendants of the launched PID are tracked. Short-lived processes may escape sampling.'
    cases = @(
        @{case_id='WIN-MANUAL-MIGRATION'; status='NOT RUN'; reason='Requires completed manual receiving checklist and before/after UI evidence.'},
        @{case_id='WIN-AUTO-HANDOFF'; status='BLOCKED'; reason='No authentic higher-version signed fixture and supported private staging route supplied.'}
    )
}
$previousEnvironment = @{}
foreach ($variable in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'COLOSSEUM_*' })) {
    $previousEnvironment[$variable.Name] = $variable.Value
}

function Require([bool]$Condition, [string]$Message) {
    $report.assertions.Add(@{check=$Message; pass=$Condition})
    if (!$Condition) { throw $Message }
}
function Save-Json([object]$Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function File-Sha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Prepare-Scenario([object]$ScenarioObject) {
    $steps = New-Object System.Collections.Generic.List[object]
    foreach ($step in @($ScenarioObject.steps)) { $steps.Add($step) }
    function Is-Quit([object]$Step) {
        return ($Step.PSObject.Properties.Name -contains 'cmd' -and $Step.cmd -eq 'ui-keypress' -and
            $Step.PSObject.Properties.Name -contains 'payload' -and $Step.payload.key -eq 'Ctrl+Q')
    }
    # Remove only the quit's optional trailing drain, not an unrelated pause.
    if ($steps.Count -gt 1 -and $steps[$steps.Count - 1].PSObject.Properties.Name -contains 'presentation_wait_ms' -and
        $steps[$steps.Count - 1].presentation_wait_ms -eq 500 -and (Is-Quit $steps[$steps.Count - 2])) {
        $steps.RemoveAt($steps.Count - 1)
    }
    for ($index = 0; $index -lt $steps.Count; $index++) {
        if ((Is-Quit $steps[$index]) -and $index -ne $steps.Count - 1) {
            throw 'Ctrl+Q is permitted only as the final command'
        }
    }
    if ($steps.Count -gt 0 -and (Is-Quit $steps[$steps.Count - 1])) { $steps.RemoveAt($steps.Count - 1) }
    if ($steps.Count -eq 0) { throw 'Scenario has no journey after removing trailing quit' }
    $ScenarioObject.steps = $steps.ToArray()
    return $ScenarioObject
}
function Classify-JourneyFailure([string]$ReceivingPhase, [int]$ExitCode, [bool]$TimedOut, [string]$Stdout) {
    $classification = @{status='FAIL'; failed_selector=$null; protocol_reply=$null;
        reason='Lanista journey failed; no supported baseline-prerequisite classification.'}
    if ($ReceivingPhase -ne 'Baseline118' -or $ExitCode -ne 1 -or $TimedOut) { return $classification }
    $errors = New-Object System.Collections.Generic.List[object]
    foreach ($line in ($Stdout -split "`r?`n")) {
        if (!$line.StartsWith('  reply ')) { continue }
        $separator = $line.IndexOf(': {')
        if ($separator -lt 0) { return $classification }
        try { $reply = $line.Substring($separator + 2) | ConvertFrom-Json }
        catch { return $classification }
        if ($reply.PSObject.Properties.Name -contains 'type' -and $reply.type -eq 'error') { $errors.Add($reply) }
    }
    # This is a known stock-1.1.8 observability gap, not a blanket excuse for
    # nonzero exit codes or missing UI after a failed navigation.
    if ($errors.Count -eq 1 -and $errors[0].code -eq 'NO_SUCH_ITEM' -and
        $errors[0].message -eq 'readerCommand_layout') {
        $classification.status = 'BLOCKED'
        $classification.failed_selector = [string]$errors[0].message
        $classification.protocol_reply = $errors[0]
        $classification.reason = 'Stock 1.1.8 lacks readerCommand_layout; a compatible baseline/manual preparation evidence path is required.'
    }
    return $classification
}
function Get-ProcessRecord([object]$Process) {
    return @{pid=[int]$Process.ProcessId; parent_pid=[int]$Process.ParentProcessId;
        creation_ticks=$Process.CreationDate.ToUniversalTime().Ticks; name=$Process.Name; path=$Process.ExecutablePath}
}
function Test-OwnedProcess([object]$Record) {
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Record.pid)" -ErrorAction SilentlyContinue
    return ($null -ne $current -and $current.CreationDate.ToUniversalTime().Ticks -eq $Record.creation_ticks)
}
function Recover-OwnedApp([Diagnostics.Process]$OwnedProcess, [int]$GraceMilliseconds = 15000) {
    # The caller retains the exact Process and SafeHandle returned by its own
    # Start-Process. Never rediscover an app by PID/name when CIM has failed.
    $outcome = @{pid=$null; forced=$false; close_sent=$false; exit_code=$null;
        surviving_pid=$null; error=$null; ownership='retained exact Process/SafeHandle'}
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $outcome.pid = $OwnedProcess.Id
        $handle = $OwnedProcess.SafeHandle
        if ($handle.IsInvalid -or $handle.IsClosed) { throw 'Retained process handle is unavailable' }
        $OwnedProcess.Refresh()
        if (!$OwnedProcess.HasExited) {
            try { $outcome.close_sent = $OwnedProcess.CloseMainWindow() }
            catch { $outcome.close_error = $_.Exception.Message }
            $OwnedProcess.WaitForExit($GraceMilliseconds) | Out-Null
            $OwnedProcess.Refresh()
            if (!$OwnedProcess.HasExited) {
                $outcome.forced = $true
                $OwnedProcess.Kill()
                $OwnedProcess.WaitForExit(5000) | Out-Null
                $OwnedProcess.Refresh()
            }
        }
        if ($OwnedProcess.HasExited) { $outcome.exit_code = $OwnedProcess.ExitCode }
        else { $outcome.surviving_pid = $OwnedProcess.Id }
    } catch {
        $outcome.error = $_.Exception.Message
        # Failure to inspect is not evidence that cleanup succeeded.
        $outcome.surviving_pid = $outcome.pid
    }
    $outcome.duration_ms = $clock.ElapsedMilliseconds
    return $outcome
}
function Observe-Children {
    if ($script:owned.Count -eq 0) { return }
    $current = @(Get-CimInstance Win32_Process)
    $live = @{}
    foreach ($item in $current) { $live[[int]$item.ProcessId] = $item }
    do {
        $added = $false
        foreach ($item in $current) {
            $parentId = [int]$item.ParentProcessId
            if ($script:owned.ContainsKey([int]$item.ProcessId) -or !$script:owned.ContainsKey($parentId)) { continue }
            $parent = $script:owned[$parentId]
            if (!$live.ContainsKey($parentId) -or $live[$parentId].CreationDate.ToUniversalTime().Ticks -ne $parent.creation_ticks) { continue }
            if ($item.CreationDate.ToUniversalTime().Ticks -lt $parent.creation_ticks) { continue }
            $script:owned[[int]$item.ProcessId] = Get-ProcessRecord $item
            $added = $true
        }
    } while ($added)
}
function Invoke-Recorded([string]$File, [string[]]$Arguments, [int]$TimeoutSeconds = 15) {
    $script:sequence++
    $prefix = Join-Path $runDir ('command-{0:D3}' -f $script:sequence)
    # These are direct executable arguments, never a shell command. Windows file
    # arguments cannot contain a quote; reject control characters before launch.
    foreach ($argument in $Arguments) {
        if ($argument -match '["\r\n]') { throw 'Unsupported quote/control character in argument.' }
    }
    $quoted = @($Arguments | ForEach-Object { '"' + ($_ -replace '(\\+)$', '$1$1') + '"' })
    $started = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $File -ArgumentList $quoted -PassThru -NoNewWindow `
        -RedirectStandardOutput "$prefix.stdout.log" -RedirectStandardError "$prefix.stderr.log"
    $entry = @{file=$File; args=$Arguments; pid=$process.Id; exit_code=$null; timed_out=$false;
        stdout="$prefix.stdout.log"; stderr="$prefix.stderr.log"}
    $script:commands.Add($entry)
    while (!$process.HasExited -and $started.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        Observe-Children
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if (!$process.HasExited) {
        $entry.timed_out = $true
        $process.Kill() # The exact command Process object, never a name/path sweep.
        $process.WaitForExit(5000) | Out-Null
    } else { $process.WaitForExit() }
    $entry.exit_code = $process.ExitCode
    $entry.duration_ms = $started.ElapsedMilliseconds
    $output = Get-Content -LiteralPath "$prefix.stdout.log" -Raw -ErrorAction SilentlyContinue
    return @{exit_code=$entry.exit_code; stdout=$output; timed_out=$entry.timed_out}
}
function Bridge([string]$Command, [string[]]$Extra = @()) {
    $result = Invoke-Recorded $script:lanistaPath (@('--pipe', $script:pipe, '--timeout', '4000', $Command) + $Extra) 12
    if ($result.timed_out -or $result.exit_code -ne 0) { return $null }
    try { return ($result.stdout | ConvertFrom-Json) } catch { return $null }
}
function Assert-IsolatedRoot([string]$Path, [string]$Tag) {
    $segments = [IO.Path]::GetFullPath($Path).Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)
    Require ($segments -contains "Colosseum-dltest-$Tag") "Resolved storage root has the exact disposable tag: $Path"
}

try {
    $package = (Resolve-Path -LiteralPath $PackageRoot).Path.TrimEnd('\', '/')
    $exe = [IO.Path]::GetFullPath((Join-Path $package $AppRelativePath))
    Require ($exe.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) 'App executable stays inside the explicit package root'
    Require (Test-Path -LiteralPath $exe -PathType Leaf) 'App executable exists'
    $script:lanistaPath = (Resolve-Path -LiteralPath $Lanista).Path
    $scenarioPath = (Resolve-Path -LiteralPath $Scenario).Path
    $fixturePath = (Resolve-Path -LiteralPath $FixtureRoot).Path
    $fixtureManifestPath = Join-Path $fixturePath 'fixture-manifest.json'
    $fixtureManifest = Get-Content -LiteralPath $fixtureManifestPath -Raw | ConvertFrom-Json
    Require ($fixtureManifest.schema -eq 'colosseum.acceptance119.fixtures.v1' -and $fixtureManifest.source_sha -eq $SourceSha) 'Fixture ownership and source pin match'
    $report.fixture_manifest_sha256 = File-Sha $fixtureManifestPath
    $scenarioText = Get-Content -LiteralPath $scenarioPath -Raw
    $report.source_scenario_sha256 = File-Sha $scenarioPath
    foreach ($property in $fixtureManifest.tokens.PSObject.Properties) {
        $encoded = ConvertTo-Json -InputObject ([string]$property.Value) -Compress
        $scenarioText = $scenarioText.Replace(('${' + $property.Name + '}'), $encoded.Substring(1, $encoded.Length - 2))
    }
    Require ($scenarioText -notmatch '\$\{[A-Z][A-Z0-9_]*\}') 'Unbound scenario fixture token'
    $scenarioJson = Prepare-Scenario ($scenarioText | ConvertFrom-Json)
    $scenarioPath = Join-Path $runDir 'scenario.json'
    [IO.File]::WriteAllText($scenarioPath, ($scenarioJson | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
    Require (@($scenarioJson.steps).Count -gt 0) 'Scenario contains real Lanista steps'
    Require (@($scenarioJson.steps | Where-Object { $_.PSObject.Properties.Name -contains 'expect' }).Count -gt 0) 'Scenario includes observable assertions'
    $report.package_root = $package; $report.fixture_root = $fixturePath
    $report.app_sha256 = File-Sha $exe
    Require ($report.app_sha256 -eq $ExpectedAppSha256.ToLowerInvariant()) 'Executable SHA256 matches the receiver-approved candidate'
    $report.lanista_sha256 = File-Sha $script:lanistaPath
    $report.scenario_sha256 = File-Sha $scenarioPath
    if ($Phase -ne 'Baseline118') { Require (![string]::IsNullOrEmpty($InstallerPath)) 'Candidate installer path is required for 1.1.9 receiving phases' }
    if ($InstallerPath) {
        $installer = (Resolve-Path -LiteralPath $InstallerPath).Path
        $report.installer_sha256 = File-Sha $installer
        $report.installer_size = (Get-Item -LiteralPath $installer).Length
        Require ($report.installer_sha256 -eq $ExpectedInstallerSha256.ToLowerInvariant()) 'Installer SHA256 matches expected artifact'
        Require ($report.installer_size -eq $ExpectedInstallerSize) 'Installer size matches expected artifact'
    }
    $expectedVersion = if ($Phase -eq 'Baseline118') { '1.1.8' } else { '1.1.9' }
    $probe = Invoke-Recorded $exe @('--version')
    Require (!$probe.timed_out -and $probe.exit_code -eq 0 -and $probe.stdout.Trim() -eq "Colosseum $expectedVersion") 'Executable version probe matches receiving phase'
    $report.app_version = $expectedVersion
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    if ($Phase -eq 'Baseline118') {
        Require (!(Test-Path -LiteralPath $SessionFile)) 'Baseline session file must be new'
        $tag = "a119win-$runId"
        $session = @{schema='colosseum.acceptance119.windows-session.v1'; tag=$tag; windows_sid=$sid; phases=@()}
    } else {
        $session = Get-Content -LiteralPath $SessionFile -Raw | ConvertFrom-Json
        Require ($session.schema -eq 'colosseum.acceptance119.windows-session.v1') 'Session schema matches'
        Require ($session.windows_sid -eq $sid) 'Restart uses the same disposable Windows account'
        $tag = $session.tag
        Require ($tag -match '^a119win-[a-f0-9]{32}$') 'Session tag has the runner-owned disposable format'
        $needed = if ($Phase -eq 'After119') { 'Baseline118' } else { 'After119' }
        Require (@($session.phases | Where-Object { $_.phase -eq $needed -and $_.status -eq 'PASS' }).Count -gt 0) "Prior $needed phase passed"
    }
    if ($SeedName) {
        Require ($Phase -eq 'Baseline118') 'Seed is allowed only for a new Baseline118'
        $seed = Join-Path (Join-Path $fixturePath 'seeds') $SeedName
        Require (Test-Path -LiteralPath $seed -PathType Container) 'Named synthetic seed exists'
        $expectedRoot = Join-Path ([Environment]::GetFolderPath('ApplicationData')) "Brotherhood\Colosseum-dltest-$tag"
        Require (!(Test-Path -LiteralPath $expectedRoot)) 'New seed destination must not already exist'
        New-Item -ItemType Directory -Path $expectedRoot | Out-Null
        foreach ($file in @(Get-ChildItem -LiteralPath $seed -Recurse -File)) {
            Require (!(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) 'Seed contains only regular fixture files'
            $relative = $file.FullName.Substring($seed.Length).TrimStart('\', '/')
            $destination = Join-Path $expectedRoot $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
        $report.seed = $seed
    }
    $script:pipe = "ColosseumAcceptance119-$runId"
    $report.profile_tag = $tag; $report.pipe = $script:pipe
    foreach ($variable in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'COLOSSEUM_*' })) {
        [Environment]::SetEnvironmentVariable($variable.Name, $null, 'Process')
    }
    $env:COLOSSEUM_APPDATA_TAG = $tag
    $env:COLOSSEUM_LANISTA_PIPE = $script:pipe
    $env:COLOSSEUM_LANISTA_DRIVE = '1'
    $stdout = Join-Path $runDir 'app.stdout.log'; $stderr = Join-Path $runDir 'app.stderr.log'
    $script:app = Start-Process -FilePath $exe -WorkingDirectory $package -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:appHandle = $script:app.SafeHandle
    $report.app_pid = $script:app.Id
    $record = Get-CimInstance Win32_Process -Filter "ProcessId=$($script:app.Id)"
    Require ($null -ne $record) 'Launched PID still exists'
    $script:owned[[int]$script:app.Id] = Get-ProcessRecord $record
    $ready = [Diagnostics.Stopwatch]::StartNew(); $pong = $null
    while ($ready.Elapsed.TotalSeconds -lt $ReadySeconds) {
        Observe-Children
        $script:app.Refresh()
        Require (!$script:app.HasExited) 'App remains alive while waiting for its bridge'
        $answer = Bridge 'ping'
        if ($null -ne $answer -and $answer.type -eq 'reply') {
            Require ([int]$answer.pid -eq $script:app.Id) 'Ping PID equals the process this runner launched'
            Require ($answer.pipe -eq $script:pipe) 'Ping names the exact owned pipe'
            Require ($answer.schema -eq 'colosseum.dev.v1.0' -and $answer.gates.drive -eq $true -and $answer.gates.write -eq $false) 'Lanista protocol and read-only write gate match'
            $pong = $answer; $script:bridgeValidated = $true; break
        }
        Start-Sleep -Milliseconds 250
    }
    Require ($null -ne $pong) 'Bounded readiness obtained a valid ping'
    Save-Json $pong (Join-Path $runDir 'ping.json')
    $state = Bridge 'get-state'
    Require ($null -ne $state -and $state.type -eq 'reply') 'get-state is a valid protocol reply'
    Assert-IsolatedRoot $state.appDataRoot $tag
    Assert-IsolatedRoot $state.cacheRoot $tag
    Require (@($state.windows | Where-Object { $_.visible -eq $true }).Count -gt 0) 'App reports a visible window'
    if ($Phase -ne 'Baseline118') {
        Require ($state.appDataRoot -eq $session.app_data_root -and $state.cacheRoot -eq $session.cache_root) 'Migration/restart uses exactly the recorded owned roots'
    }
    $report.app_data_root = $state.appDataRoot; $report.cache_root = $state.cacheRoot
    Save-Json $state (Join-Path $runDir 'get-state-before.json')
    $journey = Invoke-Recorded $script:lanistaPath @('--pipe', $script:pipe, '--timeout', '10000', '--verbose', 'run', $scenarioPath) $ScenarioSeconds
    if ($journey.timed_out -or $journey.exit_code -ne 0) {
        $failure = Classify-JourneyFailure $Phase $journey.exit_code $journey.timed_out $journey.stdout
        $script:app.Refresh()
        if ($failure.status -eq 'BLOCKED' -and !$script:app.HasExited) {
            $report.status = 'BLOCKED'
            $report.journey_failure = $failure
            throw $failure.reason
        }
    }
    Require (!$journey.timed_out -and $journey.exit_code -eq 0) 'Lanista journey completed with all assertions passing'
    $after = Bridge 'get-state'
    Require ($null -ne $after -and $after.type -eq 'reply') 'App still answers after the journey'
    Save-Json $after (Join-Path $runDir 'get-state-after.json')
    $capture = Bridge 'get-state' @('--grab', 'window')
    if ($null -ne $capture -and $capture.PSObject.Properties.Name -contains 'grabPath' -and (Test-Path -LiteralPath $capture.grabPath)) {
        Copy-Item -LiteralPath $capture.grabPath -Destination (Join-Path $runDir 'window.png')
    }
    $report.status = 'PASS'
} catch {
    if ($report.status -ne 'BLOCKED') { $report.status = 'FAIL' }
    $report.error = $_.Exception.Message
    if ($script:bridgeValidated) {
        try {
            $failureCapture = Bridge 'get-state' @('--grab', 'window')
            if ($null -ne $failureCapture) {
                Save-Json $failureCapture (Join-Path $runDir 'failure-state.json')
                if ($failureCapture.PSObject.Properties.Name -contains 'grabPath' -and (Test-Path -LiteralPath $failureCapture.grabPath)) {
                    Copy-Item -LiteralPath $failureCapture.grabPath -Destination (Join-Path $runDir 'failure.png')
                }
            }
        } catch { $report.failure_capture_error = $_.Exception.Message }
    }
} finally {
    try {
        if ($null -ne $script:app) {
            Observe-Children
            $close = [Diagnostics.Stopwatch]::StartNew()
            $script:app.Refresh()
            if ($script:app.HasExited -and $report.status -eq 'BLOCKED') {
                $report.status = 'FAIL'; $report.shutdown_error = 'App exited before the runner requested normal shutdown.'
            }
            if (!$script:app.HasExited) {
                if ($script:bridgeValidated) {
                    $quit = Bridge 'ui-keypress' @('key=Ctrl+Q')
                    $report.normal_quit_reply = $quit
                    if ($null -eq $quit -or $quit.type -ne 'reply' -or $quit.pressed -ne 'Ctrl+Q') {
                        $report.status = 'FAIL'; $report.shutdown_error = 'Normal quit did not return its expected protocol reply.'
                    }
                } else { $report.close_message_sent = $script:app.CloseMainWindow() }
                while (!$script:app.HasExited -and $close.Elapsed.TotalSeconds -lt $ShutdownSeconds) {
                    Observe-Children; Start-Sleep -Milliseconds 200; $script:app.Refresh()
                }
            }
            if (!$script:app.HasExited) {
                $report.status = 'FAIL'; $report.shutdown_error = 'Graceful close timed out; forced recovery is not a normal shutdown.'
            } else {
                $script:app.WaitForExit(); $report.app_exit_code = $script:app.ExitCode
                if ($script:app.ExitCode -ne 0) { $report.status = 'FAIL'; $report.shutdown_error = 'App exited with nonzero code.' }
            }
            # Give observed helpers a separate bounded interval to exit themselves.
            $helpers = [Diagnostics.Stopwatch]::StartNew()
            do {
                $remaining = @($script:owned.Values | Where-Object { Test-OwnedProcess $_ })
                if ($remaining.Count -eq 0) { break }
                Start-Sleep -Milliseconds 200
            } while ($helpers.Elapsed.TotalSeconds -lt $ShutdownSeconds)
            foreach ($ownedProcess in $remaining) {
                if (Test-OwnedProcess $ownedProcess) {
                    $report.status = 'FAIL'
                    $forced.Add($ownedProcess)
                    Stop-Process -Id $ownedProcess.pid -Force -ErrorAction SilentlyContinue
                }
            }
            $report.shutdown_ms = $close.ElapsedMilliseconds
            $report.owned_processes = @($script:owned.Values)
            if ($forced.Count -gt 0) {
                $script:app.WaitForExit(5000) | Out-Null
                $script:app.Refresh()
                if ($script:app.HasExited) { $report.app_exit_code = $script:app.ExitCode }
                $report.surviving_owned_pids = @($script:owned.Values | Where-Object { Test-OwnedProcess $_ } | ForEach-Object { $_.pid })
            }
        }
    } catch {
        $report.status = 'FAIL'; $report.cleanup_error = $_.Exception.Message
        if ($null -ne $script:app) {
            $recovery = Recover-OwnedApp $script:app ($ShutdownSeconds * 1000)
            $report.cleanup_recovery = $recovery
            $report.app_exit_code = $recovery.exit_code
            $report.surviving_app_pid = $recovery.surviving_pid
            if ($recovery.forced) { $forced.Add($recovery) }
            $report.helper_cleanup_status = 'BLOCKED: CIM cleanup failed; unverified helper PIDs were not swept.'
        }
    } finally {
        foreach ($variable in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'COLOSSEUM_*' })) {
            [Environment]::SetEnvironmentVariable($variable.Name, $null, 'Process')
        }
        foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
        $report.finished_at = [DateTime]::UtcNow.ToString('o')
        Save-Json $report (Join-Path $runDir 'receiving-result.json')
    }
}
if ($report.status -eq 'PASS') {
    $checkpoint = @{phase=$Phase; status='PASS'; result=(Join-Path $runDir 'receiving-result.json'); app_sha256=$report.app_sha256}
    if ($Phase -eq 'Baseline118') {
        $session.app_data_root = $report.app_data_root; $session.cache_root = $report.cache_root
    }
    $session.phases = @($session.phases) + @($checkpoint)
    $sessionParent = Split-Path -Parent ([IO.Path]::GetFullPath($SessionFile))
    New-Item -ItemType Directory -Force -Path $sessionParent | Out-Null
    Save-Json $session $SessionFile
}
Write-Output ("{0}: {1}; evidence: {2}" -f $Phase, $report.status, $runDir)
if ($report.status -eq 'BLOCKED') { exit 2 }
if ($report.status -ne 'PASS') { exit 1 }
# Phase PASS covers the supplied journey and owned lifecycle only. Both receiving
# cases above remain incomplete until the manual evidence packet is reviewed.
exit 0
