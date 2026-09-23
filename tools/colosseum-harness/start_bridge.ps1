$ErrorActionPreference = "Stop"

$HarnessRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ColosseumRoot = if ($env:COLOSSEUM_ROOT) {
    $env:COLOSSEUM_ROOT
} else {
    (Resolve-Path (Join-Path $HarnessRoot "..\..")).Path
}
$MapPath = Join-Path $HarnessRoot "intelligence\colosseum-map.json"
$CliPath = Join-Path $HarnessRoot "run.py"

$prefix = @(
    "python",
    $CliPath,
    "--root",
    $ColosseumRoot,
    "--map",
    $MapPath
)

$env:COLOSSEUM_HARNESS_COMMAND_JSON = $prefix | ConvertTo-Json -Compress
if (-not $env:COLOSSEUM_HARNESS_TIMEOUT_SECONDS) {
    $env:COLOSSEUM_HARNESS_TIMEOUT_SECONDS = "120"
}

Push-Location (Join-Path $HarnessRoot "agent_bridge")
try {
    uv run colosseum-agent-bridge
} finally {
    Pop-Location
}
