# Vendors the exact Lucide SVG subset the Colosseum player renders, pinned to
# lucide-static@0.460.0 (Harbor's pinned lucide-react version). Run once, and again only when
# the player's icon set changes. Assets are committed to the repo and NEVER fetched at app runtime.
#
#   pwsh -File scripts/vendor_lucide_player_icons.ps1
#
# Mapped set = the 18 kinds the player actually instantiates (18th is the circle-alert fallback),
# not the whole Lucide library. Add a name below only when a real player action needs it.

$ErrorActionPreference = "Stop"

$root    = Split-Path -Parent $PSScriptRoot
$outDir  = Join-Path $root "assets/icons/lucide"
$pkg     = "lucide-static@0.460.0"
$version = "0.460.0"

$icons = @(
  'arrow-left','rotate-ccw','rotate-cw','skip-back','skip-forward','play','pause',
  'maximize','minimize','minus','x','volume-2','volume-x','audio-lines','captions',
  'gallery-horizontal-end','scan','circle-alert'
)

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("lucide_vendor_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
try {
    Write-Host "npm pack $pkg ..."
    $packJson = & npm pack $pkg --json --pack-destination $tmp 2>$null
    if ($LASTEXITCODE -ne 0) { throw "npm pack $pkg failed." }
    $meta      = ($packJson | ConvertFrom-Json)[0]
    $tgz       = Join-Path $tmp $meta.filename
    $integrity = $meta.integrity
    $tarball   = "https://registry.npmjs.org/lucide-static/-/lucide-static-$version.tgz"
    if (-not (Test-Path $tgz)) { throw "tarball not produced: $tgz" }

    # Extract with cwd=$tmp and a RELATIVE archive name: GNU tar (Git's) otherwise reads the
    # "C:" in an absolute path as a remote host ("Cannot connect to C:"). Relative name is
    # colon-free and works for both GNU tar and Windows bsdtar.
    Push-Location $tmp
    try { & tar -xf $meta.filename } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "tar extract failed (exit $LASTEXITCODE)." }
    $srcIcons = Join-Path $tmp "package/icons"

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    foreach ($name in $icons) {
        $src = Join-Path $srcIcons "$name.svg"
        if (-not (Test-Path $src)) { throw "icon '$name' is not in $pkg (package/icons)." }
        Copy-Item $src (Join-Path $outDir "$name.svg") -Force
    }
    Copy-Item (Join-Path $tmp "package/LICENSE") (Join-Path $outDir "LICENSE") -Force

    @"
package:   $pkg
version:   $version
tarball:   $tarball
integrity: $integrity
icons:     $($icons -join ', ')
note:      Vendored via scripts/vendor_lucide_player_icons.ps1 - do NOT hand-edit these SVGs.
"@ | Set-Content -Path (Join-Path $outDir "SOURCE.txt") -Encoding ascii

    Write-Host "Vendored $($icons.Count) Lucide icons + LICENSE into $outDir"
    Write-Host "integrity: $integrity"
}
finally {
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
