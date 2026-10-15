$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$lanista = Join-Path $root "native\build-msvc\lanista.exe"
$colosseum = Join-Path $root "native\build-msvc\colosseum.exe"
$ffmpeg = Join-Path $root "native\build-msvc\tools\ffmpeg.exe"
$outDir = Join-Path $root "artifacts\reddit-captures"
$captureImports = Join-Path $PSScriptRoot "qmlimports"
if (Test-Path $captureImports) {
    $env:QML_IMPORT_PATH = if ($env:QML_IMPORT_PATH) { "$captureImports;$($env:QML_IMPORT_PATH)" } else { $captureImports }
    $env:QML2_IMPORT_PATH = if ($env:QML2_IMPORT_PATH) { "$captureImports;$($env:QML2_IMPORT_PATH)" } else { $captureImports }
}

foreach ($required in @($lanista, $colosseum, $ffmpeg)) {
    if (!(Test-Path $required)) { throw "required capture binary not found: $required" }
}
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Get-ChildItem $outDir -File -ErrorAction SilentlyContinue | Remove-Item -Force

function Run-Capture([string]$scenarioName, [string]$seed = "", [string]$qml = "qml/Main.qml") {
    $scenario = (Resolve-Path (Join-Path $PSScriptRoot $scenarioName)).Path
    $qmlPath = (Resolve-Path (Join-Path $root $qml)).Path
    $args = @("session", "run", $scenario, "--exe", $colosseum,
              "--qml", $qmlPath, "--drive", "--ready-ms", "60000")
    if ($seed) {
        $seedPath = (Resolve-Path (Join-Path $root $seed)).Path
        $args += @("--seed", $seedPath)
    }
    Write-Host "`n=== $scenarioName ==="
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        Push-Location $root
        try {
            & $lanista @args
            $code = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($code -eq 0) { return }
        if ($code -ne 4 -or $attempt -eq 3) {
            throw "$scenarioName failed with exit $code"
        }
        Write-Host "INFRA RETRY $attempt/2 for $scenarioName after exit 4"
        Start-Sleep -Seconds 4
    }}


# Comic covers are cached only for the presentation harness. They are not source fixtures.
$comicCoverDir = Join-Path $PSScriptRoot "assets\comic-covers"
New-Item -ItemType Directory -Force -Path $comicCoverDir | Out-Null
$comicCovers = @(
    @("invincible.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication124/v4/7c/73/8d/7c738d92-dea3-7901-7ef8-962db5966470/Invincible_Compendium01.jpg/2000x2000bb.jpg"),
    @("sandman.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication122/v4/98/e0/6f/98e06ff4-2cdc-f5f2-4fde-b3a7417c2a49/T2187500018301.jpg/2000x2000bb.jpg"),
    @("saga.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication4/v4/23/03/9c/23039c5b-155e-0ae4-36b3-d3407b07420c/AUG120491.jpg/2000x2000bb.jpg"),
    @("watchmen.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication113/v4/5e/70/d9/5e70d95a-55be-e162-7149-31305a31ed87/T2013000018301.jpg/2000x2000bb.jpg"),
    @("sincity.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication125/v4/f6/d3/9f/f6d39f2f-1ccc-605c-93ea-8a9638cbec05/9781506722894.d.jpg/2000x2000bb.jpg"),
    @("hellboy.jpg", "https://is1-ssl.mzstatic.com/image/thumb/Publication118/v4/e2/87/bd/e287bd85-1ddc-1f35-4fb1-0ba4f4634717/9781506706870.jpg/2000x2000bb.jpg")
)
foreach ($cover in $comicCovers) {
    $dest = Join-Path $comicCoverDir $cover[0]
    if (!(Test-Path $dest) -or (Get-Item $dest).Length -lt 1000) {
        Invoke-WebRequest -Uri $cover[1] -OutFile $dest -UseBasicParsing -TimeoutSec 30
    }
}
# Manga covers are cached only for the presentation harness. They mirror Catalog.js sources.
$mangaCoverDir = Join-Path $PSScriptRoot "assets\manga-covers"
New-Item -ItemType Directory -Force -Path $mangaCoverDir | Out-Null
$mangaCovers = @(
    @("onepiece.jpg", "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30013-BeslEMqiPhlk.jpg"),
    @("berserk.jpg", "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30002-Cul4OeN7bYtn.jpg"),
    @("vinland.jpg", "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30642-0mjRDkf4THpo.jpg"),
    @("vagabond.png", "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx30656-9mW113O7rDnA.png"),
    @("chainsaw.png", "https://s4.anilist.co/file/anilistcdn/media/manga/cover/large/bx105778-euxXZEIfDY2u.png"),
    @("monster.jpg", "https://uploads.mangadex.org/covers/d9e30523-9d65-469e-92a2-302995770950/a397b3d3-d7b3-413f-8d6a-f2b136a4b4e2.jpg.512.jpg")
)
foreach ($cover in $mangaCovers) {
    $dest = Join-Path $mangaCoverDir $cover[0]
    if (!(Test-Path $dest) -or (Get-Item $dest).Length -lt 1000) {
        Invoke-WebRequest -Uri $cover[1] -OutFile $dest -UseBasicParsing -TimeoutSec 30
    }
}
# Presentation-only media seeds. These never modify the regression fixtures.
$readerSeed = Join-Path $PSScriptRoot "seeds\tankoban-reader"
$theatrePlayerSeed = Join-Path $root "artifacts\reddit-capture-seeds\theatre-player"
$theatreRecentDir = Join-Path $theatrePlayerSeed "vault"
$theatreVideo = Join-Path $PSScriptRoot "assets\Colosseum.mp4"
New-Item -ItemType Directory -Force -Path $theatreRecentDir | Out-Null
$recent = @{ items = @(@{
    path = $theatreVideo.Replace('\', '/')
    title = "Colosseum Theatre Demo"
    kind = "video"
    vaultId = "vault:reddit-theatre-demo"
}) }
[IO.File]::WriteAllText((Join-Path $theatreRecentDir "open-recent.json"),
    ($recent | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Run-Capture "home.json"
Run-Capture "tankoban_manga.json" "" "tests\lanista_capture\MangaCapture.qml"
Run-Capture "tankoban_comics.json" "" "tests\lanista_capture\ComicsCapture.qml"
Run-Capture "tankoban_reader.json" "tests\lanista_capture\seeds\tankoban-reader"
Run-Capture "biblio.json"
Run-Capture "biblio_reader.json" "" "tests\lanista_capture\BiblioReaderCapture.qml"
Run-Capture "theatre.json" "" "tests\lanista_capture\TheatreCatalogCapture.qml"
Run-Capture "theatre_player.json" "artifacts\reddit-capture-seeds\theatre-player"
Run-Capture "downloads.json" "tests\lanista_fixtures\journeys\open-manga-v1"
Run-Capture "vault.json" "tests\lanista_fixtures\journeys\vault-browse-five-shapes-v1"
Run-Capture "account.json" "" "tests\lanista_capture\AccountCenterCapture.qml"

$mp4 = @(Get-ChildItem $outDir -Filter *.mp4 -File)
$gif = @(Get-ChildItem $outDir -Filter *.gif -File)
if ($mp4.Count -ne 11 -or $gif.Count -ne 11) {
    throw "expected 11 MP4 + 11 GIF outputs; got $($mp4.Count) MP4 + $($gif.Count) GIF"
}
if (@($mp4 + $gif | Where-Object Length -lt 1000).Count -ne 0) {
    throw "one or more capture outputs are unexpectedly small"
}
Write-Host "`nCAPTURE_BATCH_OK: 11 MP4 + 11 GIF -> $outDir"
Get-ChildItem $outDir -File | Sort-Object Name | Select-Object Name, Length, LastWriteTime
