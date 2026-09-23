# Shared controlled-peer startup checks for the K10 real-wire runners.
# Dot-source this file; every helper throws so a runner fails at the cause
# instead of reporting a later readiness timeout.

function Start-ControlledPeer {
    param([string[]]$Arguments, [string]$Log)
    Start-Process -FilePath python -ArgumentList $Arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardError ($Log + '.stderr.txt')
}

function Wait-ControlledPeers {
    param([string]$Case, [object[]]$Peers, [string[]]$Logs, [int]$Seconds = 5)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ($true) {
        $listening = 0
        for ($index = 0; $index -lt $Logs.Count; ++$index) {
            $log = $Logs[$index]
            if ((Test-Path -LiteralPath $log) -and (Select-String -Quiet -SimpleMatch 'LISTEN ' $log)) {
                ++$listening
                continue
            }
            $peer = $Peers[$index]
            if ($peer.HasExited) {
                $reason = ''
                if (Test-Path -LiteralPath $log) {
                    $reason = (@(Select-String -LiteralPath $log -Pattern '^BIND_FAILED ') | ForEach-Object Line) -join '; '
                }
                if (-not $reason -and (Test-Path -LiteralPath ($log + '.stderr.txt'))) {
                    $reason = ((Get-Content -LiteralPath ($log + '.stderr.txt') -Tail 3) -join ' ').Trim()
                }
                throw "$Case controlled peer exited before LISTEN (exit=$($peer.ExitCode)): $reason"
            }
        }
        if ($listening -eq $Logs.Count) { return }
        if ((Get-Date) -ge $deadline) {
            throw "$Case controlled peer did not LISTEN within $Seconds s ($listening/$($Logs.Count) listening)"
        }
        Start-Sleep -Milliseconds 20
    }
}

function Assert-NoUtpDial {
    param([string]$Case, [string[]]$Logs)
    foreach ($log in $Logs) {
        $datagrams = @(Select-String -LiteralPath $log -Pattern '^UDP_DATAGRAM ').Count
        if ($datagrams -ne 0) {
            throw "$Case transport dialed a controlled peer over uTP ($datagrams UDP datagrams in $(Split-Path -Leaf $log)); the source swarm dials TCP only"
        }
    }
}

function Get-ControlledPeerPort {
    param([string]$Log)
    $line = @(Select-String -LiteralPath $Log -Pattern '^LISTEN port=(\d+)$')[0]
    [int]$line.Matches[0].Groups[1].Value
}
