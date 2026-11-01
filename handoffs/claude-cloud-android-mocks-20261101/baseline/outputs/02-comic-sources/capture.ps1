# Laptop 1 only. Isolated headless Chrome screenshots + mock geometry/interaction checks.
# All outputs remain under this task's captures directory.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$node = Get-Command node -ErrorAction Stop
& $node.Source (Join-Path $root 'verify-mocks.cjs')
if ($LASTEXITCODE -ne 0) { throw "Local mock verification failed with exit code $LASTEXITCODE" }
