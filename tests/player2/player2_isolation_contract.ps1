$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$nativeRoot = Join-Path $repoRoot 'native'
$nativeCMakePath = Join-Path $nativeRoot 'CMakeLists.txt'
$player2CMakePath = Join-Path $nativeRoot 'player2\CMakeLists.txt'
$buildMarkerPath = Join-Path $nativeRoot 'player2\core\Player2BuildMarker.h'
$offBuild = Join-Path $nativeRoot 'build-player2-contract-off'
$onBuild = Join-Path $nativeRoot 'build-player2'

function Resolve-BuildTool([string]$Name, [string]$QtFallback) {
    if (Test-Path $QtFallback) {
        return $QtFallback
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "contract setup failure: cannot locate $Name or $QtFallback"
}

function Import-MsvcEnvironment {
    $vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "contract setup failure: cannot locate $vcvars"
    }

    $command = 'call "' + $vcvars + '" >nul && set'
    $environment = & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "contract setup failure: vcvars64.bat exited with $LASTEXITCODE"
    }
    foreach ($line in $environment) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

function Require-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "contract failure: $Message"
    }
}

function Reject-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "contract failure: $Message"
    }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Program @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    if ($exitCode -ne 0) {
        throw "$Program failed with exit code ${exitCode}:`n$($output -join "`n")"
    }
    return ($output -join "`n")
}

$nativeCMake = Get-Content -Raw $nativeCMakePath
if ($nativeCMake -notmatch 'option\(COLOSSEUM_BUILD_PLAYER2') {
    throw 'COLOSSEUM_BUILD_PLAYER2 option is missing'
}

if (-not (Test-Path $player2CMakePath)) {
    throw 'contract failure: native/player2/CMakeLists.txt is missing'
}
if (-not (Test-Path $buildMarkerPath)) {
    throw 'contract failure: Player2BuildMarker.h is missing'
}

$player2CMake = Get-Content -Raw $player2CMakePath
Require-Text $nativeCMake 'option\(COLOSSEUM_BUILD_PLAYER2\s+"Build the isolated Player 2 laboratory"\s+OFF\)' 'Player 2 must default OFF'
Require-Text $nativeCMake 'if\(COLOSSEUM_BUILD_PLAYER2\)\s*add_subdirectory\(player2\)\s*endif\(\)' 'Player 2 subdirectory must be conditional'
Require-Text $player2CMake 'add_library\(player2_core\s+STATIC' 'player2_core must be a static library'
Require-Text $player2CMake 'add_executable\(player2_harness' 'player2_harness must be an executable target'
Require-Text $player2CMake 'add_custom_target\(player2_unit_tests' 'player2_unit_tests umbrella target is required'
Require-Text $player2CMake 'find_package\(Qt6\s+REQUIRED\s+COMPONENTS\s+Core\s+Gui\s+Quick\s+Qml\s+Network\s+Test\)' 'Player 2 must declare its Qt boundary'
Require-Text $player2CMake 'FFMPEG_ROOT' 'Player 2 must require an FFmpeg development root'
Reject-Text $player2CMake 'MpvQt|libmpv|mpv\.lib|Qt6::OpenGL' 'Player 2 targets must not depend on mpvqt, libmpv, or OpenGL'

$cmakeExe = Resolve-BuildTool 'cmake' 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ninjaExe = Resolve-BuildTool 'ninja' 'C:\Qt\Tools\Ninja\ninja.exe'
Import-MsvcEnvironment
$commonConfigure = @(
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninjaExe",
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64'
)

$offConfigure = Invoke-Checked $cmakeExe (@('-S', $nativeRoot, '-B', $offBuild) + $commonConfigure + @('-DCOLOSSEUM_BUILD_PLAYER2=OFF'))
$offTargets = Invoke-Checked $cmakeExe @('--build', $offBuild, '--target', 'help')
Reject-Text $offTargets 'player2_core|player2_harness|player2_unit_tests' 'Player 2 targets must not exist when the option is OFF'

$onConfigure = Invoke-Checked $cmakeExe (@('-S', $nativeRoot, '-B', $onBuild) + $commonConfigure + @('-DCOLOSSEUM_BUILD_PLAYER2=ON'))
$onTargets = Invoke-Checked $cmakeExe @('--build', $onBuild, '--target', 'help')
Require-Text $onTargets 'player2_core' 'player2_core target must exist when the option is ON'
Require-Text $onTargets 'player2_harness' 'player2_harness target must exist when the option is ON'
Require-Text $onTargets 'player2_unit_tests' 'player2_unit_tests target must exist when the option is ON'

$productionGraph = Invoke-Checked $ninjaExe @('-C', $onBuild, '-t', 'graph', 'colosseum')
Reject-Text $productionGraph 'player2_core|player2_harness|player2_unit_tests|qml[/\\]player2' 'production target graph must not contain Player 2'

$buildNinja = Get-Content -Raw (Join-Path $onBuild 'build.ninja')
Reject-Text $buildNinja 'build colosseum(?:\.exe)?:[^\r\n]*(?:player2_core|player2_harness|player2_unit_tests)' 'production target must not directly depend on Player 2'

Write-Output 'player2_isolation_contract: PASS'
