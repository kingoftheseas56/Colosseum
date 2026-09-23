$ErrorActionPreference = "Stop"

$HarnessRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

Push-Location (Join-Path $HarnessRoot "agent_bridge")
try {
    uv run colosseum-desktop-bridge
} finally {
    Pop-Location
}
