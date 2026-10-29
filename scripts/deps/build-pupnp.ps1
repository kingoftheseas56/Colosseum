# build-pupnp.ps1 — reproducible pupnp (libupnp) prefix for Colosseum's DLNA MediaServer (Arc 71).
#
# Builds into one MSVC prefix ($Prefix), in dependency order, every step pinned to an exact
# upstream commit and verified before use:
#   1. pthreads4w 3.0.3.0  (Vollstrecker/pthreads4w @ $PthreadsCommit)
#        static + shared, C flavour, PTHREADS4W_BUILD_TESTING=OFF
#   2. pupnp 22.1.9        (pupnp/pupnp tag $PupnpRef = $PupnpCommit)
#        static only (libupnps.lib + libixmls.lib), webserver + device API on, IPv6 and the
#        client API off, tests/samples off, DOWNLOAD_AND_BUILD_DEPS=OFF (pthreads comes from
#        step 1 — nothing floats with a moving master).
#
# Any *.patch under scripts/deps/patches/pupnp/ is `git apply`d to the pupnp clone before it is
# configured (each is checked first; a patch that no longer applies fails the build loudly).
# Residents: 0001 the MSVC 64-bit-file-offset fix that makes >2 GiB vdir responses work;
# 0002 a per-write idle timeout for web-server bodies (UpnpSetWebServerWriteIdleTimeout) so a
# client that stops reading cannot pin a worker thread forever.
#
# Both libs build Release /MD, which is ABI-compatible with RelWithDebInfo consumers (same CRT).
#
# The script must run inside a VS x64 environment; when cl.exe is not on PATH it relaunches
# itself under vcvars64 (located via vswhere). No other machine state is assumed: cmake/ninja
# resolve from PATH first, then the Qt Tools fallbacks, then the parameters.
[CmdletBinding()]
param(
    [string]$Prefix = 'C:\tools\pupnp-22.1.9-msvc',
    [string]$WorkDir = "$env:TEMP\colosseum-pupnp-build",
    [string]$PupnpRepo = 'https://github.com/pupnp/pupnp.git',
    [string]$PupnpRef = 'release-22.1.9',
    [string]$PupnpCommit = 'ad9247b68c6a1185e3636add0bbdda7e1848553a',
    [string]$PthreadsRepo = 'https://github.com/Vollstrecker/pthreads4w.git',
    [string]$PthreadsCommit = '5c17342e83eead9e667b03c4d657e6e5c0abe547',
    [string]$CMake = '',
    [string]$Ninja = '',
    [string]$Configuration = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

# ── toolchain plumbing ────────────────────────────────────────────────────────────────────
function Resolve-Tool([string]$Name, [string]$Explicit, [string[]]$Fallbacks) {
    if ($Explicit -and (Test-Path $Explicit)) { return (Resolve-Path $Explicit).Path }
    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($f in $Fallbacks) {
        if ($f -and (Test-Path $f)) { return (Resolve-Path $f).Path }
    }
    throw "$Name not found: pass -$Name or add it to PATH. Tried fallbacks: $($Fallbacks -join ', ')"
}

# Re-launch this very script under vcvars64 when cl.exe is not already on PATH. Done at script
# scope on purpose: $PSBoundParameters inside a function would be that function's (empty) set.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "cl.exe not on PATH and vswhere.exe not found at $vswhere" }
    $vcvars = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find VC\Auxiliary\Build\vcvars64.bat | Select-Object -First 1
    if (-not $vcvars -or -not (Test-Path $vcvars)) { throw "vcvars64.bat not found via vswhere" }
    Write-Host "== relaunching under VS x64 environment ($vcvars)"
    # Re-pass exactly what the caller chose (defaults are not in PSBoundParameters); every
    # value is quoted so spaces survive the cmd -> powershell hop.
    $relaunch = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    foreach ($kv in $PSBoundParameters.GetEnumerator()) {
        if ($kv.Value -is [System.Management.Automation.SwitchParameter]) {
            if ($kv.Value) { $relaunch += "-$($kv.Key)" }
        } else {
            $relaunch += "-$($kv.Key)"; $relaunch += "`"$($kv.Value)`""
        }
    }
    cmd /s /c "`"$vcvars`" >nul && powershell $($relaunch -join ' ')"
    exit $LASTEXITCODE
}

function Run([string]$What, [string]$Exe, [string[]]$ArgList) {
    Write-Host "==> $What"
    Write-Host "    $Exe $($ArgList -join ' ')"
    & $Exe @ArgList | Out-Host   # Out-Host: keep tool output out of the caller's return value
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

# ── the build ─────────────────────────────────────────────────────────────────────────────
& {
    $cmake = Resolve-Tool 'cmake' $CMake @('C:\Qt\Tools\CMake_64\bin\cmake.exe')
    $ninja = Resolve-Tool 'ninja' $Ninja @('C:\Qt\Tools\Ninja\ninja.exe')
    Write-Host "== cmake: $cmake"
    Write-Host "== ninja: $ninja"
    Write-Host "== prefix: $Prefix"
    Write-Host "== workdir: $WorkDir"

    if ($Clean -and (Test-Path $WorkDir)) { Remove-Item -Recurse -Force $WorkDir }
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

    function Sync-GitClone([string]$Name, [string]$Repo, [string]$Commit, [string]$Ref) {
        $dir = Join-Path $WorkDir $Name
        if (-not (Test-Path (Join-Path $dir '.git'))) {
            Run "clone $Name" git @('-c', 'core.autocrlf=false', 'clone', $Repo, $dir)
        }
        Run "checkout $Name @ $Commit" git @('-C', $dir, 'checkout', $Commit)
        $head = (& git -C $dir 'rev-parse' 'HEAD').Trim()
        if ($head -ne $Commit) { throw "${Name}: HEAD $head != pinned $Commit" }
        $desc = (& git -C $dir 'describe' '--tags' '--always').Trim()
        Write-Host "== $Name at $head ($desc)"
        return $dir
    }

    $pthreadsSrc = Sync-GitClone 'pthreads4w' $PthreadsRepo $PthreadsCommit $null
    $pupnpSrc = Sync-GitClone 'pupnp' $PupnpRepo $PupnpCommit $PupnpRef

    # Resident patches (MSVC 64-bit file offsets &c). Applied to a pristine tree so the same
    # patch set is idempotent across rebuilds.
    $patchDir = Join-Path $PSScriptRoot 'patches\pupnp'
    if (Test-Path $patchDir) {
        Run 'reset pupnp worktree' git @('-C', $pupnpSrc, 'checkout', '--', '.')
        Get-ChildItem $patchDir -Filter '*.patch' | Sort-Object Name | ForEach-Object {
            Run "apply patch $($_.Name)" git @('-C', $pupnpSrc, 'apply', "--check", $_.FullName)
            Run "apply patch $($_.Name)" git @('-C', $pupnpSrc, 'apply', $_.FullName)
        }
    }

    # 1. pthreads4w -> $Prefix (pupnp's MSVC build find_package(PTHREADS4W CONFIG REQUIRED)s it).
    $ptBuild = Join-Path $WorkDir 'build-pthreads4w'
    Run 'configure pthreads4w' $cmake @(
        '-S', $pthreadsSrc, '-B', $ptBuild, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCMAKE_INSTALL_PREFIX=$Prefix", '-DPTHREADS4W_BUILD_TESTING=OFF')
    Run 'build pthreads4w' $cmake @('--build', $ptBuild, '--config', $Configuration)
    Run 'install pthreads4w' $cmake @('--install', $ptBuild, '--config', $Configuration)

    # 2. pupnp -> $Prefix. DOWNLOAD_AND_BUILD_DEPS=OFF: pthreads4w comes from the prefix, so
    #    nothing tracks a moving master.
    $upBuild = Join-Path $WorkDir 'build-pupnp'
    Run 'configure pupnp' $cmake @(
        '-S', $pupnpSrc, '-B', $upBuild, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCMAKE_INSTALL_PREFIX=$Prefix", "-DCMAKE_PREFIX_PATH=$Prefix",
        '-DUPNP_BUILD_SHARED=OFF', '-DUPNP_BUILD_STATIC=ON',
        '-DUPNP_BUILD_SAMPLES=OFF',
        '-DUPNP_ENABLE_TESTING=OFF', '-DUPNP_ENABLE_TESTING_INTEGRATION=OFF',
        '-DUPNP_ENABLE_IPV6=OFF',        # DLNA renderers are IPv4; drops the v6 failure class
        '-DUPNP_ENABLE_CLIENT_API=OFF',  # pure server
        '-DUPNP_ENABLE_OPEN_SSL=OFF',
        '-DUPNP_ADAPTER_UUID_NAME=ON',   # Windows: select the NIC by adapter GUID, not its (Unicode) friendly name
        '-DDOWNLOAD_AND_BUILD_DEPS=OFF')
    Run 'build pupnp' $cmake @('--build', $upBuild, '--config', $Configuration)
    Run 'install pupnp' $cmake @('--install', $upBuild, '--config', $Configuration)

    foreach ($must in @(
        "lib\cmake\UPNP\UPNPConfig.cmake",
        "lib\cmake\PTHREADS4W\PTHREADS4WConfig.cmake",
        "lib\libupnps.lib", "lib\libpthreadC3s.lib")) {
        if (-not (Test-Path (Join-Path $Prefix $must))) { throw "expected $Prefix\$must is missing" }
    }
    # ixml's static name differs by configure (libixmls.lib vs ixmls.lib); either is fine as
    # long as the installed UPNP package points at it.
    if (-not ((Test-Path (Join-Path $Prefix 'lib\libixmls.lib')) -or
              (Test-Path (Join-Path $Prefix 'lib\ixmls.lib')))) {
        throw "expected $Prefix\lib\libixmls.lib (or ixmls.lib) is missing"
    }
    Write-Host "== OK: pupnp prefix complete at $Prefix"
    exit 0
}
