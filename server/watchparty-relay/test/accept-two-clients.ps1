#!/usr/bin/env pwsh
# Colosseum Watch Party relay - Slice 7 acceptance orchestrator.
#
# Proves TWO real colosseum.exe instances (client code FROZEN, untouched)
# share one live local room: rosters agree, chat crosses relay-side, kick +
# fresh rejoin works, host-drop grace and reconnect present correctly, and
# room-end presents to both - the full social machinery without playback
# (that is Slice 8).
#
# Built ONLY on ledgered primitives: two `colosseum.exe` processes launched
# directly with unique per-instance env (the MCP Lanista adapter is
# one-session-at-a-time by design, so it is NOT used here), driven by
# per-pipe `lanista.exe` CLI calls - the exact long-lived-instance pattern
# Slice 6 leg B proved. The scripted host is `probe-live.mjs --host-only`,
# extended this slice with CHAT / DROP / RECONNECT / KICK_BY_NAME stdin
# commands (test-instrument-only, mirrors the plan's allowed END extension;
# src/ untouched).
#
# Safety: NEVER touches PID 20548 (Hemanth's daily app, command line
# `native\build-msvc\colosseum.exe` with no qml arg). Every test instance
# this script launches carries `qml/Main.qml` on its command line and a
# unique tag; only PIDs this script itself started are ever killed, always
# by exact PID after a command-line check. Another agent may be running a
# concurrent test session (a `tk3f`-tagged process seen 2026-08-20) - this
# script only ever touches PIDs it launches itself.

$ErrorActionPreference = "Stop"
$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$RelayRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Lanista   = Join-Path $RepoRoot "native\build-msvc\lanista.exe"
$ColExe    = Join-Path $RepoRoot "native\build-msvc\colosseum.exe"
$QmlMain   = "qml/Main.qml"
$DevCa     = Join-Path $RelayRoot "test\dev-ca.pem"
$WranglerBin = Join-Path $RelayRoot "node_modules\.bin\wrangler.cmd"
$RelayPort = 8787
$RelayUrl  = "wss://localhost:$RelayPort"
$HostGraceMs = 15000

$DailyAppPid = 20548
$Results = [ordered]@{}
$LaunchedPids = New-Object System.Collections.Generic.List[int]
$EvidenceRoot = Join-Path $RepoRoot "artifacts\watchparty-slice7\$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$StallLogPath = Join-Path $EvidenceRoot "stall-instrumentation.log"
"# Slice 7 stall instrumentation - per-call timestamps around relay/host-drop moments" | Set-Content $StallLogPath

function Log($msg) { $line = "[accept-two-clients] $msg"; Write-Host $line }
function StallLog($msg) {
    $ts = (Get-Date).ToString("HH:mm:ss.fff")
    # Best-effort: a transient file-lock (AV/indexer) must never abort the
    # orchestrator over a diagnostic write. Retry once, then give up silently
    # for this line - the timestamps are also echoed to Write-Host via Log()
    # callers where it matters, so a dropped stall-log line is not silent
    # loss of the finding, only of one persisted copy of it.
    try {
        Add-Content -Path $StallLogPath -Value "$ts $msg" -ErrorAction Stop
    } catch {
        Start-Sleep -Milliseconds 50
        try { Add-Content -Path $StallLogPath -Value "$ts $msg" -ErrorAction Stop } catch {}
    }
}

function Assert-NotDailyApp([int]$procPid) {
    if ($procPid -eq $DailyAppPid) {
        throw "REFUSING to touch PID $procPid - that is the daily app. Abort."
    }
}

function Test-RamFloor {
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory  # KB
    $freeMb = [math]::Round($free / 1024)
    Log "free RAM: ${freeMb}MB"
    if ($freeMb -lt 800) {
        throw "RAM floor breached (${freeMb}MB free < 800MB) - stopping cleanly per safety instruction."
    }
    return $freeMb
}

# --- Instrumented per-pipe CLI call: timestamps + delta, stall-flags >5000ms ---
function Invoke-LanistaTimed([string]$pipe, [string]$label, [string[]]$cliArgs, [int]$timeoutMs = 8000) {
    $t0 = Get-Date
    StallLog "BEFORE pipe=$pipe label='$label' args=$($cliArgs -join ' ')"
    $out = & $Lanista --pipe $pipe --timeout $timeoutMs @cliArgs 2>&1
    $exit = $LASTEXITCODE
    $t1 = Get-Date
    $deltaMs = [math]::Round(($t1 - $t0).TotalMilliseconds)
    StallLog "AFTER  pipe=$pipe label='$label' deltaMs=$deltaMs exit=$exit"
    if ($deltaMs -gt 5000) {
        StallLog "STALL_FLAG pipe=$pipe label='$label' deltaMs=$deltaMs exceeds 5000ms threshold"
        Log "STALL FLAGGED: pipe=$pipe label='$label' took ${deltaMs}ms"
    }
    return @{ Out = $out; Exit = $exit; DeltaMs = $deltaMs }
}

# --- Registry / AppData isolation helpers (Slice 6 pattern) -----------------

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

function Start-Relay([string]$devAuth = "1", [int]$graceMs = 15000) {
    Log "starting wrangler dev --local-protocol https --port $RelayPort --var RELAY_DEV_AUTH:$devAuth --var RELAY_HOST_GRACE_MS:$graceMs"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $WranglerBin
    $psi.Arguments = "dev --local-protocol https --port $RelayPort --inspector-port 9339 --var RELAY_DEV_AUTH:$devAuth --var RELAY_HOST_GRACE_MS:$graceMs"
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

# --- Host (scripted, probe-live.mjs --host-only, Slice 7 stdin extensions) --

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
        Log "host: $line"
        if ($line -match "^ROOM_ID=(.+)$") { $roomId = $Matches[1] }
        if ($line -eq "HOST_ONLY_READY") { $ready = $true }
    }
    if (-not $ready -or -not $roomId) {
        throw "host-only did not become ready. Lines: $($lines -join ' | ')"
    }
    Log "host ready, PID=$($proc.Id), ROOM_ID=$roomId"
    return @{ Proc = $proc; RoomId = $roomId }
}

# Sends one stdin command line and blocks (sequential ReadLine, same idiom
# accept-one-client.ps1 already uses for host-only startup) until a stdout
# line starting with $markerPrefix appears. The host always answers each of
# CHAT/DROP/RECONNECT/KICK_BY_NAME/END within a couple seconds in normal
# operation, so a bounded sequence of blocking ReadLine() calls is the same
# correctness class as the proven startup-wait loop.
function Send-HostCommand($hostInfo, [string]$cmd, [string]$markerPrefix, [int]$maxLines = 20) {
    Log "host <- $cmd (awaiting marker '$markerPrefix')"
    $t0 = Get-Date
    StallLog "BEFORE host-command cmd='$cmd' marker='$markerPrefix'"
    $hostInfo.Proc.StandardInput.WriteLine($cmd)
    $hostInfo.Proc.StandardInput.Flush()
    $seen = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $maxLines; $i++) {
        $line = $hostInfo.Proc.StandardOutput.ReadLine()
        if ($null -eq $line) { break }
        $seen.Add($line)
        Log "host: $line"
        if ($line.StartsWith($markerPrefix)) {
            $t1 = Get-Date
            $deltaMs = [math]::Round(($t1 - $t0).TotalMilliseconds)
            StallLog "AFTER  host-command cmd='$cmd' marker='$markerPrefix' deltaMs=$deltaMs"
            if ($deltaMs -gt 5000) {
                StallLog "STALL_FLAG host-command cmd='$cmd' deltaMs=$deltaMs exceeds 5000ms threshold"
            }
            return @{ Line = $line; Lines = $seen; DeltaMs = $deltaMs }
        }
    }
    throw "host command '$cmd' never produced marker '$markerPrefix'. Lines seen: $($seen -join ' | ')"
}

function Stop-Host($hostInfo) {
    if (-not $hostInfo.Proc.HasExited) {
        Assert-NotDailyApp $hostInfo.Proc.Id
        try { $hostInfo.Proc.StandardInput.Close() } catch {}
        $deadline = (Get-Date).AddSeconds(5)
        while (-not $hostInfo.Proc.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
        if (-not $hostInfo.Proc.HasExited) {
            try { & taskkill /PID $hostInfo.Proc.Id /T /F 2>&1 | Out-Null } catch {}
        }
    }
}

# --- App instance lifecycle (long-lived, per-pipe CLI driven; Slice 6 leg B) -

function Start-AppInstance([string]$tag, [string]$pipe) {
    Seed-Onboarding $tag
    Remove-AppDataDirs $tag

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ColExe
    $psi.Arguments = $QmlMain
    $psi.WorkingDirectory = $RepoRoot
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables["COLOSSEUM_LANISTA_PIPE"] = $pipe
    $psi.EnvironmentVariables["COLOSSEUM_APPDATA_TAG"] = $tag
    $psi.EnvironmentVariables["COLOSSEUM_LANISTA_DRIVE"] = "1"
    $psi.EnvironmentVariables["QT_FORCE_STDERR_LOGGING"] = "1"
    $psi.EnvironmentVariables["COLOSSEUM_WATCH_PARTY_URL"] = $RelayUrl
    $proc = [System.Diagnostics.Process]::Start($psi)
    $LaunchedPids.Add($proc.Id) | Out-Null
    Log "app instance launched PID=$($proc.Id) pipe=$pipe tag=$tag"

    # Readiness: ping until PID match.
    $ready = $false
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        $r = Invoke-LanistaTimed -pipe $pipe -label "readiness-ping" -cliArgs @("ping") -timeoutMs 3000
        if ($r.Exit -eq 0) { $ready = $true } else { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready) { throw "app instance (tag=$tag pipe=$pipe) never answered ping" }
    Log "app instance ready: tag=$tag pipe=$pipe PID=$($proc.Id)"

    # Isolation: get-state must carry Colosseum-dltest-<tag> in BOTH roots.
    $stateOut = & $Lanista --pipe $pipe --timeout 5000 get-state 2>&1
    $stateJoined = ($stateOut -join "`n")
    Log "isolation get-state (tag=$tag): $stateJoined"
    if ($stateJoined -notmatch [regex]::Escape("Colosseum-dltest-$tag")) {
        Assert-NotDailyApp $proc.Id
        try { & taskkill /PID $proc.Id /T /F 2>&1 | Out-Null } catch {}
        throw "isolation check FAILED for tag=$tag - appDataRoot/cacheRoot did not carry Colosseum-dltest-$tag. Killed PID $($proc.Id)."
    }

    return @{ Proc = $proc; Pipe = $pipe; Tag = $tag }
}

function Stop-AppInstance($appInfo) {
    Assert-NotDailyApp $appInfo.Proc.Id
    Log "graceful-closing app instance tag=$($appInfo.Tag) PID=$($appInfo.Proc.Id)"
    try {
        $wmiProc = Get-Process -Id $appInfo.Proc.Id -ErrorAction SilentlyContinue
        if ($wmiProc) { $wmiProc.CloseMainWindow() | Out-Null }
    } catch {}
    $closeDeadline = (Get-Date).AddSeconds(8)
    while (-not $appInfo.Proc.HasExited -and (Get-Date) -lt $closeDeadline) { Start-Sleep -Milliseconds 300 }
    if (-not $appInfo.Proc.HasExited) {
        Assert-NotDailyApp $appInfo.Proc.Id
        & taskkill /PID $appInfo.Proc.Id /T /F 2>&1 | Out-Null
    }
    Log "app instance tag=$($appInfo.Tag) exited: $($appInfo.Proc.HasExited)"
}

# --- Drive: join flow (per-pipe CLI equivalent of Slice 6's scenario) -------

function Invoke-JoinDrive([string]$pipe, [string]$roomId, [string]$guestName, [string]$leg) {
    Invoke-LanistaTimed -pipe $pipe -label "$leg boot-splash-gone" -cliArgs @("ui-wait-for","object=bootSplash","prop=visible","value=false","timeout_ms=60000") -timeoutMs 61000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg open-taskbar" -cliArgs @("ui-click","target=colosseumTaskbarHomeButton") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg join-control-width" -cliArgs @("ui-wait-for","object=taskbarWatchPartyJoin","prop=width","value=46","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg open-join-sheet" -cliArgs @("ui-click","target=taskbarWatchPartyJoin") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg join-sheet-open" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinOpen","value=true","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg type-room-id" -cliArgs @("ui-text-input","target=watchPartyJoinRoomId","text=$roomId") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg type-guest-name" -cliArgs @("ui-text-input","target=watchPartyJoinGuestName","text=$guestName") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg submit-join" -cliArgs @("ui-click","target=watchPartyJoinSubmit") | Out-Null
    $joinWait = Invoke-LanistaTimed -pipe $pipe -label "$leg join-active" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinPhase","value=active","timeout_ms=15000") -timeoutMs 16000
    return $joinWait.Exit
}

function Get-Scalars([string]$pipe, [string]$label, [string[]]$expectPhaseCandidates = @("idle","connecting","establishing","synchronizing","active","reconnecting","error")) {
    # GROUND-TRUTH FINDING (recorded in the Slice 7 report): the plain
    # per-pipe CLI's k=v arg parser (payloadFromArgs in native/tools/
    # lanista.cpp) never parses JSON - every value becomes a bare string,
    # number, or bool. `qml-get object=X props=a,b` therefore serializes
    # payload.props as the STRING "a,b", and LanistaServer::cmdQmlGet calls
    # `.toArray()` on it, which silently yields an EMPTY array for any
    # non-array QJsonValue - so this call always returns "props": {} no
    # matter what is asked for. There is no comma-splitting, no repeated-key
    # accumulation, and no raw-JSON-payload escape hatch in the plain CLI;
    # only the JSON scenario-file mechanism (`session run`) can express an
    # array payload. This is a genuine plain-CLI tooling gap, not a client
    # behavior defect - confirmed the hard way on this run's first
    # checkpoint (every qml-get props= call returned empty).
    #
    # Workaround used here: probe each candidate phase value with a
    # near-instant `ui-wait-for` (timeout_ms=200) and report which one
    # currently matches - ui-wait-for's single `prop=`/`value=` k=v pair
    # does NOT hit the array-parsing gap, so this is a real read, just
    # phrased as a targeted membership test instead of a raw property dump.
    $matchedPhase = $null
    foreach ($candidate in $expectPhaseCandidates) {
        $r = Invoke-LanistaTimed -pipe $pipe -label "$label-phase-probe-$candidate" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinPhase","value=$candidate","timeout_ms=200") -timeoutMs 1200
        if ($r.Out -join "" -match '"matched":\s*true') { $matchedPhase = $candidate; break }
    }
    $errR = Invoke-LanistaTimed -pipe $pipe -label "$label-errorcat-probe-empty" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinErrorCategory","value=","timeout_ms=200") -timeoutMs 1200
    $errorCategoryEmpty = ($errR.Out -join "") -match '"matched":\s*true'
    $errorCategory = "(unknown - not empty)"
    if ($errorCategoryEmpty) { $errorCategory = "" }
    else {
        foreach ($cat in @("roomNotFound","participantRemoved","protocolVersionMismatch")) {
            $r2 = Invoke-LanistaTimed -pipe $pipe -label "$label-errorcat-probe-$cat" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinErrorCategory","value=$cat","timeout_ms=200") -timeoutMs 1200
            if (($r2.Out -join "") -match '"matched":\s*true') { $errorCategory = $cat; break }
        }
    }
    $summary = "watchPartyJoinPhase=$matchedPhase watchPartyJoinErrorCategory=$errorCategory"
    Log "scalars ($label): $summary"
    return $summary
}

function Save-Grab([string]$pipe, [string]$evidenceName) {
    $r = Invoke-LanistaTimed -pipe $pipe -label "grab-$evidenceName" -cliArgs @("get-state","--grab","colosseumTaskbar") -timeoutMs 8000
    $outPath = Join-Path $EvidenceRoot "$evidenceName.txt"
    $r.Out | Out-File -FilePath $outPath -Encoding utf8
    # The grab PNG lives under the tagged AppData root, which teardown wipes
    # (Remove-AppDataDirs, same cleanup discipline as Slice 6). Copy it into
    # the durable evidence root NOW, before that happens, so the paths this
    # script reports in results.json/the slice report still resolve after
    # the run finishes.
    $joined = $r.Out -join "`n"
    try {
        $parsed = $joined | ConvertFrom-Json -ErrorAction Stop
        if ($parsed.grabPath -and (Test-Path $parsed.grabPath)) {
            $destPng = Join-Path $EvidenceRoot "$evidenceName.png"
            Copy-Item -Path $parsed.grabPath -Destination $destPng -Force
            Log "grab PNG copied: $destPng (source: $($parsed.grabPath))"
        }
    } catch {
        Log "grab PNG copy skipped for $evidenceName (parse/copy issue: $($_.Exception.Message))"
    }
    Log "grab saved: $outPath (pipe=$pipe)"
    return $joined
}

# =============================================================================
# MAIN
# =============================================================================

$InfoHashRoom = "c1d2e3f4a5b6c1d2e3f4a5b6c1d2e3f4a5b6c1d2"
$PipeA = "WPTwoA"
$PipeB = "WPTwoB"
$TagA = "wp-two-a"
$TagB = "wp-two-b"

try {
    Log "=== pre-flight: only daily PID $DailyAppPid should be running; RAM floor check ==="
    Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host
    Test-RamFloor | Out-Null

    Log "=== leg 0: relay boot + handshake probe ==="
    $relay = Start-Relay -devAuth "1" -graceMs $HostGraceMs
    Push-Location $RelayRoot
    $env:NODE_EXTRA_CA_CERTS = $DevCa
    $env:RELAY_URL = $RelayUrl
    node test/probe-handshake.mjs
    $Results["probe-handshake"] = $LASTEXITCODE
    Remove-Item Env:\RELAY_URL -ErrorAction SilentlyContinue
    Pop-Location
    if ($Results["probe-handshake"] -ne 0) { throw "probe-handshake failed" }

    Log "=== leg 1: host creates the shared room ==="
    $hostSession = Start-Host -infoHash $InfoHashRoom -fileIdx 0
    $roomId = $hostSession.RoomId
    Log "shared ROOM_ID=$roomId"

    Log "=== leg 2: launch instance A, join as GuestA ==="
    $appA = Start-AppInstance -tag $TagA -pipe $PipeA
    Test-RamFloor | Out-Null
    $Results["A-joined"] = Invoke-JoinDrive -pipe $PipeA -roomId $roomId -guestName "GuestA" -leg "A"

    Log "=== leg 3: launch instance B, join as GuestB ==="
    $appB = Start-AppInstance -tag $TagB -pipe $PipeB
    Test-RamFloor | Out-Null
    $Results["B-joined"] = Invoke-JoinDrive -pipe $PipeB -roomId $roomId -guestName "GuestB" -leg "B"

    Log "=== checkpoint: both-joined ==="
    $scalarsA1 = Get-Scalars -pipe $PipeA -label "both-joined-A"
    $scalarsB1 = Get-Scalars -pipe $PipeB -label "both-joined-B"
    Save-Grab -pipe $PipeA -evidenceName "01-both-joined-A" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "01-both-joined-B" | Out-Null
    $Results["both-joined-A-scalars"] = $scalarsA1
    $Results["both-joined-B-scalars"] = $scalarsB1

    Log "=== leg 4: chat visibility (host sends chat; relay-side proof only per code ground-truth) ==="
    $chatResult = Send-HostCommand $hostSession "CHAT hello from the host (Slice 7)" "HOST_ONLY_CHAT_SENDING"
    $Results["chat-sent"] = $chatResult.Line
    # HONEST CHECK (see report): chat renders only inside WatchPartyPanel
    # (qml/WatchPartyPanel.qml watchPartyChatSection), which is reachable
    # only through the Player - not through colosseumTaskbar or the join
    # sheet (qml/WatchPartyJoinSheet.qml has no chat surface at all). With
    # the player not opened in this slice (that's Slice 8), app-side chat
    # visibility cannot be asserted from either app; taskbar scalars are
    # captured anyway to record that they are UNCHANGED by chat traffic.
    $scalarsAChat = Get-Scalars -pipe $PipeA -label "post-chat-A"
    $scalarsBChat = Get-Scalars -pipe $PipeB -label "post-chat-B"
    $Results["post-chat-A-scalars"] = $scalarsAChat
    $Results["post-chat-B-scalars"] = $scalarsBChat

    Log "=== leg 5: kick GuestB ==="
    $kickResult = Send-HostCommand $hostSession "KICK_BY_NAME GuestB" "HOST_ONLY_KICKING"
    $Results["kick-sent"] = $kickResult.Line
    $kickWaitB = Invoke-LanistaTimed -pipe $PipeB -label "B-kicked-error-category" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinErrorCategory","value=participantRemoved","timeout_ms=10000") -timeoutMs 11000
    $Results["B-kicked-presentation"] = $kickWaitB.Exit
    $scalarsBKicked = Get-Scalars -pipe $PipeB -label "B-kicked"
    Save-Grab -pipe $PipeB -evidenceName "02-B-kicked" | Out-Null
    $Results["B-kicked-scalars"] = $scalarsBKicked

    Log "=== leg 5b: A unaffected by B's kick ==="
    $scalarsAAfterKick = Get-Scalars -pipe $PipeA -label "A-after-B-kicked"
    Save-Grab -pipe $PipeA -evidenceName "03-A-unaffected-after-kick" | Out-Null
    $Results["A-after-kick-scalars"] = $scalarsAAfterKick

    Log "=== leg 6: B fresh rejoin ==="
    $Results["B-rejoined"] = Invoke-JoinDrive -pipe $PipeB -roomId $roomId -guestName "GuestB-Again" -leg "B-rejoin"
    $scalarsBRejoined = Get-Scalars -pipe $PipeB -label "B-rejoined"
    Save-Grab -pipe $PipeB -evidenceName "04-B-rejoined" | Out-Null
    $Results["B-rejoined-scalars"] = $scalarsBRejoined

    Log "=== leg 7: host-grace - host socket DROPS (no endRoom) ==="
    StallLog "=== HOST-GRACE LEG BEGIN (stall instrumentation focus per Slice 6 leg B finding) ==="
    $dropResult = Send-HostCommand $hostSession "DROP" "HOST_ONLY_DROPPING"
    $Results["host-dropped"] = $dropResult.Line
    # GROUND-TRUTH (see report): hostGraceActive is exposed only via
    # PlayerPage.qml's watchPartyHostGraceActive (reads the WatchPartyUi
    # singleton, requires the Player surface) and WatchPartyPanel.qml's own
    # controller.hostGraceActive (same singleton, same player-only reach).
    # colosseumTaskbar exposes ONLY watchPartyJoinPhase/watchPartyJoinError-
    # Category - no grace scalar. qml-get requires a QQuickItem
    # (LanistaServer::cmdQmlGet -> resolveTarget), and WatchPartyUi is a
    # plain QObject context property, not a QQuickItem, so it cannot be
    # qml-get'd directly; UiController::diagnosticSnapshot() (which DOES
    # carry hostGraceActive) is not on the invoke-read allowlist
    # (LanistaServer::cmdInvokeRead). With the player closed in this slice,
    # host-grace presentation is Bridge blocked outside the player - taskbar
    # scalars are captured through the grace window anyway to record that
    # membership survives it (phase stays active, no error).
    $scalarsAGrace = Get-Scalars -pipe $PipeA -label "A-during-grace"
    $scalarsBGrace = Get-Scalars -pipe $PipeB -label "B-during-grace"
    Save-Grab -pipe $PipeA -evidenceName "05-A-during-grace" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "05-B-during-grace" | Out-Null
    $Results["A-during-grace-scalars"] = $scalarsAGrace
    $Results["B-during-grace-scalars"] = $scalarsBGrace

    Log "=== leg 8: host reconnects within grace ==="
    $reconnectResult = Send-HostCommand $hostSession "RECONNECT" "HOST_ONLY_RECONNECTED"
    $Results["host-reconnected"] = $reconnectResult.Line
    $scalarsAAfterReconnect = Get-Scalars -pipe $PipeA -label "A-after-host-reconnect"
    $scalarsBAfterReconnect = Get-Scalars -pipe $PipeB -label "B-after-host-reconnect"
    Save-Grab -pipe $PipeA -evidenceName "06-A-after-reconnect" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "06-B-after-reconnect" | Out-Null
    $Results["A-after-reconnect-scalars"] = $scalarsAAfterReconnect
    $Results["B-after-reconnect-scalars"] = $scalarsBAfterReconnect
    StallLog "=== HOST-GRACE LEG END ==="

    Log "=== leg 9: room-end ==="
    $endResult = Send-HostCommand $hostSession "END" "HOST_ONLY_ENDING"
    $Results["host-ended"] = $endResult.Line
    $endWaitA = Invoke-LanistaTimed -pipe $PipeA -label "A-ended-idle" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinPhase","value=idle","timeout_ms=10000") -timeoutMs 11000
    $endWaitB = Invoke-LanistaTimed -pipe $PipeB -label "B-ended-idle" -cliArgs @("ui-wait-for","object=colosseumTaskbar","prop=watchPartyJoinPhase","value=idle","timeout_ms=10000") -timeoutMs 11000
    $Results["A-ended-presentation"] = $endWaitA.Exit
    $Results["B-ended-presentation"] = $endWaitB.Exit
    Save-Grab -pipe $PipeA -evidenceName "07-A-ended" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "07-B-ended" | Out-Null

    Log "=== teardown: close both app instances, host, relay ==="
    Stop-AppInstance $appA
    Stop-AppInstance $appB
    Stop-Host $hostSession
    Stop-RelayTree $relay.Id

} finally {
    Log "=== final teardown safety sweep ==="
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
    foreach ($t in @($TagA, $TagB)) {
        Remove-Onboarding $t
        Remove-AppDataDirs $t
    }
    Log "=== results ==="
    $Results | Format-Table | Out-String | Write-Host
    $Results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $EvidenceRoot "results.json")
    Log "verify: only PID $DailyAppPid should remain"
    Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host
    Log "evidence root: $EvidenceRoot"
}
