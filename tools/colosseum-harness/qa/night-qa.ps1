# Night QA switch.  night-qa.ps1 start [-Until 07:30] [-MaxTours 20] | stop | status
# start: makes sure ChatGPT Lite runs in automation mode (CDP on 127.0.0.1:9333), then runs the
#        night loop hidden in the background. Lite is relaunched only if it lacks that port.
param(
    [Parameter(Position = 0)][ValidateSet('start', 'stop', 'status')][string]$Action = 'status',
    [string]$Until = '07:30',
    [int]$MaxTours = 20,
    [string]$LiteExe = (Join-Path $env:USERPROFILE 'Desktop\ChatGPT-WebView2\x64\Release\ChatGPT.exe')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$qaDir = Join-Path $repo 'artifacts\qa'
$pidFile = Join-Path $qaDir 'night-loop.pid'
$stopFile = Join-Path $qaDir 'STOP'
New-Item -ItemType Directory -Force $qaDir | Out-Null

function Test-Cdp { try { $null = Invoke-RestMethod 'http://127.0.0.1:9333/json/version' -TimeoutSec 3; $true } catch { $false } }
function Get-Loop {
    if (-not (Test-Path $pidFile)) { return $null }
    $p = Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq 'node') { return $p } else { return $null }
}

switch ($Action) {
    'start' {
        if (Get-Loop) { Write-Output 'Night QA loop is already running.'; break }
        if (-not (Test-Cdp)) {
            Get-Process ChatGPT -ErrorAction SilentlyContinue | Where-Object Path -eq $LiteExe |
                ForEach-Object { $_.CloseMainWindow() | Out-Null }
            Start-Sleep 5
            Get-Process ChatGPT -ErrorAction SilentlyContinue | Where-Object Path -eq $LiteExe | Stop-Process -Force
            Start-Process $LiteExe -ArgumentList '--playwright' -WorkingDirectory (Split-Path $LiteExe)
            for ($i = 0; $i -lt 30 -and -not (Test-Cdp); $i++) { Start-Sleep 2 }
            if (-not (Test-Cdp)) { throw 'ChatGPT Lite did not expose its automation port (9333).' }
            Start-Sleep 10
        }
        Remove-Item $stopFile -ErrorAction SilentlyContinue
        $out = Join-Path $qaDir 'night-loop.out.log'
        $p = Start-Process node -ArgumentList @((Join-Path $PSScriptRoot 'night-loop.mjs'), '--until', $Until, '--max-tours', $MaxTours) `
            -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError (Join-Path $qaDir 'night-loop.err.log') -PassThru
        Set-Content $pidFile $p.Id
        Write-Output "Night QA loop started (pid $($p.Id)); runs until $Until or $MaxTours tours. Log: $qaDir\night-log.jsonl"
    }
    'stop' {
        New-Item -ItemType File -Force $stopFile | Out-Null
        $p = Get-Loop
        if ($p) { Start-Sleep 2; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
        Remove-Item $pidFile -ErrorAction SilentlyContinue
        Write-Output 'Night QA loop stopped.'
    }
    'status' {
        $p = Get-Loop
        Write-Output ("Loop: " + $(if ($p) { "running (pid $($p.Id))" } else { 'not running' }))
        Write-Output ("Lite automation port: " + $(if (Test-Cdp) { 'up' } else { 'down' }))
        $log = Join-Path $qaDir 'night-log.jsonl'
        if (Test-Path $log) { Get-Content $log -Tail 8 }
    }
}
