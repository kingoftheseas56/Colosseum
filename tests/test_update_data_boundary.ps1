$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$native = Join-Path $root 'native'
$pathTokens = 'applicationDirPath\s*\(|QDir::current(Path)?\s*\(|QCoreApplication::applicationDirPath'
$writeTokens = 'QSaveFile|QFile::open|QFile\s+\w+.*WriteOnly|QDir::(rename|mkpath)|QSettings|\.write\s*\(|QFile::(copy|remove)|QFileInfo::setFile'
$violations = @()

$sourceFiles = @(rg --files $native -g '*.cpp' -g '*.h' -g '!build-msvc/**' -g '!build*/**')
if ($LASTEXITCODE -ne 0) {
    throw 'Could not enumerate production C++ sources'
}

foreach ($sourcePath in $sourceFiles) {
    $file = Get-Item -LiteralPath $sourcePath
    $lines = Get-Content -LiteralPath $file.FullName
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -match $pathTokens -and $line -match $writeTokens) {
            $violations += '{0}:{1}: writable operation is coupled to an install-relative/current path: {2}' -f `
                $file.FullName.Substring($root.Length + 1), ($index + 1), $line.Trim()
        }
    }
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Host "FAIL: $_" }
    exit 1
}

Write-Host 'UPDATE_DATA_BOUNDARY_OK'
