# Deterministic local HTTP media server for Player 2 streaming tests. It serves a real media file
# over HTTP/1.1 with honest byte-range support so the harness can open --url and prove the real
# QtHttpTransport + custom AVIO path end to end, with no internet dependency. Scenario modes let a
# human reproduce the range-rejection, slow-chunk and mid-stream-disconnect cases the hermetic
# player2_http_media_test covers with a fake transport.
#
#   Modes:
#     range     (default) full 206/Accept-Ranges support
#     norange   ignore Range, always serve 200 with no Accept-Ranges (unseekable)
#     slow      serve the body in small throttled chunks (forces Buffering)
#     dropfirst disconnect the first connection mid-body; later ranged reconnects succeed
#
#   Example:
#     powershell -NoProfile -File tests/player2/player2_http_fixture_server.ps1 `
#       -File "C:\path\to\clip.mp4" -Port 8791
#   then, in another shell:
#     player2_harness.exe --url http://localhost:8791/media --report stream.json --soak-seconds 5

param(
    [Parameter(Mandatory = $true)][string]$File,
    [int]$Port = 8791,
    [ValidateSet('range', 'norange', 'slow', 'dropfirst')][string]$Mode = 'range'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $File)) { throw "media file not found: $File" }
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $File))
$total = $bytes.Length

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
Write-Host "player2_http_fixture_server: serving $File ($total bytes) at http://localhost:$Port/media [mode=$Mode]"
Write-Host "player2_http_fixture_server: GET /quit to stop"

$connectionCount = 0
try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        if ($request.Url.AbsolutePath -eq '/quit') {
            $response.StatusCode = 200
            $response.Close()
            break
        }
        $connectionCount++

        # Resolve the requested byte range.
        $start = 0
        $end = $total - 1
        $hasRange = $false
        $rangeHeader = $request.Headers['Range']
        if ($Mode -ne 'norange' -and $rangeHeader -match 'bytes=(\d*)-(\d*)') {
            $hasRange = $true
            if ($Matches[1] -ne '') { $start = [int]$Matches[1] }
            if ($Matches[2] -ne '') { $end = [int]$Matches[2] }
        }
        if ($start -lt 0) { $start = 0 }
        if ($end -ge $total) { $end = $total - 1 }
        $count = $end - $start + 1

        if ($Mode -eq 'norange') {
            $response.StatusCode = 200
            $start = 0; $end = $total - 1; $count = $total
        } elseif ($hasRange) {
            $response.StatusCode = 206
            $response.AddHeader('Content-Range', "bytes $start-$end/$total")
            $response.AddHeader('Accept-Ranges', 'bytes')
        } else {
            $response.StatusCode = 200
            $response.AddHeader('Accept-Ranges', 'bytes')
        }
        $response.ContentType = 'video/mp4'
        $response.ContentLength64 = $count

        try {
            $stream = $response.OutputStream
            if ($Mode -eq 'dropfirst' -and $connectionCount -eq 1) {
                # Send only part of the body then abort so the client must reconnect.
                $half = [Math]::Max(1, [int]($count / 2))
                $stream.Write($bytes, $start, $half)
                $stream.Flush()
                $context.Response.Abort()
                Write-Host "player2_http_fixture_server: dropped first connection after $half bytes"
                continue
            }
            $chunk = if ($Mode -eq 'slow') { 32 * 1024 } else { 256 * 1024 }
            $offset = $start
            $remaining = $count
            while ($remaining -gt 0) {
                $take = [Math]::Min($chunk, $remaining)
                $stream.Write($bytes, $offset, $take)
                $stream.Flush()
                $offset += $take
                $remaining -= $take
                if ($Mode -eq 'slow') { Start-Sleep -Milliseconds 120 }
            }
            $response.Close()
        } catch {
            # A client abort (seek/cancel) is expected; keep serving.
            try { $context.Response.Abort() } catch { }
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
    Write-Host 'player2_http_fixture_server: stopped'
}
