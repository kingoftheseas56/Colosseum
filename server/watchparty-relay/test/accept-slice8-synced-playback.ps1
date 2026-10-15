#!/usr/bin/env pwsh
# Colosseum Watch Party relay - Slice 8 acceptance orchestrator.
#
# Proves TWO real colosseum.exe instances play the SAME torrent source in one
# room and stay locked: play/pause/seek, drift-bounded, Catch Up. Builds on
# Slice 7's accept-two-clients.ps1 primitives (relay/host/app-instance
# lifecycle, per-pipe CLI drive) and adds: Slice 8b's named entry affordances
# (taskbarExtensions/topBarSearch/extensionsPaneTab_*), a world-scoped handle
# resolver for topBarSearch's duplicate-objectName gap, per-instance Torrentio
# enablement (two layers - global + per-world ask-order), the search->series->
# sources-sheet drive, sourceRow infoHash matching, and the player-side
# watch-party scalar table (Slice 8's crown proof).
#
# Safety: never touches a PID this script did not launch itself, verified by
# command line first. RAM floor checked before the second instance and before
# playback (per-run threshold below).

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

$Results = [ordered]@{}
$LaunchedPids = New-Object System.Collections.Generic.List[int]
$KnownDailyPids = New-Object System.Collections.Generic.List[int]
$EvidenceRoot = Join-Path $RepoRoot "artifacts\watchparty-slice8\$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$StallLogPath = Join-Path $EvidenceRoot "stall-instrumentation.log"
"# Slice 8 stall instrumentation - per-call timestamps" | Set-Content $StallLogPath

function Log($msg) { $line = "[accept-slice8] $msg"; Write-Host $line }
function StallLog($msg) {
    $ts = (Get-Date).ToString("HH:mm:ss.fff")
    try { Add-Content -Path $StallLogPath -Value "$ts $msg" -ErrorAction Stop }
    catch { Start-Sleep -Milliseconds 50; try { Add-Content -Path $StallLogPath -Value "$ts $msg" -ErrorAction Stop } catch {} }
}

function Assert-NotKnownDailyApp([int]$procPid) {
    if ($KnownDailyPids -contains $procPid) {
        throw "REFUSING to touch PID $procPid - recorded as a pre-existing (daily) process at preflight. Abort."
    }
}

function Test-RamFloor([string]$label) {
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory  # KB
    $freeMb = [math]::Round($free / 1024)
    Log "RAM check ($label): ${freeMb}MB free"
    if ($freeMb -lt 700) {
        throw "RAM floor breached at '$label' (${freeMb}MB free < 700MB) - stopping cleanly per safety instruction."
    }
    return $freeMb
}

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

function Invoke-LanistaRetry([string]$pipe, [string]$label, [string[]]$cliArgs, [int]$timeoutMs = 8000, [int]$retries = 2) {
    for ($i = 0; $i -le $retries; $i++) {
        $r = Invoke-LanistaTimed -pipe $pipe -label "$label-try$i" -cliArgs $cliArgs -timeoutMs $timeoutMs
        if ($r.Exit -eq 0) { return $r }
        Log "retrying '$label' (attempt $i failed, exit=$($r.Exit))"
        Start-Sleep -Milliseconds 500
    }
    return $r
}

# --- Registry / AppData isolation helpers (Slice 6/7 pattern) ---------------

function Seed-Onboarding([string]$tag) {
    $regPath = "HKCU:\Software\Brotherhood\Colosseum-dltest-$tag\account"
    New-Item -Path $regPath -Force | Out-Null
    New-ItemProperty -Path $regPath -Name "localOnlyChosen" -PropertyType String -Value "true" -Force | Out-Null
    New-ItemProperty -Path $regPath -Name "onboardingCompleted" -PropertyType String -Value "true" -Force | Out-Null
    Log "seeded onboarding registry for tag=$tag"
}
function Remove-Onboarding([string]$tag) {
    $regKey = "HKCU:\Software\Brotherhood\Colosseum-dltest-$tag"
    if (Test-Path $regKey) { Remove-Item -Path $regKey -Recurse -Force; Log "removed onboarding registry for tag=$tag" }
}
function Remove-AppDataDirs([string]$tag) {
    $roaming = "$env:APPDATA\Brotherhood\Colosseum-dltest-$tag"
    $local   = "$env:LOCALAPPDATA\Brotherhood\Colosseum-dltest-$tag"
    foreach ($d in @($roaming, $local)) {
        if (-not (Test-Path $d)) { continue }
        # A just-killed process can hold a file handle (sqlite catalog, torrent
        # index) for a brief moment after taskkill returns - retry a few times
        # rather than fail teardown over a transient lock.
        $removed = $false
        for ($i = 0; $i -lt 4 -and -not $removed; $i++) {
            try { Remove-Item -Path $d -Recurse -Force -ErrorAction Stop; $removed = $true }
            catch { Start-Sleep -Milliseconds 750 }
        }
        if ($removed) { Log "removed AppData dir: $d" }
        else { Log "AppData dir still locked after retries, leaving for later cleanup: $d" }
    }
}

# --- Relay lifecycle ----------------------------------------------------------

function Start-Relay([string]$devAuth = "1", [int]$graceMs = 15000) {
    Log "starting wrangler dev --local-protocol https --port $RelayPort"
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
            $tcp.Close(); $ready = $true; break
        } catch { Start-Sleep -Milliseconds 300 }
    }
    if (-not $ready) { throw "wrangler dev did not become ready on port $RelayPort" }
    Log "wrangler dev ready, PID=$($proc.Id)"
    return $proc
}
function Stop-RelayTree([int]$rootPid) {
    Assert-NotKnownDailyApp $rootPid
    Log "killing wrangler dev tree rooted at PID $rootPid"
    try { & taskkill /PID $rootPid /T /F 2>&1 | Out-Null } catch {}
}

# --- Host (scripted, probe-live.mjs --host-only) -----------------------------

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
    $roomId = $null; $ready = $false
    $lines = New-Object System.Collections.Generic.List[string]
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        $line = $proc.StandardOutput.ReadLine()
        if ($null -eq $line) { break }
        $lines.Add($line); Log "host: $line"
        if ($line -match "^ROOM_ID=(.+)$") { $roomId = $Matches[1] }
        if ($line -eq "HOST_ONLY_READY") { $ready = $true }
    }
    if (-not $ready -or -not $roomId) { throw "host-only did not become ready. Lines: $($lines -join ' | ')" }
    Log "host ready, PID=$($proc.Id), ROOM_ID=$roomId"
    return @{ Proc = $proc; RoomId = $roomId }
}

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
        $seen.Add($line); Log "host: $line"
        if ($line.StartsWith($markerPrefix)) {
            $t1 = Get-Date
            $deltaMs = [math]::Round(($t1 - $t0).TotalMilliseconds)
            StallLog "AFTER  host-command cmd='$cmd' marker='$markerPrefix' deltaMs=$deltaMs"
            if ($deltaMs -gt 5000) { StallLog "STALL_FLAG host-command cmd='$cmd' deltaMs=$deltaMs exceeds 5000ms threshold" }
            return @{ Line = $line; Lines = $seen; DeltaMs = $deltaMs }
        }
    }
    throw "host command '$cmd' never produced marker '$markerPrefix'. Lines seen: $($seen -join ' | ')"
}
function Stop-Host($hostInfo) {
    if (-not $hostInfo.Proc.HasExited) {
        Assert-NotKnownDailyApp $hostInfo.Proc.Id
        try { $hostInfo.Proc.StandardInput.Close() } catch {}
        $deadline = (Get-Date).AddSeconds(5)
        while (-not $hostInfo.Proc.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
        if (-not $hostInfo.Proc.HasExited) { try { & taskkill /PID $hostInfo.Proc.Id /T /F 2>&1 | Out-Null } catch {} }
    }
}

# --- App instance lifecycle ---------------------------------------------------

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

    $ready = $false
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        $r = Invoke-LanistaTimed -pipe $pipe -label "readiness-ping" -cliArgs @("ping") -timeoutMs 3000
        if ($r.Exit -eq 0) { $ready = $true } else { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready) { throw "app instance (tag=$tag pipe=$pipe) never answered ping" }

    $stateOut = & $Lanista --pipe $pipe --timeout 5000 get-state 2>&1
    $stateJoined = ($stateOut -join "`n")
    if ($stateJoined -notmatch [regex]::Escape("Colosseum-dltest-$tag")) {
        Assert-NotKnownDailyApp $proc.Id
        try { & taskkill /PID $proc.Id /T /F 2>&1 | Out-Null } catch {}
        throw "isolation check FAILED for tag=$tag. Killed PID $($proc.Id)."
    }
    Log "app instance ready+isolated: tag=$tag pipe=$pipe PID=$($proc.Id)"

    Invoke-LanistaTimed -pipe $pipe -label "boot-splash-gone" -cliArgs @("ui-wait-for","object=bootSplash","prop=visible","value=false","timeout_ms=60000") -timeoutMs 61000 | Out-Null
    return @{ Proc = $proc; Pipe = $pipe; Tag = $tag }
}

function Stop-AppInstance($appInfo) {
    Assert-NotKnownDailyApp $appInfo.Proc.Id
    Log "graceful-closing app instance tag=$($appInfo.Tag) PID=$($appInfo.Proc.Id)"
    try {
        $wmiProc = Get-Process -Id $appInfo.Proc.Id -ErrorAction SilentlyContinue
        if ($wmiProc) { $wmiProc.CloseMainWindow() | Out-Null }
    } catch {}
    $closeDeadline = (Get-Date).AddSeconds(8)
    while (-not $appInfo.Proc.HasExited -and (Get-Date) -lt $closeDeadline) { Start-Sleep -Milliseconds 300 }
    if (-not $appInfo.Proc.HasExited) {
        Assert-NotKnownDailyApp $appInfo.Proc.Id
        & taskkill /PID $appInfo.Proc.Id /T /F 2>&1 | Out-Null
    }
    Log "app instance tag=$($appInfo.Tag) exited: $($appInfo.Proc.HasExited)"
}

# --- Drive: join flow (Slice 6/7 pattern) ------------------------------------

function Invoke-JoinDrive([string]$pipe, [string]$roomId, [string]$guestName, [string]$leg) {
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

function Save-Grab([string]$pipe, [string]$evidenceName, [string]$rootObj = "colosseumTaskbar") {
    $r = Invoke-LanistaTimed -pipe $pipe -label "grab-$evidenceName" -cliArgs @("get-state","--grab",$rootObj) -timeoutMs 10000
    $outPath = Join-Path $EvidenceRoot "$evidenceName.txt"
    $r.Out | Out-File -FilePath $outPath -Encoding utf8
    $joined = $r.Out -join "`n"
    try {
        $parsed = $joined | ConvertFrom-Json -ErrorAction Stop
        if ($parsed.grabPath -and (Test-Path $parsed.grabPath)) {
            $destPng = Join-Path $EvidenceRoot "$evidenceName.png"
            Copy-Item -Path $parsed.grabPath -Destination $destPng -Force
            Log "grab PNG copied: $destPng"
        }
    } catch { Log "grab PNG copy skipped for $evidenceName ($($_.Exception.Message))" }
    return $joined
}

# --- Handle resolution (world-scoped, for the duplicate-objectName gap) -----

function Resolve-HandleInRoot([string]$pipe, [string]$root, [string]$name, [int]$maxDepth = 14) {
    $r = Invoke-LanistaTimed -pipe $pipe -label "dumpui-$root-for-$name" -cliArgs @("dump-ui","root=$root","maxDepth=$maxDepth") -timeoutMs 10000
    $joined = ($r.Out -join "`n")
    $parsed = $joined | ConvertFrom-Json -ErrorAction Stop
    $match = $parsed.items | Where-Object { $_.objectName -eq $name } | Select-Object -First 1
    if (-not $match) { throw "Resolve-HandleInRoot: no item named '$name' under root='$root' (count=$($parsed.items.Count) truncated=$($parsed.truncated))" }
    return $match.handle
}

# GROUND-TRUTH FINDING (controller diagnostic, session 20260820-191520-a577b1cf,
# independently reproduced): plain-name resolution (findItem's first-DFS-match)
# is not safe for `extensionsPage` in this drive - after joining a room, more
# than one Loader-backed instance of a full-page surface can exist, and a
# name-only ui-wait-for/ui-click can silently answer against a HIDDEN one
# (matches the topBarSearch shadowing already found and fixed in Slice 8b).
# Resolve-VisibleHandle dumps the WHOLE tree (no root filter - the ambiguity
# is precisely about not knowing which root is real) and returns the handle
# of the first item with the given name whose own `visible` is true. Every
# subsequent step in this function resolves through that HANDLE, not the name,
# so a later stale/duplicate instance can never intercept a click that was
# meant for the real one.
function Resolve-VisibleHandle([string]$pipe, [string]$name, [string]$leg, [int]$maxDepth = 20) {
    $r = Invoke-LanistaTimed -pipe $pipe -label "$leg dumpui-visible-$name" -cliArgs @("dump-ui","maxDepth=$maxDepth") -timeoutMs 10000
    $parsed = ($r.Out -join "`n") | ConvertFrom-Json -ErrorAction Stop
    $match = $parsed.items | Where-Object { $_.objectName -eq $name -and $_.visible -eq $true } | Select-Object -First 1
    if (-not $match) { throw "Resolve-VisibleHandle: no VISIBLE item named '$name' (count=$($parsed.items.Count) truncated=$($parsed.truncated)) - the surface never actually opened, or opened hidden" }
    return $match.handle
}

# Click-and-verify: a bare ui-click reply proves the CALL succeeded, never
# that it did anything (exactly the vacuous-pass class the controller found).
# This clicks by name, then asserts the `checked` mirror (Slice 8b amendment)
# actually flipped - if it doesn't, the run aborts here with the real cause
# instead of surfacing as an unrelated rows-timeout three steps later.
function Click-ToggleAndVerify([string]$pipe, [string]$toggleName, [string]$leg) {
    Invoke-LanistaTimed -pipe $pipe -label "$leg toggle-visible-$toggleName" -cliArgs @("ui-wait-for","object=$toggleName","prop=visible","value=true","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-$toggleName" -cliArgs @("ui-click","target=$toggleName") | Out-Null
    $verify = Invoke-LanistaTimed -pipe $pipe -label "$leg verify-checked-$toggleName" -cliArgs @("ui-wait-for","object=$toggleName","prop=checked","value=true","timeout_ms=4000") -timeoutMs 5000
    if (($verify.Out -join "") -notmatch '"matched":\s*true') {
        throw "$leg`: clicked $toggleName but checked never reached true - enablement did not actually land (this is the exact silent-failure class the controller diagnosed)"
    }
    Log "$leg $toggleName confirmed checked=true"
}

# --- Extension enablement (per-instance sandbox; fresh tag = fresh state) ---

function Enable-TorrentioForInstance([string]$pipe, [string]$leg) {
    Invoke-LanistaTimed -pipe $pipe -label "$leg open-taskbar" -cliArgs @("ui-click","target=colosseumTaskbarHomeButton") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg dock-open" -cliArgs @("ui-wait-for","object=taskbarWatchPartyJoin","prop=width","value=46","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-extensions" -cliArgs @("ui-click","target=taskbarExtensions") | Out-Null
    # NOTE: an earlier version of this function used Resolve-VisibleHandle
    # (a full unscoped dump-ui) here to defend against the shadowing class
    # the controller diagnosed. Live-run finding: dump-ui's own byte budget
    # truncates a full-tree walk before reaching extensionsPage (count=238,
    # truncated=true, maxDepth=20) - a self-inflicted regression, LESS
    # reliable than the plain name wait it replaced (which worked cleanly in
    # every isolated diagnostic this session, including the join-first
    # sequence that reproduces the original conditions). Reverted to the
    # plain name wait; Click-ToggleAndVerify's `checked` assertion below is
    # the actual safety net the controller asked for - if THIS toggle click
    # silently misses its real target for any reason (shadowing or
    # otherwise), the checked==true assertion catches it and aborts here
    # with clear evidence, without needing a risky full-tree pre-resolution.
    Invoke-LanistaTimed -pipe $pipe -label "$leg extensions-page-open" -cliArgs @("ui-wait-for","object=extensionsPage","prop=visible","value=true","timeout_ms=6000") -timeoutMs 7000 | Out-Null

    # Layer 1: global install toggle (Installed pane).
    Invoke-LanistaTimed -pipe $pipe -label "$leg pane-installed" -cliArgs @("ui-click","target=extensionsPaneTab_installed") | Out-Null
    Click-ToggleAndVerify -pipe $pipe -toggleName "extensionToggle_com.stremio.torrentio.addon" -leg $leg

    # Layer 2: per-world (Theatre) ask-order toggle (Sources pane, the default).
    Invoke-LanistaTimed -pipe $pipe -label "$leg pane-sources" -cliArgs @("ui-click","target=extensionsPaneTab_sources") | Out-Null
    Click-ToggleAndVerify -pipe $pipe -toggleName "extensionSourceToggle_com.stremio.torrentio.addon" -leg $leg

    Log "Torrentio enabled AND VERIFIED (both layers) for $leg"
}

# --- Search -> series -> sources sheet drive ---------------------------------

function Drive-ToSourcesSheet([string]$pipe, [string]$leg, [string]$searchTerm, [string]$imdbId) {
    # Close extensions, return home, enter Theatre.
    Invoke-LanistaTimed -pipe $pipe -label "$leg extensions-back" -cliArgs @("ui-click","target=colosseumTaskbarHomeButton") | Out-Null
    Start-Sleep -Milliseconds 400
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-theatre-pill" -cliArgs @("ui-click","target=modePill_Theatre") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg theatre-world-visible" -cliArgs @("ui-wait-for","object=theatreWorld","prop=visible","value=true","timeout_ms=6000") -timeoutMs 7000 | Out-Null

    $searchHandle = Resolve-HandleInRoot -pipe $pipe -root "theatreWorld" -name "topBarSearch"
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-search-icon" -cliArgs @("ui-click","target=$searchHandle") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg search-input-visible" -cliArgs @("ui-wait-for","object=searchSurfaceInput","prop=visible","value=true","timeout_ms=6000") -timeoutMs 7000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg type-search" -cliArgs @("ui-text-input","target=searchSurfaceInput","text=$searchTerm") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg search-results-visible" -cliArgs @("ui-wait-for","object=searchSurfaceResults","prop=visible","value=true","timeout_ms=10000") -timeoutMs 11000 | Out-Null

    $resultName = "searchResult_$imdbId"
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-result" -cliArgs @("ui-click","target=$resultName") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg series-page-visible" -cliArgs @("ui-wait-for","object=theatreSeriesPage","prop=visible","value=true","timeout_ms=8000") -timeoutMs 9000 | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg click-watch" -cliArgs @("ui-click","target=theatreSeriesWatch") | Out-Null
    Invoke-LanistaTimed -pipe $pipe -label "$leg sources-sheet-visible" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=visible","value=true","timeout_ms=10000") -timeoutMs 11000 | Out-Null
    # sourcesSheet.loading gates row materialization (async multi-extension ask,
    # up to a 22s safety cutoff in the QML itself) - wait for loading==false
    # before scanning rows, or the row probe races an empty sheet.
    Invoke-LanistaTimed -pipe $pipe -label "$leg sources-sheet-loaded" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=loading","value=false","timeout_ms=23000") -timeoutMs 24000 | Out-Null
    Log "reached sourcesSheet for $leg (loading complete)"
}

# Reopen-retry wrapper: the sheet's own 22s internal Timer (SourcesSheet.qml)
# sets `timedOut=true, rows=[]` if the multi-extension async ask has not
# finished by then - observed live under this machine's two-instance/relay
# CPU+network load (a run with a quiet standalone instance completed inside
# the budget; three orchestrated runs in a row did not). `timedOut` resets
# to false at the SAME point `loading` is set true again (SourcesSheet.qml's
# open-trigger function), so closing (Escape) and reopening (re-click Watch)
# genuinely re-issues the ask rather than replaying a stale empty state.
function Assert-AskedCountPositive([string]$pipe, [string]$leg, [string]$context) {
    # HARDENED (controller order A): distinguishes "the multi-extension ask
    # never went out" (enablement upstream failed silently) from "the ask is
    # still running" or "it ran and returned nothing" - a bare rows-timeout
    # cannot tell these apart, and this arc already lost real time to that
    # exact ambiguity. askedCount==0 here means Torrentio (or nothing) was
    # ever actually asked - abort with THIS evidence, not a later rows fail.
    $zeroCheck = Invoke-LanistaTimed -pipe $pipe -label "$leg askedcount-check-$context" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=askedCount","value=0","timeout_ms=800") -timeoutMs 1800
    $isZero = (($zeroCheck.Out -join "") -match '"matched":\s*true')
    if ($isZero) {
        throw "$leg`: sourcesSheet.askedCount==0 at $context - the source ask never went out (enablement failure upstream, not a content/seed problem). Aborting with this evidence per controller order A."
    }
    Log "$leg sourcesSheet.askedCount confirmed nonzero at $context"
}

function Drive-ToSourcesSheetReady([string]$pipe, [string]$leg, [string]$searchTerm, [string]$imdbId, [int]$maxAttempts = 3) {
    Drive-ToSourcesSheet -pipe $pipe -leg $leg -searchTerm $searchTerm -imdbId $imdbId
    Assert-AskedCountPositive -pipe $pipe -leg $leg -context "initial-open"
    for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
        $timedOutR = Invoke-LanistaTimed -pipe $pipe -label "$leg timedout-check-$attempt" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=timedOut","value=true","timeout_ms=500") -timeoutMs 1500
        $timedOut = (($timedOutR.Out -join "") -match '"matched":\s*true')
        if (-not $timedOut) { Log "$leg sourcesSheet loaded within budget (attempt $attempt)"; return }
        Log "$leg sourcesSheet timedOut on attempt $attempt - closing and re-opening to retry the ask"
        Invoke-LanistaTimed -pipe $pipe -label "$leg retry-close-sheet" -cliArgs @("ui-keypress","key=Escape") -timeoutMs 5000 | Out-Null
        Invoke-LanistaTimed -pipe $pipe -label "$leg retry-sheet-gone" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=visible","value=false","timeout_ms=5000") -timeoutMs 6000 | Out-Null
        Invoke-LanistaTimed -pipe $pipe -label "$leg retry-click-watch" -cliArgs @("ui-click","target=theatreSeriesWatch") | Out-Null
        Invoke-LanistaTimed -pipe $pipe -label "$leg retry-sheet-visible" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=visible","value=true","timeout_ms=10000") -timeoutMs 11000 | Out-Null
        Invoke-LanistaTimed -pipe $pipe -label "$leg retry-sheet-loaded" -cliArgs @("ui-wait-for","object=sourcesSheet","prop=loading","value=false","timeout_ms=23000") -timeoutMs 24000 | Out-Null
        Assert-AskedCountPositive -pipe $pipe -leg $leg -context "retry-$attempt"
    }
    Log "$leg sourcesSheet still timedOut after $maxAttempts attempts - proceeding anyway, row match will fail honestly if truly empty"
}

function Find-MatchingSourceRow([string]$pipe, [string]$leg, [string[]]$candidateHashes, [int]$maxIndex = 30) {
    # GROUND-TRUTH FINDING (this run): the sheet's row set is not stable to
    # read slowly. A first standalone (quiet-machine) diagnostic found rows
    # instantly; three orchestrated runs in a row found the sheet reporting
    # `loading=false` but then EITHER zero rows (timedOut) OR the sheet
    # itself gone (NO_SUCH_ITEM) by the time a ~18s sequential probe loop
    # (2 candidates x 6 indices x 1.5s) finished reading it - i.e. whatever
    # is happening, reading it SLOWLY loses the race. Fix: one fast dump-ui
    # read of the row count immediately after `loading=false`, then short
    # (400ms) per-candidate equality probes only across rows confirmed to
    # exist - total probe time now ~2.5s worst case instead of ~18s.
    $countR = Invoke-LanistaTimed -pipe $pipe -label "$leg dumpui-sourcesSheet-rowcount" -cliArgs @("dump-ui","root=sourcesSheet","maxDepth=10") -timeoutMs 8000
    $rowCount = 0
    try {
        $parsed = ($countR.Out -join "`n") | ConvertFrom-Json -ErrorAction Stop
        $rowCount = ($parsed.items | Where-Object { $_.objectName -match '^sourceRow_\d+$' }).Count
        Log "$leg sourcesSheet row count (fast dump-ui): $rowCount"
    } catch { Log "$leg dump-ui row-count read failed: $($_.Exception.Message)" }
    $effectiveMax = if ($rowCount -gt 0) { $rowCount - 1 } else { $maxIndex }

    foreach ($idx in 0..$effectiveMax) {
        $rowName = "sourceRow_$idx"
        foreach ($hash in $candidateHashes) {
            $m = Invoke-LanistaTimed -pipe $pipe -label "$leg probe-$rowName-hash" -cliArgs @("ui-wait-for","object=$rowName","prop=automationInfoHash","value=$hash","timeout_ms=400") -timeoutMs 1400
            if (($m.Out -join "") -match '"matched":\s*true') {
                Log "$leg matched $rowName -> infoHash=$hash"
                return @{ RowName = $rowName; Hash = $hash }
            }
        }
    }
    return $null
}

# --- Player scalar table ------------------------------------------------------
#
# GROUND-TRUTH FINDING (this run, before driving two live instances): the
# plain per-pipe CLI cannot read an arbitrary numeric property value at all.
# `qml-get` requires `props` to be a JSON ARRAY (native/devtools/LanistaServer.cpp
# cmdQmlGet: `p.value("props").toArray()`), and payloadFromArgs (native/tools/
# lanista.cpp) never parses a k=v value as JSON - every value becomes a bare
# string/number/bool, so no array can ever reach the server through this CLI.
# `ui-wait-for` (LanistaServer::cmdUiWaitFor) is STRICT EQUALITY ONLY server-
# side (`QJsonValue::fromVariant(...) == wanted`) - there is no `<`/`>`/`>=`
# comparison operator on the wire at all (that operator set lives only in
# native/tools/lanista.cpp's `opMatches`, which is the CLIENT-side assertion
# engine for `session run` scenario files, a different code path that always
# launches its own fresh app instance and cannot attach to an already-running
# long-lived pipe).
#
# Every scalar below is therefore read through EQUALITY probes only:
#   - booleans/small enums: probe each candidate value directly (proven
#     pattern, same one Slice 6/7 used for watchPartyJoinPhase).
#   - decodedWidth (need only "nonzero", never an exact resolution): probe
#     value=0 - a WAIT_TIMEOUT (does NOT match 0) is read as "nonzero", which
#     is the only thing playbackStarted's own definition needs.
#   - watchPartyDriftSeconds (continuous, cannot be read at all): ground-
#     truthed native/watchparty/WatchPartyPlayerSync.h `kDriftToleranceMs =
#     1'000` (exactly the plan's "<1.0s" acceptance threshold) and .cpp's
#     reconcile(): `setSyncStatus(outOfSync ? Desynced : InSync)` where
#     `outOfSync = pauseMismatch || absoluteDriftMs > kDriftToleranceMs || ...`.
#     `watchPartySyncStatus == "inSync"` is therefore the EXACT boolean
#     proxy for "drift is under the plan's own 1.0s threshold" (a strict
#     superset check, since it also folds in pause-state agreement) - used
#     here instead of a raw number, which the tooling cannot produce.
#   - playbackPosition (continuous): same limitation: no raw read is
#     attempted; pause/seek/track-together assertions below use
#     watchPartySyncStatus for the same reason.

function Test-PlayerBool([string]$pipe, [string]$prop, [bool]$want, [int]$timeoutMs = 1500) {
    $r = Invoke-LanistaTimed -pipe $pipe -label "playerbool-$prop-$want" -cliArgs @("ui-wait-for","object=player","prop=$prop","value=$($want.ToString().ToLower())","timeout_ms=$timeoutMs") -timeoutMs ($timeoutMs + 1000)
    return (($r.Out -join "") -match '"matched":\s*true')
}

function Test-PlayerStringEquals([string]$pipe, [string]$prop, [string]$want, [int]$timeoutMs = 1500) {
    $r = Invoke-LanistaTimed -pipe $pipe -label "playerstr-$prop-$want" -cliArgs @("ui-wait-for","object=player","prop=$prop","value=$want","timeout_ms=$timeoutMs") -timeoutMs ($timeoutMs + 1000)
    return (($r.Out -join "") -match '"matched":\s*true')
}

# Enum reader: probes each candidate; returns the first that matches, or
# "(none-of: candidates)" if none did (never silently reports a wrong value).
function Get-PlayerEnum([string]$pipe, [string]$prop, [string[]]$candidates, [int]$timeoutMsEach = 800) {
    foreach ($c in $candidates) {
        if (Test-PlayerStringEquals -pipe $pipe -prop $prop -want $c -timeoutMs $timeoutMsEach) { return $c }
    }
    return "(none-of: $($candidates -join ','))"
}

function Get-PlayerScalarTable([string]$pipe, [string]$leg) {
    $row = [ordered]@{ leg = $leg; ts = (Get-Date).ToString("HH:mm:ss.fff") }
    $row["playbackStarted"]           = Test-PlayerBool -pipe $pipe -prop "playbackStarted" -want $true
    $row["decodedWidthNonzero"]       = -not (Test-PlayerStringEquals -pipe $pipe -prop "decodedWidth" -want "0")
    $row["watchPartySyncActive"]      = Test-PlayerBool -pipe $pipe -prop "watchPartySyncActive" -want $true
    $row["watchPartySyncStatus"]      = Get-PlayerEnum -pipe $pipe -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
    $row["watchPartyCatchUpAvailable"]= Test-PlayerBool -pipe $pipe -prop "watchPartyCatchUpAvailable" -want $true
    $row["watchPartyRoomActive"]      = Test-PlayerBool -pipe $pipe -prop "watchPartyRoomActive" -want $true
    $row["watchPartyUiPhase"]         = Get-PlayerEnum -pipe $pipe -prop "watchPartyUiPhase" -candidates @("active","synchronizing","connecting","idle","error","unavailable")
    $row["watchPartyLocalSyncStatus"] = Get-PlayerEnum -pipe $pipe -prop "watchPartyLocalSyncStatus" -candidates @("inSync","desynced","inactive")
    $row["watchPartySourceEligible"]  = Test-PlayerBool -pipe $pipe -prop "watchPartySourceEligible" -want $true
    $row["watchPartySourceMatchesRoom"] = Test-PlayerBool -pipe $pipe -prop "watchPartySourceMatchesRoom" -want $true
    Log "player scalars ($leg): $($row | ConvertTo-Json -Compress)"
    return $row
}

function Wait-PlaybackStarted([string]$pipe, [string]$leg, [int]$timeoutSec = 120) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $started = Test-PlayerBool -pipe $pipe -prop "playbackStarted" -want $true -timeoutMs 800
        if ($started) {
            $nonzero = -not (Test-PlayerStringEquals -pipe $pipe -prop "decodedWidth" -want "0" -timeoutMs 800)
            if ($nonzero) { Log "$leg playback started, decodedWidth nonzero"; return $true }
        }
        Start-Sleep -Seconds 2
    }
    Log "$leg playback NEVER started within ${timeoutSec}s"
    return $false
}

# =============================================================================
# MAIN
# =============================================================================

# Controller order B: Night of the Living Dead (1968), tt0063350 - public
# domain, 24 live Torrentio rows verified (curl + in-app diagnostic session,
# 2026-08-20). Row 0 (index 0 of 24 in-app, confirmed via a grab screenshot
# in the same diagnostic): "Night of the Living Dead 1968 2160p BluRay",
# 20 seeders, 4.35 GB, YTS, English, 4K/BluRay/HEVC - the exact well-seeded
# row the controller named. No smaller-file YTS row was confirmed seeded at
# diagnostic time; this is the controller-preapproved fallback.
$SearchTerm = "Night of the Living Dead"
$ImdbId = "tt0063350"
$CandidateHashes = @("1d71aeb11a149d7f2c2d5b5b05193cf80a5b927c")
$PipeA = "WP8A2"; $PipeB = "WP8B3"
$TagA = "wp-8a2"; $TagB = "wp-8b2"

try {
    Log "=== pre-flight: record any pre-existing colosseum.exe as untouchable ==="
    $preExisting = Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'"
    foreach ($p in $preExisting) { $KnownDailyPids.Add([int]$p.ProcessId) | Out-Null; Log "daily/pre-existing PID recorded: $($p.ProcessId) cmd=$($p.CommandLine)" }
    Test-RamFloor "preflight" | Out-Null

    Log "=== leg 0: live re-verify the Coffee Run Torrentio rows before locking the descriptor ==="
    $liveStreams = & curl.exe -sL "https://torrentio.strem.fun/stream/movie/$ImdbId.json"
    $Results["torrentio-live-check"] = $liveStreams
    $streamsParsed = $liveStreams | ConvertFrom-Json
    $liveHashes = $streamsParsed.streams | ForEach-Object { $_.infoHash }
    Log "live Torrentio rows for $ImdbId : $($liveHashes -join ', ')"
    $chosenHash = $CandidateHashes | Where-Object { $liveHashes -contains $_ } | Select-Object -First 1
    if (-not $chosenHash) {
        Log "neither pre-checked candidate is live; falling back to the first live row"
        $chosenHash = $liveHashes | Select-Object -First 1
    }
    if (-not $chosenHash) { throw "no live Torrentio rows for $ImdbId - cannot proceed" }
    Log "descriptor LOCKED: infoHash=$chosenHash fileIdx=0 (Coffee Run, $ImdbId)"
    $Results["chosen-infoHash"] = $chosenHash

    Log "=== leg 1: relay boot ==="
    $relay = Start-Relay -devAuth "1" -graceMs $HostGraceMs

    Log "=== leg 2: host creates the shared room with the locked descriptor ==="
    $hostSession = Start-Host -infoHash $chosenHash -fileIdx 0
    $roomId = $hostSession.RoomId

    Log "=== leg 3: instance A - launch, join, enable Torrentio, drive to sources sheet ==="
    $appA = Start-AppInstance -tag $TagA -pipe $PipeA
    Test-RamFloor "before-A-drive" | Out-Null
    $Results["A-joined"] = Invoke-JoinDrive -pipe $PipeA -roomId $roomId -guestName "GuestA" -leg "A"
    Enable-TorrentioForInstance -pipe $PipeA -leg "A"
    Drive-ToSourcesSheetReady -pipe $PipeA -leg "A" -searchTerm $SearchTerm -imdbId $ImdbId
    # Accept EITHER live candidate here, not only the pre-locked $chosenHash -
    # Torrentio is a live scraper and its row set/order was observed (this
    # session, two prior runs) to churn in the minutes between the curl
    # pre-check and the in-app sheet actually loading. If A's sheet shows a
    # DIFFERENT row than the room was created with, that is exactly the
    # documented fallback case ("if the exact official torrent does not
    # appear ... RE-BASE the room descriptor ... have the host re-create the
    # room with that descriptor BEFORE the play clicks") - implemented below.
    $matchA = Find-MatchingSourceRow -pipe $PipeA -leg "A" -candidateHashes $CandidateHashes
    if (-not $matchA) {
        # Diagnostic-on-failure: dump the sheet's actual structure before
        # throwing, so a failed run leaves real evidence instead of a blind
        # retry. Not committed to the repo (evidence dirs are gitignored).
        $dumpR = Invoke-LanistaTimed -pipe $PipeA -label "A-diag-dump-sourcesSheet" -cliArgs @("dump-ui","root=sourcesSheet","maxDepth=10") -timeoutMs 10000
        $dumpR.Out | Out-File -FilePath (Join-Path $EvidenceRoot "DIAG-A-sourcesSheet-dump.json") -Encoding utf8
        $sheetQuery = Invoke-LanistaTimed -pipe $PipeA -label "A-diag-query-sheet" -cliArgs @("ui-query","object=sourcesSheet") -timeoutMs 8000
        $sheetQuery.Out | Out-File -FilePath (Join-Path $EvidenceRoot "DIAG-A-sourcesSheet-query.json") -Encoding utf8
        throw "A: sourcesSheet showed none of the locked candidate infoHash(es) ($($CandidateHashes -join ', ')) - Verification failed (diagnostic dump saved)"
    }
    Save-Grab -pipe $PipeA -evidenceName "01-A-sources-sheet" -rootObj "sourcesSheet" | Out-Null

    if ($matchA.Hash -ne $chosenHash) {
        Log "REBASE: A's live sheet shows infoHash=$($matchA.Hash), room was created with $chosenHash - re-creating the room per the documented fallback"
        $Results["rebase-occurred"] = $true
        $Results["rebase-from"] = $chosenHash
        $Results["rebase-to"] = $matchA.Hash
        Send-HostCommand $hostSession "END" "HOST_ONLY_ENDING" | Out-Null
        Stop-Host $hostSession
        $chosenHash = $matchA.Hash
        $hostSession = Start-Host -infoHash $chosenHash -fileIdx 0
        $roomId = $hostSession.RoomId
        Log "rebased ROOM_ID=$roomId infoHash=$chosenHash"
        # A's room membership died with the old room (roomEnded) - rejoin
        # under the new roomId. A's sourcesSheet state is independent of
        # room membership (content browsing, not party state) and stays put.
        $Results["A-rejoined-after-rebase"] = Invoke-JoinDrive -pipe $PipeA -roomId $roomId -guestName "GuestA" -leg "A-rejoin"
    } else {
        $Results["rebase-occurred"] = $false
    }

    Log "=== RAM check before launching second instance ==="
    Test-RamFloor "before-B-launch" | Out-Null

    Log "=== leg 4: instance B - launch, join, enable Torrentio, drive to sources sheet ==="
    $appB = Start-AppInstance -tag $TagB -pipe $PipeB
    Test-RamFloor "before-B-drive" | Out-Null
    $Results["B-joined"] = Invoke-JoinDrive -pipe $PipeB -roomId $roomId -guestName "GuestB" -leg "B"
    Enable-TorrentioForInstance -pipe $PipeB -leg "B"
    Drive-ToSourcesSheetReady -pipe $PipeB -leg "B" -searchTerm $SearchTerm -imdbId $ImdbId
    # B must match the (possibly rebased) $chosenHash specifically - a match
    # on the OTHER candidate here would mean A and B are about to play
    # different torrents in the same "room", which is not a valid Slice 8
    # proof no matter how it is dressed up.
    $matchB = Find-MatchingSourceRow -pipe $PipeB -leg "B" -candidateHashes @($chosenHash)
    if (-not $matchB) { throw "B: sourcesSheet never showed the room's locked infoHash=$chosenHash - Verification failed (A and B would play different content)" }
    Save-Grab -pipe $PipeB -evidenceName "02-B-sources-sheet" -rootObj "sourcesSheet" | Out-Null

    Log "=== RAM check before starting playback on both ==="
    Test-RamFloor "before-playback" | Out-Null

    Log "=== leg 5: click the matched source row on both (start torrent playback) ==="
    Invoke-LanistaTimed -pipe $PipeA -label "A-click-source-row" -cliArgs @("ui-click","target=$($matchA.RowName)") | Out-Null
    Invoke-LanistaTimed -pipe $PipeB -label "B-click-source-row" -cliArgs @("ui-click","target=$($matchB.RowName)") | Out-Null

    $Results["A-playback-started"] = Wait-PlaybackStarted -pipe $PipeA -leg "A" -timeoutSec 120
    $Results["B-playback-started"] = Wait-PlaybackStarted -pipe $PipeB -leg "B" -timeoutSec 120
    if (-not $Results["A-playback-started"] -or -not $Results["B-playback-started"]) {
        throw "playback did not start on both instances within timeout - Verification failed"
    }
    Save-Grab -pipe $PipeA -evidenceName "03-A-both-playing" -rootObj "player" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "03-B-both-playing" -rootObj "player" | Out-Null

    Log "=== leg 6: sync scalars (eligibility/match/active/inSync) both apps ==="
    $Results["scalars-both-playing-A"] = Get-PlayerScalarTable -pipe $PipeA -leg "both-playing-A"
    $Results["scalars-both-playing-B"] = Get-PlayerScalarTable -pipe $PipeB -leg "both-playing-B"

    Log "=== leg 7: host PAUSE -> both pause ==="
    # No raw pause boolean is CLI-readable (mpv.pause has no mirrored root
    # property; see the scalar-table note above for why raw reads are limited
    # to equality probes). watchPartySyncStatus folds pauseMismatch into its
    # own inSync/desynced computation (WatchPartyPlayerSync.cpp reconcile()),
    # so both apps reporting inSync after a host PAUSE is the honest available
    # proxy that both actually paused together, not a fabricated pause read.
    Send-HostCommand $hostSession "PAUSE" "HOST_ONLY_PAUSING" | Out-Null
    Start-Sleep -Seconds 3
    $Results["A-syncStatus-after-pause"] = Get-PlayerEnum -pipe $PipeA -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
    $Results["B-syncStatus-after-pause"] = Get-PlayerEnum -pipe $PipeB -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
    Log "post-pause syncStatus: A=$($Results['A-syncStatus-after-pause']) B=$($Results['B-syncStatus-after-pause'])"
    Save-Grab -pipe $PipeA -evidenceName "04-A-both-paused" -rootObj "player" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "04-B-both-paused" -rootObj "player" | Out-Null

    Log "=== leg 8: host SEEK +300s ==="
    Send-HostCommand $hostSession "SEEK 300000" "HOST_ONLY_SEEKING" | Out-Null
    Start-Sleep -Seconds 4
    $Results["A-syncStatus-after-seek"] = Get-PlayerEnum -pipe $PipeA -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
    $Results["B-syncStatus-after-seek"] = Get-PlayerEnum -pipe $PipeB -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
    $Results["post-seek-both-inSync"] = ($Results["A-syncStatus-after-seek"] -eq "inSync") -and ($Results["B-syncStatus-after-seek"] -eq "inSync")
    Log "post-seek syncStatus: A=$($Results['A-syncStatus-after-seek']) B=$($Results['B-syncStatus-after-seek']) bothInSync=$($Results['post-seek-both-inSync'])"
    Save-Grab -pipe $PipeA -evidenceName "05-A-post-seek" -rootObj "player" | Out-Null
    Save-Grab -pipe $PipeB -evidenceName "05-B-post-seek" -rootObj "player" | Out-Null

    Log "=== leg 9: host PLAY (resume) ==="
    Send-HostCommand $hostSession "PLAY" "HOST_ONLY_PLAYING" | Out-Null
    Start-Sleep -Seconds 2

    Log "=== leg 10: drift table - 3 samples >=5s apart (watchPartySyncStatus proxy; see scalar-table note) ==="
    $driftTable = @()
    for ($s = 0; $s -lt 3; $s++) {
        $dA = Get-PlayerEnum -pipe $PipeA -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
        $dB = Get-PlayerEnum -pipe $PipeB -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
        $sample = [ordered]@{ sample = $s; ts = (Get-Date).ToString("HH:mm:ss.fff"); syncStatusA = $dA; syncStatusB = $dB }
        $driftTable += $sample
        Log "drift sample $s (syncStatus proxy, threshold=kDriftToleranceMs=1000ms) : A=$dA B=$dB"
        if ($s -lt 2) { Start-Sleep -Seconds 5 }
    }
    $Results["drift-table"] = $driftTable
    $Results["drift-all-under-1s"] = -not ($driftTable | Where-Object { $_.syncStatusA -ne "inSync" -or $_.syncStatusB -ne "inSync" })

    Log "=== leg 11: catch-up - force B behind via its own local UI seek back, then Catch Up ==="
    Invoke-LanistaTimed -pipe $PipeB -label "B-open-watchparty-panel" -cliArgs @("ui-click","target=watchPartyPlayerControl") | Out-Null
    Invoke-LanistaTimed -pipe $PipeB -label "B-panel-open-wait" -cliArgs @("ui-wait-for","object=watchPartyPanel","prop=visible","value=true","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    # B seeks itself backward locally (a guest user-seek under Host Control routes
    # to sync as a REQUEST that does not move the room - WatchPartyPlayerSync.cpp's
    # reconcile() drives drift/catchUpAvailable off the authoritative timeline vs
    # B's own local position, so a local-only seek is exactly the smallest real
    # drift this design permits without a second host action).
    Invoke-LanistaTimed -pipe $PipeB -label "B-local-seek-back" -cliArgs @("ui-keypress","key=Left") -timeoutMs 5000 | Out-Null
    Invoke-LanistaTimed -pipe $PipeB -label "B-local-seek-back-2" -cliArgs @("ui-keypress","key=Left") -timeoutMs 5000 | Out-Null
    Start-Sleep -Seconds 3
    $Results["B-catchUpAvailable-after-local-seek"] = Test-PlayerBool -pipe $PipeB -prop "watchPartyCatchUpAvailable" -want $true
    Log "B watchPartyCatchUpAvailable after local seek-back: $($Results['B-catchUpAvailable-after-local-seek'])"
    if ($Results["B-catchUpAvailable-after-local-seek"] -eq $true) {
        Invoke-LanistaTimed -pipe $PipeB -label "B-click-catchup" -cliArgs @("ui-click","target=watchPartyCatchUp") | Out-Null
        Start-Sleep -Seconds 3
        $Results["B-syncStatus-after-catchup"] = Get-PlayerEnum -pipe $PipeB -prop "watchPartySyncStatus" -candidates @("inSync","desynced","inactive")
        Log "B syncStatus after Catch Up: $($Results['B-syncStatus-after-catchup'])"
    } else {
        Log "catchUpAvailable never went true after B's local seek-back - recording honestly, not fabricating a catch-up leg"
        $Results["B-syncStatus-after-catchup"] = "(catch-up leg did not trigger - see note)"
    }
    Save-Grab -pipe $PipeB -evidenceName "06-B-catchup" -rootObj "player" | Out-Null

    Log "=== leg 12: chat visibility (panel open, host CHAT, verify in-panel) ==="
    Invoke-LanistaTimed -pipe $PipeA -label "A-open-watchparty-panel" -cliArgs @("ui-click","target=watchPartyPlayerControl") | Out-Null
    Invoke-LanistaTimed -pipe $PipeA -label "A-panel-open-wait" -cliArgs @("ui-wait-for","object=watchPartyPanel","prop=visible","value=true","timeout_ms=5000") -timeoutMs 6000 | Out-Null
    Send-HostCommand $hostSession "CHAT hello from the room" "HOST_ONLY_CHAT_SENDING" | Out-Null
    Start-Sleep -Seconds 2
    Save-Grab -pipe $PipeA -evidenceName "07-A-chat-visible" -rootObj "watchPartyChatViewport" | Out-Null
    $chatQuery = Invoke-LanistaTimed -pipe $PipeA -label "A-chat-query" -cliArgs @("ui-query","object=watchPartyChatViewport") -timeoutMs 6000
    $Results["chat-panel-query"] = ($chatQuery.Out -join "`n")

    Log "=== teardown: close both instances, host, relay ==="
    Stop-AppInstance $appA
    Stop-AppInstance $appB
    Stop-Host $hostSession
    Stop-RelayTree $relay.Id
    $Results["overall"] = "COMPLETED"

} catch {
    $Results["overall"] = "FAILED: $($_.Exception.Message)"
    Log "FATAL: $($_.Exception.Message)"
} finally {
    Log "=== final teardown safety sweep ==="
    foreach ($p in $LaunchedPids) {
        Assert-NotKnownDailyApp $p
        try {
            $proc = Get-Process -Id $p -ErrorAction SilentlyContinue
            if ($proc -and -not $proc.HasExited) { Log "teardown: killing leftover launched PID $p"; & taskkill /PID $p /T /F 2>&1 | Out-Null }
        } catch {}
    }
    foreach ($t in @($TagA, $TagB)) { Remove-Onboarding $t; Remove-AppDataDirs $t }
    Log "=== results ==="
    $Results | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $EvidenceRoot "results.json")
    Log "verify: only pre-existing PIDs should remain"
    Get-CimInstance Win32_Process -Filter "Name='colosseum.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host
    Get-CimInstance Win32_Process -Filter "Name='wrangler.exe' or Name='workerd.exe'" | Select-Object ProcessId, CommandLine | Format-Table | Out-String | Write-Host
    Log "evidence root: $EvidenceRoot"
}
