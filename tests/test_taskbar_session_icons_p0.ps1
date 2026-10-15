# Taskbar session circles P0 (Hemanth 2026-07-18): the three surface sessions show
# his SVG icons in Windows-style circles — no world names on the bar.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bar = Get-Content (Join-Path $root 'qml/Taskbar.qml') -Raw

# the three icons exist on disk with a CONCRETE stroke (currentColor renders black in Qt SVG)
foreach ($icon in @('comic-book.svg', 'book-library.svg', 'projector-theatre.svg')) {
    $p = Join-Path $root "assets/icons/$icon"
    if (!(Test-Path $p)) { throw "missing session icon $icon" }
    $svg = Get-Content $p -Raw
    if ($svg -match 'currentColor') { throw "$icon still uses currentColor - Qt renders that black on the dark bar" }
}

# the mapping covers all three worlds' surfaces
if ($bar -notmatch 'tankoban.*comic-book\.svg') { throw 'tankoban sessions lost the comic-book icon' }
if ($bar -notmatch 'biblio.*book-library\.svg') { throw 'biblio sessions lost the book-library icon' }
if ($bar -notmatch 'theatre.*projector-theatre\.svg') { throw 'theatre sessions lost the projector icon' }

# circles, not name pills: no title Text left in the tile delegate; radius = half of 46
$delegate = [regex]::Match($bar, 'model:\s*bar\.groups[\s\S]*?HoverHandler \{ id: tileHover \}').Value
if (-not $delegate) { throw 'could not locate the session tile delegate' }
if ($delegate -match 'modelData\.title') { throw 'session tiles still print world names - the ask was icon circles' }
if ($delegate -notmatch 'radius: 23') { throw 'session tiles are no longer circles (radius 23 on the 46px tile)' }

# a stack still tells you it is a stack, and the fan (with full titles) still opens it
if ($delegate -notmatch 'sessions \|\| \[\]\)\.length') { throw 'the multi-session count chip is gone' }
if ($bar -notmatch 'fan\.openFor\(tile') { throw 'stacked circles no longer fan out' }

Write-Host 'test_taskbar_session_icons_p0: PASS (three icons on disk, mapped, circular, stack-aware)'
