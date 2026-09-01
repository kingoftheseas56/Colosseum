$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pagePath = Join-Path $root 'qml\GalaxyUniversePage.qml'
$systemPath = Join-Path $root 'qml\StarWarsGalaxySystem.qml'
$page = Get-Content $pagePath -Raw
$system = Get-Content $systemPath -Raw
function Need([string]$text,[string]$needle,[string]$message) {
    if ($text -notlike "*$needle*") { throw $message }
}
Need $system 'cameraYaw: -0.42' 'Galactic System must retain approved camera yaw.'
Need $system 'cameraPitch: 0.73' 'Galactic System must retain approved camera pitch.'
Need $system 'function projectPoint' 'Planets must be camera-projected, not pinned to screen percentages.'
Need $system 'viewportScale' 'Galactic System must scale to the actual viewport.'
Need $system 'property bool placeLeft: gate.p.x < sun.p.x' 'Planet labels must face away from the Skywalker star.'
Need $system 'running: false' 'Automatic orbit drift must stay disabled to protect readable spacing.'
Need $system 'visible: gate.modelData.node !== true' 'Normal era destinations must render as planets.'
Need $system 'visible: gate.modelData.node === true' 'Diamond chassis must be restricted to non-planet nodes.'
Need $system 'signal skywalkerActivated()' 'Skywalker star must expose a dedicated activation seam.'
Need $system 'root.skywalkerActivated()' 'Skywalker star click/keyboard activation must open its own destination.'
Need $page 'id:"skywalker"' 'Galaxy page must define the Skywalker Saga destination.'
Need $page 'skywalker-saga-screen' 'Skywalker destination must use the dedicated Episodes I-IX payload section.'
Need $system 'x: Math.min(-bodyPad, labelRow.x - 8)' 'Planet hitboxes must follow the rendered label side.'
Need $page 'orbit:170' 'High Republic must use the approved inner orbit.'
Need $page 'orbit:700' 'Beyond Skywalker must use the approved outer orbit.'
Need $page 'marks/high-republic.png' 'High Republic era mark must be present.'
Need $page 'marks/republic.svg' 'Fall of the Jedi era mark must be present.'
Need $page 'marks/empire.svg' 'Empire era mark must be present.'
Need $page 'marks/rebel.svg' 'Rebellion era mark must be present.'
Need $page 'marks/new-republic.png' 'New Republic era mark must be present.'
Need $page 'marks/first-order.svg' 'First Order era mark must be present.'
Need $page 'marks/new-jedi-order.png' 'Beyond Skywalker era mark must be present.'
Need $page 'radius:820, angle:2.72, y:30' 'Across the Eras must clear the title at laptop viewports.'
$payload = Get-Content (Join-Path $root 'assets\universes\star-wars.json') -Raw | ConvertFrom-Json
$saga = @($payload.universe.sections | Where-Object { $_.id -eq 'skywalker-saga-screen' })
if ($saga.Count -ne 1) { throw 'Skywalker Saga must have exactly one dedicated payload section.' }
$actualSagaIds = @($saga[0].entries | ForEach-Object { $_.id })
$expectedSagaIds = @('tt0120915','tt0121765','tt0121766','tt0076759','tt0080684','tt0086190','tt2488496','tt2527336','tt2527338')
if (($actualSagaIds -join ',') -ne ($expectedSagaIds -join ',')) { throw 'Skywalker Saga must remain Episodes I-IX in order.' }
if ($page -like '*x:.43, y:.50*') { throw 'Temporary fixed-screen planet coordinates must not return.' }
Write-Host 'STAR_WARS_SYSTEM_PARITY_OK'
