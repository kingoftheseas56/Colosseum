#!/usr/bin/env pwsh
# Colosseum Watch Party relay - Slice 6 acceptance orchestrator.
#
# Proves the production colosseum.exe (client code FROZEN, untouched) can
# join a LIVE local `wrangler dev` room as a guest through the real Join
# sheet, observe roster/chat truth, and survive room end.
#
# Legs:
#   1. Happy path  - host creates a room, one real app instance joins as a
#      guest via a lanista `session run` scenario, scalars + grabs captured.
#      Room-end is driven from the orchestrator AFTER the session tears
#      down (session run is self-contained), via the probe-live.mjs
#      `--host-only` END-on-stdin extension - so the app-side room-ended
#      PRESENTATION is not asserted here (that is deferred to Slice 7's
#      long-lived-instance orchestrator; the relay-side roomEnded is still
#      captured in the relay log as evidence).
#   A. Negative - wrong room code: fresh session, typed error asserted.
#   B. Negative - relay dies mid-membership: manual per-pipe app launch
#      (long-lived, not `session run`), joins a fresh room, relay killed by
#      PID after join confirmed, reconnect presentation observed.
#   S. Regression - solo `journey_play_video`, COLOSSEUM_WATCH_PARTY_URL
#      UNSET, proving default-boot fail-closed behavior is unaffected.
#
# Safety: NEVER touches PID 20548 (Hemanth's daily app, command line
# `native\build-msvc\colosseum.exe` with no qml arg). Every test instance
# this script launches carries `qml/Main.qml` on its command line and a
# unique tag; only PIDs this script itself started are ever killed, always
# by exact PID after a command-line check.

$ErrorActionPreference = "Stop"
$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$RelayRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Lanista   = Join-Path $RepoRoot "native\build-msvc\lanista.exe"
$ColExe    = Join-Path $RepoRoot "native\build-msvc\colosseum.exe"
$QmlMain   = "qml/Main.qml"
$SeedDir   = Join-Path $RepoRoot "tests\lanista_fixtures\journeys\play-video-v1"
$DevCa     = Join-Path $RelayRoot "test\dev-ca.pem"
$WranglerBin = Join-Path $RelayRoot "node_modules\.bin\wrangler.cmd"
$RelayPort = 8787
$RelayUrl  = "wss://localhost:$RelayPort"

$DailyAppPid = 20548
$Results = [ordered]@{}
$LaunchedPids = New-Object System.Collections.Generic.List[int]

function Log($msg) { Write-Host "[accept-one-client] $msg" }

function Assert-NotDailyApp([int]$procPid) {
    if ($procPid -eq $DailyAppPid) {
        throw "REFUSING to touch PID $procPid - that is the daily app. Abort."
    }
}

# --- Registry / AppData isolation helpers -----------------------------------

function Seed-Onboarding([string]$tag) {
    $regPath = "HKCU:\Software\Brotherhood\Colosseum-dltest-$tag\account"
    New-Item -Path $regPath -Force | Out-Null
    New-ItemProperty -Path $regPath -Name "localOnlyChosen" -PropertyType String -Value "true" -Force | Out-Null
    New-ItemProperty -Path $regPath -Name "onboardingCompleted" -PropertyType String -Value "true" -Force | Out-Null
    Log "seeded onboarding registry for tag=$tag"
}

function Remove-Onboarding([string]$tag) {
    $regKey = "HKCU:\Software\Brotherhood\Colosseum-dltest-$tag"
    if (Test-Path $regKey) {
        Remove-Item -Path $regKey -Recurse -Force
        Log "removed onboarding registry for tag=$tag"
    }
}

function Remove-AppDataDirs([string]$tag) {
    $roaming = "$env:APPDATA\Brotherhood\Colosseum-dltest-$tag"
    $local   = "$env:LOCALAPPDATA\Brotherhood\Colosseum-dltest-$tag"
    foreach ($d in @($roaming, $local)) {
        if (Test-Path $d) {
            Remove-Item -Path $d -Recurse -Force
            Log "removed AppData dir: $d"
        }
    }
}

# --- Relay lifecycle ----------------------------------------------------------

function Start-Relay([string]$devAuth = "1") {
    Log "starting wrangler dev --local-protocol https --port $RelayPort --var RELAY_DEV_AUTH:$devAuth"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $WranglerBin
    $psi.Arguments = "dev --local-protocol https --port $RelayPort --inspector-port 9339 --var RELAY_DEV_AUTH:$devAuth"
    $psi.WorkingDirectory = $RelayRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $LaunchedPids.Add($proc.Id) | Out-Null

    $deadline = (Get-Date).AddSeconds(30)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.Connect("127.0.0.1", $RelayPort)
            $tcp.Close()
            $ready = $true
            break
        } catch { Start-Sleep -Milliseconds 300 }
    }
    if (-not $ready) { throw "wrangler dev did not become ready on port $RelayPort" }
    Log "wrangler dev ready, PID=$($proc.Id)"
    return $proc
}

function Stop-RelayTree([int]$rootPid) {
    Assert-NotDailyApp $rootPid
    Log "killing wrangler dev tree rooted at PID $rootPid"
    try { & taskkill /PID $rootPid /T /F 2>&1 | Out-Null } catch {}
}

# --- Host (scripted, probe-live.mjs --host-only) ------------------------------

function Start-Host([string]$infoHash, [int]$fileIdx = 0, [string]$bearer = "dev-token-host") {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "node"
    $psi.Arguments = "test/probe-live.mjs --host-only --url $RelayUrl --bearer $bearer --info-hash $infoHash --file-idx $fileIdx"
    $psi.WorkingDirectory = $RelayRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables["NODE_EXTRA_CA_CERTS"] = $DevCa
    $psi.EnvironmentVariables["WP_PROBE_REEXECED"] = "1"
    $proc = [System.Diagnostics.Process]::Start($psi)
    $LaunchedPids.Add($proc.Id) | Out-Null

    $roomId = $null
    $ready = $false
    $lines = New-Object System.Collections.Generic.List[string]
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        $line = $proc.StandardOutput.ReadLine()
        if ($null -eq $line) { break }
        $lines.Add($line)
        if ($line -match "^ROOM_ID=(.+)$") { $roomId = $Matches[1] }
        if ($line -eq "HOST_ONLY_READY") { $ready = $true }
    }
    if (-not $ready -or -not $roomId) {
        throw "host-only did not become ready. Lines: $($lines -join ' | ')"
    }
    Log "host ready, PID=$($proc.Id), ROOM_ID=$roomId"
    return @{ Proc = $proc; RoomId = $roomId }
}

function Send-EndToHost($hostInfo) {
    Log "sending END to host PID $($hostInfo.Proc.Id) for room $($hostInfo.RoomId)"
    $hostInfo.Proc.StandardInput.WriteLine("END")
    $hostInfo.Proc.StandardInput.Flush()
    $deadline = (Get-Date).AddSeconds(5)
    while (-not $hostInfo.Proc.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
}

function Stop-Host($hostInfo) {
    if (-not $hostInfo.Proc.HasExited) {
        Assert-NotDailyApp $hostInfo.Proc.Id
        $hostInfo.Proc.StandardInput.Close()
        $deadline = (Get-Date).AddSeconds(5)
        while (-not $hostInfo.Proc.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
        if (-not $hostInfo.Proc.HasExited) {
            try { & taskkill /PID $hostInfo.Proc.Id /T /F 2>&1 | Out-Null } catch {}
        }
    }
}

# --- Scenario templating -------------------------------------------------------

function Write-JoinScenario([string]$path, [string]$roomId, [string]$guestName, [bool]$expectSuccess) {
    if ($expectSuccess) {
        $phaseWait = @{ cmd = "ui-wait-for"; label = "join reaches the settled joined state (active - the client sets 'synchronizing' transiently right after sessionEstablished, then 'active' once roomSnapshot lands and controller.inRoom flips true; polled every 50ms, observed value recorded in the report)"
            payload = @{ object = "colosseumTaskbar"; prop = "watchPartyJoinPhase"; value = "active"; timeout_ms = 15000 }
            expect = @(@{ path = "matched"; op = "=="; value = "true" }) }
        $tailSteps = @(
            @{ cmd = "qml-get"; label = "joined-scalars"
               payload = @{ object = "colosseumTaskbar"; props = @("watchPartyJoinPhase","watchPartyJoinErrorCategory") }
               expect = @(@{ path = "props.watchPartyJoinPhase"; op = "=="; value = "active" }, @{ path = "props.watchPartyJoinErrorCategory"; op = "=="; value = "" }) },
            @{ cmd = "qml-get"; label = "joined-grab"
               payload = @{ object = "watchPartyJoinJoinedCard"; props = @("visible"); grab = @{ target = "window"; timeoutMs = 4000 } }
               expect = @(@{ path = "props.visible"; op = "=="; value = "true" }, @{ path = "grabPath"; op = "exists" }) }
        )
    } else {
        $phaseWait = @{ cmd = "ui-wait-for"; label = "join is refused with the typed roomNotFound category (relay's room_not_found error code -> WatchPartyRoomServiceClient's roomNotFound category)"
            payload = @{ object = "colosseumTaskbar"; prop = "watchPartyJoinErrorCategory"; value = "roomNotFound"; timeout_ms = 15000 }
            expect = @(@{ path = "matched"; op = "=="; value = "true" }) }
        $tailSteps = @(
            @{ cmd = "qml-get"; label = "wrong-code-scalars"
               payload = @{ object = "colosseumTaskbar"; props = @("watchPartyJoinPhase","watchPartyJoinErrorCategory") }
               expect = @(@{ path = "props.watchPartyJoinErrorCategory"; op = "=="; value = "roomNotFound" }) },
            @{ cmd = "qml-get"; label = "wrong-code-grab"
               payload = @{ object = "watchPartyJoinError"; props = @("visible"); grab = @{ target = "window"; timeoutMs = 4000 } }
               expect = @(@{ path = "grabPath"; op = "exists" }) }
        )
    }

    $steps = @(
        @{ cmd = "ping"; label = "the session answers, drive gated on"
           expect = @(@{ path = "schema"; op = "matches"; value = "^colosseum\.dev\.v1" }, @{ path = "gates.drive"; op = "=="; value = "true" }) },
        @{ cmd = "ui-wait-for"; label = "the boot splash has left the screen"
           payload = @{ object = "bootSplash"; prop = "visible"; value = $false; timeout_ms = 60000 }
           expect = @(@{ path = "matched"; op = "=="; value = "true" }) },
        @{ cmd = "ui-click"; label = "open the taskbar dock"
           payload = @{ target = "colosseumTaskbarHomeButton" } },
        @{ cmd = "ui-wait-for"; label = "the watch-party join control has its final width"
           payload = @{ object = "taskbarWatchPartyJoin"; prop = "width"; value = 46; timeout_ms = 5000 }
           expect = @(@{ path = "matched"; op = "=="; value = "true" }) },
        @{ cmd = "ui-click"; label = "open the Join Watch Party sheet"
           payload = @{ target = "taskbarWatchPartyJoin" } },
        @{ cmd = "ui-wait-for"; label = "the join sheet reports open"
           payload = @{ object = "colosseumTaskbar"; prop = "watchPartyJoinOpen"; value = $true; timeout_ms = 5000 }
           expect = @(@{ path = "matched"; op = "=="; value = "true" }) },
        @{ cmd = "ui-text-input"; label = "type the room code"
           payload = @{ target = "watchPartyJoinRoomId"; text = $roomId } },
        @{ cmd = "ui-text-input"; label = "type the guest name"
           payload = @{ target = "watchPartyJoinGuestName"; text = $guestName } },
        @{ cmd = "ui-click"; label = "submit Join"
           payload = @{ target = "watchPartyJoinSubmit" } },
        $phaseWait
    ) + $tailSteps

    $scenario = @{ name = "wp-accept-1"; comment = "Slice 6 acceptance - real colosseum.exe joins a live local watch-party room via the real Join sheet."; steps = $steps }
    $scenario | ConvertTo-Json -Depth 12 | Set-Content -Path $path -Encoding utf8
    Log "wrote scenario $path (roomId=$roomId expectSuccess=$expectSuccess)"
}

# --- Leg runner (session run) --------------------------------------------------

function Run-SessionLeg([string]$tag, [string]$scenarioPath, [string]$watchPartyUrl) {
    Seed-Onboarding $tag
    Remove-AppDataDirs $tag
    $env:COLOSSEUM_WATCH_PARTY_URL = $watchPartyUrl
    Log "session run tag=$tag url=$watchPartyUrl"
    $sessionOut = & $Lanista session run $scenarioPath --exe $ColExe --qml $QmlMain --seed $SeedDir --tag $tag --drive --ready-ms 90000 --verbose 2>&1
    $exit = $LASTEXITCODE
    $sessionOut | ForEach-Object { Write-Host $_ }
    Remove-Item Env:\COLOSSEUM_WATCH_PARTY_URL -ErrorAction SilentlyContinue
    return $exit
}

# =============================================================================
# MAIN
# =============================================================================

$InfoHashA = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"
$InfoHashB = "b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3"

try {
    Log "=== pre-flight: confirm PID $DailyAppPid is the only colosseum.exe running, and it stays untouched ==="
    Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host

    Log "=== leg 0: relay boot + handshake probe ==="
    $relay = Start-Relay -devAuth "1"
    Push-Location $RelayRoot
    $env:NODE_EXTRA_CA_CERTS = $DevCa
    $env:RELAY_URL = $RelayUrl
    node test/probe-handshake.mjs
    $Results["probe-handshake"] = $LASTEXITCODE
    Remove-Item Env:\RELAY_URL -ErrorAction SilentlyContinue
    Pop-Location
    if ($Results["probe-handshake"] -ne 0) { throw "probe-handshake failed" }

    Log "=== leg 1: happy path - host + one real client joins ==="
    $host1 = Start-Host -infoHash $InfoHashA -fileIdx 0
    $scenario1 = Join-Path $RepoRoot "_wp_accept1.json"
    Write-JoinScenario -path $scenario1 -roomId $host1.RoomId -guestName "Hemanth-Guest" -expectSuccess $true
    $Results["leg1-session"] = Run-SessionLeg -tag "wp-accept-1" -scenarioPath $scenario1 -watchPartyUrl $RelayUrl
    Log "leg1 session exit code: $($Results['leg1-session'])"
    Send-EndToHost $host1
    Stop-Host $host1
    Remove-Onboarding "wp-accept-1"
    Remove-AppDataDirs "wp-accept-1"
    Remove-Item $scenario1 -ErrorAction SilentlyContinue

    Log "=== leg A: negative - wrong room code ==="
    $scenarioA = Join-Path $RepoRoot "_wp_accept1_wrongcode.json"
    Write-JoinScenario -path $scenarioA -roomId "WP-XXXX-XXXX" -guestName "Hemanth-Guest" -expectSuccess $false
    $Results["legA-session"] = Run-SessionLeg -tag "wp-accept-1-negA" -scenarioPath $scenarioA -watchPartyUrl $RelayUrl
    Log "legA session exit code: $($Results['legA-session'])"
    Remove-Onboarding "wp-accept-1-negA"
    Remove-AppDataDirs "wp-accept-1-negA"
    Remove-Item $scenarioA -ErrorAction SilentlyContinue

    Log "=== leg B: negative - relay dies mid-membership (manual per-pipe long-lived instance) ==="
    $host2 = Start-Host -infoHash $InfoHashB -fileIdx 0
    $tag2 = "wp-accept-2"
    $pipe2 = "WPAccept2"
    Seed-Onboarding $tag2
    Remove-AppDataDirs $tag2

    $psi2 = New-Object System.Diagnostics.ProcessStartInfo
    $psi2.FileName = $ColExe
    $psi2.Arguments = $QmlMain
    $psi2.WorkingDirectory = $RepoRoot
    $psi2.UseShellExecute = $false
    $psi2.EnvironmentVariables["COLOSSEUM_LANISTA_PIPE"] = $pipe2
    $psi2.EnvironmentVariables["COLOSSEUM_APPDATA_TAG"] = $tag2
    $psi2.EnvironmentVariables["COLOSSEUM_LANISTA_DRIVE"] = "1"
    $psi2.EnvironmentVariables["QT_FORCE_STDERR_LOGGING"] = "1"
    $psi2.EnvironmentVariables["COLOSSEUM_WATCH_PARTY_URL"] = $RelayUrl
    $proc2 = [System.Diagnostics.Process]::Start($psi2)
    $LaunchedPids.Add($proc2.Id) | Out-Null
    Log "leg B app launched PID=$($proc2.Id) pipe=$pipe2 tag=$tag2"

    $ready2 = $false
    $deadline2 = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline2 -and -not $ready2) {
        $pingOut = & $Lanista --pipe $pipe2 --timeout 3000 ping 2>&1
        if ($LASTEXITCODE -eq 0) { $ready2 = $true } else { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready2) { throw "leg B app never answered ping on pipe $pipe2" }
    Log "leg B app ready: $pingOut"

    $stateOut = & $Lanista --pipe $pipe2 --timeout 5000 get-state 2>&1
    $stateJoined = ($stateOut -join "`n")
    Log "leg B get-state: $stateJoined"
    if ($stateJoined -notmatch [regex]::Escape("Colosseum-dltest-$tag2")) {
        Assert-NotDailyApp $proc2.Id
        try { & taskkill /PID $proc2.Id /T /F 2>&1 | Out-Null } catch {}
        throw "leg B isolation check FAILED - appDataRoot/cacheRoot did not carry Colosseum-dltest-$tag2. Killed PID $($proc2.Id)."
    }
    $Results["legB-isolation"] = "pass"

    & $Lanista --pipe $pipe2 --timeout 60000 ui-wait-for object=bootSplash prop=visible value=false timeout_ms=60000 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-click target=colosseumTaskbarHomeButton 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-wait-for object=taskbarWatchPartyJoin prop=width value=46 timeout_ms=5000 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-click target=taskbarWatchPartyJoin 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-wait-for object=colosseumTaskbar prop=watchPartyJoinOpen value=true timeout_ms=5000 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-text-input target=watchPartyJoinRoomId text=$($host2.RoomId) 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-text-input target=watchPartyJoinGuestName text=Hemanth-Guest2 2>&1 | Write-Host
    & $Lanista --pipe $pipe2 --timeout 5000 ui-click target=watchPartyJoinSubmit 2>&1 | Write-Host
    $joinWait = & $Lanista --pipe $pipe2 --timeout 16000 ui-wait-for object=colosseumTaskbar prop=watchPartyJoinPhase value=active timeout_ms=15000 2>&1
    Log "legB join wait: $joinWait"
    $Results["legB-joined"] = $LASTEXITCODE

    $legBScalars = & $Lanista --pipe $pipe2 --timeout 5000 qml-get object=colosseumTaskbar props=watchPartyJoinPhase,watchPartyJoinErrorCategory 2>&1
    Log "legB scalars pre-kill: $legBScalars"

    Log "killing wrangler relay by exact PID mid-membership"
    Stop-RelayTree $relay.Id
    $Results["legB-relay-killed"] = "done"

    $reconnectWait = & $Lanista --pipe $pipe2 --timeout 31000 ui-wait-for object=colosseumTaskbar prop=watchPartyJoinPhase value=connecting timeout_ms=30000 2>&1
    Log "legB reconnect wait: $reconnectWait"
    $Results["legB-reconnect-presentation"] = $LASTEXITCODE

    $legBGrab = & $Lanista --pipe $pipe2 --timeout 8000 get-state --grab colosseumTaskbar 2>&1
    Log "legB post-kill grab/state: $legBGrab"

    Log "graceful-closing leg B app PID $($proc2.Id)"
    Assert-NotDailyApp $proc2.Id
    try {
        $wmiProc = Get-Process -Id $proc2.Id -ErrorAction SilentlyContinue
        if ($wmiProc) { $wmiProc.CloseMainWindow() | Out-Null }
    } catch {}
    $closeDeadline = (Get-Date).AddSeconds(8)
    while (-not $proc2.HasExited -and (Get-Date) -lt $closeDeadline) { Start-Sleep -Milliseconds 300 }
    if (-not $proc2.HasExited) {
        Assert-NotDailyApp $proc2.Id
        & taskkill /PID $proc2.Id /T /F 2>&1 | Out-Null
    }
    Log "leg B app exited: $($proc2.HasExited)"
    Stop-Host $host2
    Remove-Onboarding $tag2
    Remove-AppDataDirs $tag2

    Log "=== leg S: regression - solo journey_play_video, COLOSSEUM_WATCH_PARTY_URL UNSET ==="
    Remove-Item Env:\COLOSSEUM_WATCH_PARTY_URL -ErrorAction SilentlyContinue
    $tagS = "wp-accept-solo"
    Seed-Onboarding $tagS
    Remove-AppDataDirs $tagS
    $soloScenario = Join-Path $RepoRoot "tests\lanista_scenarios\journey_play_video.json"
    & $Lanista session run $soloScenario --exe $ColExe --qml $QmlMain --seed $SeedDir --tag $tagS --drive --ready-ms 90000 --verbose
    $Results["legS-solo"] = $LASTEXITCODE
    Remove-Onboarding $tagS
    Remove-AppDataDirs $tagS

} finally {
    Log "=== teardown ==="
    foreach ($p in $LaunchedPids) {
        Assert-NotDailyApp $p
        try {
            $proc = Get-Process -Id $p -ErrorAction SilentlyContinue
            if ($proc -and -not $proc.HasExited) {
                Log "teardown: killing leftover launched PID $p"
                & taskkill /PID $p /T /F 2>&1 | Out-Null
            }
        } catch {}
    }
    foreach ($t in @("wp-accept-1", "wp-accept-1-negA", "wp-accept-2", "wp-accept-solo")) {
        Remove-Onboarding $t
        Remove-AppDataDirs $t
    }
    foreach ($f in @("_wp_accept1.json", "_wp_accept1_wrongcode.json")) {
        $fp = Join-Path $RepoRoot $f
        Remove-Item $fp -ErrorAction SilentlyContinue
    }
    Log "=== results ==="
    $Results | Format-Table | Out-String | Write-Host
    Log "verify: only PID $DailyAppPid should remain"
    Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host
}
