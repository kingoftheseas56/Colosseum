[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot 'runtime'),
    [string]$HostPython = '',
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Python 3.14.8 x64 embeddable ZIP, verified against python.org's release page.
# Update both the URL and digest together when intentionally moving Python.
$pythonVersion = '3.14.8'
$pythonArchive = "python-$pythonVersion-embed-amd64.zip"
$pythonUrl = "https://www.python.org/ftp/python/$pythonVersion/$pythonArchive"
$pythonSha256 = 'a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310'
$pythonVersionCode = '314'
$requiredPackageNames = @(
    'certifi', 'charset-normalizer', 'idna', 'requests', 'urllib3', 'yt-dlp', 'ytmusicapi'
)
$requiredDirectPins = @{
    'ytmusicapi' = '1.12.3'
    'yt-dlp' = '2026.8.19'
}

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit code $LASTEXITCODE)."
    }
}

$destinationFull = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationFull) {
    throw "Runtime destination already exists; refusing to overwrite it: $destinationFull"
}
$destinationParent = [System.IO.Path]::GetDirectoryName($destinationFull)
if ([string]::IsNullOrWhiteSpace($destinationParent)) {
    throw "Runtime destination must have a parent directory: $destinationFull"
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The bundled runtime is Windows x64 only.'
}

if ([string]::IsNullOrWhiteSpace($HostPython)) {
    $hostPythonCommand = Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $HostPython = $hostPythonCommand.Source
}
$hostPythonFull = [System.IO.Path]::GetFullPath($HostPython)
if (-not (Test-Path -LiteralPath $hostPythonFull -PathType Leaf)) {
    throw "Host Python was not found at the requested path: $hostPythonFull"
}
$requirementsPath = Join-Path $PSScriptRoot 'requirements.txt'
$runtimeLockPath = Join-Path $PSScriptRoot 'runtime-requirements.lock'
if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) {
    throw "The pinned Vinyl requirements file is missing: $requirementsPath"
}
if (-not (Test-Path -LiteralPath $runtimeLockPath -PathType Leaf)) {
    throw "The reviewed Vinyl wheel lock is missing: $runtimeLockPath"
}

$directPins = @{}
foreach ($line in (Get-Content -LiteralPath $requirementsPath)) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.TrimStart().StartsWith('#')) {
        continue
    }
    if ($line -notmatch '^\s*([A-Za-z0-9_.-]+)==([^\s;#]+)\s*(?:#.*)?$') {
        throw "Every Vinyl direct dependency must use an exact name==version pin: $line"
    }
    $name = [regex]::Replace($Matches[1].ToLowerInvariant(), '[-_.]+', '-')
    if ($directPins.ContainsKey($name)) {
        throw "Duplicate direct Vinyl dependency: $name"
    }
    $directPins[$name] = $Matches[2]
}
if ($directPins.Count -ne $requiredDirectPins.Count) {
    throw 'The direct Vinyl dependency set changed; review and update the runtime lock and pin contract together.'
}
foreach ($name in $requiredDirectPins.Keys) {
    if (-not $directPins.ContainsKey($name) -or $directPins[$name] -ne $requiredDirectPins[$name]) {
        throw "Expected scripts/vinyl/requirements.txt to pin $name==$($requiredDirectPins[$name])."
    }
}

$lockEntries = @()
$lockByName = @{}
foreach ($line in (Get-Content -LiteralPath $runtimeLockPath)) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.TrimStart().StartsWith('#')) {
        continue
    }
    if ($line -notmatch '^\s*([A-Za-z0-9_.-]+)==([^\s;#]+)\s+--hash=sha256:([0-9a-fA-F]{64})\s*$') {
        throw "Runtime lock entries must pin one exact version and SHA-256: $line"
    }
    $name = [regex]::Replace($Matches[1].ToLowerInvariant(), '[-_.]+', '-')
    if ($lockByName.ContainsKey($name)) {
        throw "Duplicate package in the Vinyl runtime lock: $name"
    }
    $entry = [ordered]@{
        name = $name
        version = $Matches[2]
        sha256 = $Matches[3].ToLowerInvariant()
    }
    $lockByName[$name] = $entry
    $lockEntries += $entry
}
$expectedNames = @($requiredPackageNames | Sort-Object)
$actualNames = @($lockByName.Keys | Sort-Object)
$packageNameDrift = @(Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames).Count
if ($lockEntries.Count -ne $expectedNames.Count -or $packageNameDrift -ne 0) {
    throw 'The Vinyl runtime lock package set drifted; update the reviewed seven-wheel closure deliberately.'
}
foreach ($name in $requiredDirectPins.Keys) {
    if (-not $lockByName.ContainsKey($name) -or $lockByName[$name].version -ne $requiredDirectPins[$name]) {
        throw "The runtime lock disagrees with the direct pin $name==$($requiredDirectPins[$name])."
    }
}
if ($ValidateOnly) {
    $lockHash = (Get-FileHash -LiteralPath $runtimeLockPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "Runtime lock validated: $($lockEntries.Count) exact wheel entries; SHA-256 $lockHash"
    return
}

if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
}
$stageName = '.vinyl-runtime-stage-' + [Guid]::NewGuid().ToString('N')
$stage = Join-Path $destinationParent $stageName
if (Test-Path -LiteralPath $stage) {
    throw "Generated staging path unexpectedly exists: $stage"
}
$payload = Join-Path $stage 'payload'
$wheelhouse = Join-Path $stage 'wheelhouse'
$archivePath = Join-Path $stage $pythonArchive
$resolvedLockPath = Join-Path $stage 'requirements.lock.txt'

try {
    New-Item -ItemType Directory -Path $payload -Force | Out-Null

    Invoke-WebRequest -Uri $pythonUrl -OutFile $archivePath
    $actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $pythonSha256) {
        throw "python.org embeddable archive SHA-256 mismatch: expected $pythonSha256, got $actualSha256"
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $payload

    $runtimePython = Join-Path $payload 'python.exe'
    $pythonDll = Join-Path $payload "python$pythonVersionCode.dll"
    $pythonZip = Join-Path $payload "python$pythonVersionCode.zip"
    $pthPath = Join-Path $payload "python$pythonVersionCode._pth"
    foreach ($requiredFile in @($runtimePython, $pythonDll, $pythonZip, $pthPath)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "The official embeddable archive is missing a required file: $requiredFile"
        }
    }

    # Download exactly the reviewed lock entries. --no-deps prevents a fresh
    # transitive resolution, and --require-hashes validates every wheel byte.
    New-Item -ItemType Directory -Path $wheelhouse -Force | Out-Null
    Invoke-CheckedNative -Executable $hostPythonFull -Arguments @(
        '-m', 'pip', 'download', '--disable-pip-version-check', '--no-deps',
        '--require-hashes', '--only-binary=:all:',
        '--platform', 'win_amd64', '--python-version', $pythonVersion,
        '--implementation', 'cp', '--abi', 'cp314',
        '--dest', $wheelhouse, '-r', $runtimeLockPath
    ) -FailureMessage 'Locked Vinyl wheel download failed'

    $wheelFiles = @(Get-ChildItem -LiteralPath $wheelhouse -File)
    if ($wheelFiles.Count -ne $lockEntries.Count -or
        @($wheelFiles | Where-Object { $_.Extension -ne '.whl' }).Count -ne 0) {
        throw "Locked download produced $($wheelFiles.Count) wheels for $($lockEntries.Count) lock entries."
    }
    $resolvedPackages = @()
    $claimedWheels = @{}
    foreach ($entry in $lockEntries) {
        $wheelNamePattern = $entry.name.Replace('-', '[-_]')
        $wheelPattern = '^' + $wheelNamePattern + '-' + [regex]::Escape($entry.version) + '[-_].+\.whl$'
        $matchingWheels = @($wheelFiles | Where-Object { $_.Name -match $wheelPattern })
        if ($matchingWheels.Count -ne 1) {
            throw "Locked package $($entry.name)==$($entry.version) did not produce exactly one wheel."
        }
        $wheel = $matchingWheels[0]
        if ($claimedWheels.ContainsKey($wheel.Name)) {
            throw "One downloaded wheel matched more than one lock entry: $($wheel.Name)"
        }
        $claimedWheels[$wheel.Name] = $true
        $wheelSha256 = (Get-FileHash -LiteralPath $wheel.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($wheelSha256 -ne $entry.sha256) {
            throw "Wheel hash mismatch for $($entry.name)==$($entry.version): expected $($entry.sha256), got $wheelSha256"
        }
        $resolvedPackages += [ordered]@{
            name = $entry.name
            version = $entry.version
            wheel = $wheel.Name
            sha256 = $entry.sha256
        }
    }
    $resolvedPackages = @($resolvedPackages | Sort-Object name)
    Copy-Item -LiteralPath $runtimeLockPath -Destination $resolvedLockPath

    $sitePackages = Join-Path $payload 'Lib\site-packages'
    New-Item -ItemType Directory -Path $sitePackages -Force | Out-Null
    Invoke-CheckedNative -Executable $hostPythonFull -Arguments @(
        '-m', 'pip', 'install', '--disable-pip-version-check', '--no-index',
        '--find-links', $wheelhouse, '--no-deps',
        '--no-warn-script-location', '--no-compile', '--require-hashes',
        '--only-binary=:all:', '--platform', 'win_amd64',
        '--python-version', $pythonVersion, '--implementation', 'cp', '--abi', 'cp314',
        '--target', $sitePackages, '-r', $runtimeLockPath
    ) -FailureMessage 'Hashed Vinyl wheel installation failed'

    $pthContent = @(
        "python$pythonVersionCode.zip"
        '.'
        'Lib\site-packages'
        'import site'
    )
    Set-Content -LiteralPath $pthPath -Value $pthContent -Encoding Ascii

    $smoke = @'
import importlib.metadata as metadata
import ytmusicapi
import yt_dlp
assert metadata.version("ytmusicapi") == "1.12.3"
assert metadata.version("yt-dlp") == "2026.8.19"
print("VINYL_RUNTIME_OK", metadata.version("ytmusicapi"), metadata.version("yt-dlp"))
'@
    Push-Location ([System.IO.Path]::GetTempPath())
    try {
        $smokeOutput = & $runtimePython -I -c $smoke 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "The isolated runtime import smoke failed: $($smokeOutput -join [Environment]::NewLine)"
        }
        if (($smokeOutput -join "`n") -notmatch 'VINYL_RUNTIME_OK 1\.12\.3 2026\.8\.19') {
            throw "The runtime smoke did not report the expected package pins: $($smokeOutput -join [Environment]::NewLine)"
        }
    }
    finally {
        Pop-Location
    }

    $hostVersion = (& $hostPythonFull -c 'import platform; print(platform.python_version())' | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not read the host Python version used for dependency preparation.'
    }
    $pipVersionOutput = (& $hostPythonFull -m pip --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $pipVersionOutput -notmatch '\bpip\s+(\S+)') {
        throw 'Could not read the host pip version used for dependency preparation.'
    }
    $pipVersion = $Matches[1]

    $provenance = [ordered]@{
        schemaVersion = 1
        preparedAtUtc = [DateTime]::UtcNow.ToString('o')
        runtime = [ordered]@{
            version = $pythonVersion
            architecture = 'win-amd64'
            source = $pythonUrl
            sha256 = $pythonSha256
        }
        installer = [ordered]@{
            hostPythonVersion = $hostVersion
            pipVersion = $pipVersion
            target = "CPython $pythonVersion / win_amd64 / cp314"
            dependencyResolution = 'disabled; runtime-requirements.lock only'
        }
        directRequirements = @($requiredDirectPins.GetEnumerator() | Sort-Object { $_.Key } | ForEach-Object {
            '{0}=={1}' -f $_.Key, $_.Value
        })
        lockFile = 'requirements.lock.txt'
        lockSha256 = (Get-FileHash -LiteralPath $runtimeLockPath -Algorithm SHA256).Hash.ToLowerInvariant()
        resolvedPackageCount = $resolvedPackages.Count
        resolvedPackages = $resolvedPackages
        isolatedImportSmoke = 'passed with python.exe -I from the system temporary directory'
    }
    $provenancePath = Join-Path $payload 'runtime-provenance.json'
    Set-Content -LiteralPath $provenancePath -Value ($provenance | ConvertTo-Json -Depth 8) -Encoding Utf8

    # Recheck immediately before publishing. The destination is never merged or overwritten.
    if (Test-Path -LiteralPath $destinationFull) {
        throw "Runtime destination appeared during preparation; refusing to overwrite it: $destinationFull"
    }
    Move-Item -LiteralPath $payload -Destination $destinationFull
    Write-Output "Prepared app-owned Vinyl runtime at $destinationFull"
    Write-Output "Python $pythonVersion (win-amd64), archive SHA-256 $pythonSha256"
    Write-Output "Pinned wheels: $($resolvedPackages.Count); isolated imports passed."
}
finally {
    if ((Test-Path -LiteralPath $stage -PathType Container) -and
        ([System.IO.Path]::GetFullPath($stage).StartsWith($destinationParent + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) -and
        ([System.IO.Path]::GetFileName($stage) -eq $stageName) -and
        $stageName.StartsWith('.vinyl-runtime-stage-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
