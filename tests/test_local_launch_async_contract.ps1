$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\native\engine\LocalLaunch.cpp') -Raw
$header = Get-Content (Join-Path $PSScriptRoot '..\native\engine\LocalLaunch.h') -Raw
$main = Get-Content (Join-Path $PSScriptRoot '..\qml\Main.qml') -Raw

if ($source -notmatch 'QtConcurrent::run\(\[paths, cancel\]') {
    throw 'LocalLaunch async validation must use QtConcurrent::run.'
}
if ($source -notmatch 'generation != m_routeGeneration') {
    throw 'LocalLaunch async completion must drop stale generations.'
}
if ($source -notmatch 'm_routeCancel->storeRelaxed\(1\)') {
    throw 'LocalLaunch async replacement must cancel the prior token.'
}
if ($source -notmatch 'if \(pathsOrUrls\.isEmpty\(\)\) \{\s*invalidateAsyncGeneration\(\)') {
    throw 'An empty open request must invalidate an older completion.'
}
if ($source -notmatch 'if \(index < 0 \|\| index >= m_nextToOpen\.size\(\)\) \{\s*invalidateAsyncGeneration\(\)') {
    throw 'An invalid staged index must invalidate an older completion.'
}
if ($source -notmatch 'if \(index == m_pendingStagedIndex\) \{\s*invalidateAsyncGeneration\(\)') {
    throw 'Removing a selected staged entry must invalidate its completion.'
}
if ($source -notmatch 'Identity is a GUI-owned state machine') {
    throw 'Identity observation must remain explicitly owner-thread work.'
}
foreach ($method in @('openAsync', 'routeInfoAsync', 'openNextToOpenAsync')) {
    if ($header -notmatch "Q_INVOKABLE void $method") {
        throw "$method must be exposed as a queued QML seam."
    }
}
if ($main -match 'dispatchLocalRoute\(LocalLaunch\.(open|routeInfo|openNextToOpen)\(') {
    throw 'Main.qml must not synchronously invoke a blocking LocalLaunch route path.'
}
Write-Output 'LOCAL_LAUNCH_ASYNC_CONTRACT_OK'
