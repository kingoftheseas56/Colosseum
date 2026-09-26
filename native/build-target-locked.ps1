param(
    [Parameter(Mandatory = $true)][string]$BatchFile,
    [Parameter(Mandatory = $true)][string]$Target
)

if ($Target -notmatch '^[A-Za-z0-9_+.-]+$') {
    Write-Error 'Invalid CMake target.'
    exit 2
}

$mutex = [System.Threading.Mutex]::new($false, 'Local\ColosseumNativeBuildTarget')
$locked = $false
try {
    Write-Host 'Waiting for Colosseum build lock...'
    try {
        $locked = $mutex.WaitOne()
    } catch [System.Threading.AbandonedMutexException] {
        $locked = $true
    }
    Write-Host 'Colosseum build lock acquired.'
    $env:COLOSSEUM_BUILD_LOCK_HELD = '1'
    & $env:ComSpec /d /c ('"{0}" "{1}"' -f $BatchFile, $Target)
    exit $LASTEXITCODE
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
