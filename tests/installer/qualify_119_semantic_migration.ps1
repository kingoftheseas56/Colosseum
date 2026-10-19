param(
    [Parameter(Mandatory=$true)][string]$PreviousInstaller,
    [Parameter(Mandatory=$true)][string]$Installer
)
$ErrorActionPreference = 'Stop'
function Require([bool]$ok, [string]$reason) {
    if (!$ok) { throw "PREFLIGHT_SEMANTIC_MIGRATION_FAIL: $reason" }
}
function Sha([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
Require ($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_ENVIRONMENT -eq 'github-hosted') 'disposable GitHub-hosted Windows required'
Require ((Sha $PreviousInstaller) -eq '34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e') 'published 1.1.8 digest'
Require ((Sha $Installer) -eq '3aea853978853958dc8e47fec0fa72a4514f4510d13323cdfe99bbff78e7a2a9') 'exact final 1.1.9 digest'

$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\Colosseum'
$regPath = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum'
$exe = Join-Path $installRoot 'native\build-msvc\colosseum.exe'
Require (!(Test-Path $installRoot) -and !(Test-Path $regPath)) 'fresh runner must not have Colosseum'
$tag = "semantic119-$env:GITHUB_RUN_ID"
$pipeName = "PreflightSemantic119-$env:GITHUB_RUN_ID"
$env:COLOSSEUM_APPDATA_TAG = $tag
$env:COLOSSEUM_LANISTA_PIPE = $pipeName
$env:COLOSSEUM_AUTOMATION_FOCUS_SEAMS = '0'
$profile = Join-Path $env:APPDATA "Brotherhood\Colosseum-dltest-$tag"
$fixtureDir = Join-Path $env:RUNNER_TEMP 'preflight119-offline-book'
$bookPath = Join-Path $fixtureDir 'preflight-offline-book.epub'
$bookId = 'preflight:manual119:book:001'
$bookTitle = 'Preflight Offline Reading Fixture'
$author = 'Offline CI Fixture'
$old = $null
$new = $null

function Write-ZipEntry($zip,[string]$name,[string]$text,[System.IO.Compression.CompressionLevel]$compression = [System.IO.Compression.CompressionLevel]::Optimal) {
    $entry = $zip.CreateEntry($name,$compression)
    $stream = $entry.Open()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($text)
        $stream.Write($bytes,0,$bytes.Length)
    } finally { $stream.Dispose() }
}
function New-OfflineEpub {
    New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
    Add-Type -AssemblyName System.IO.Compression
    $z = [System.IO.Compression.ZipFile]::Open($bookPath,[System.IO.Compression.ZipArchiveMode]::Create)
    try {
        Write-ZipEntry $z 'mimetype' 'application/epub+zip' ([System.IO.Compression.CompressionLevel]::NoCompression)
        Write-ZipEntry $z 'META-INF/container.xml' '<?xml version="1.0" encoding="UTF-8"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
        Write-ZipEntry $z 'OEBPS/book.opf' '<?xml version="1.0" encoding="UTF-8"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">preflight:manual119:book:001</dc:identifier><dc:title>Preflight Offline Reading Fixture</dc:title><dc:language>en</dc:language><dc:creator>Offline CI Fixture</dc:creator><meta property="dcterms:modified">2026-10-19T00:00:00Z</meta></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="chapter"/></spine></package>'
        Write-ZipEntry $z 'OEBPS/chapter.xhtml' '<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Fixture</title></head><body><h1>Offline reading fixture</h1><p>A single deterministic chapter, for installer migration checks.</p></body></html>'
    } finally { $z.Dispose() }
    Require ((Get-Item $bookPath).Length -gt 100) 'local offline EPUB created'
}
function New-ProbeQml([bool]$seed,[string]$destination) {
    $qml = @'
import QtQuick
import QtQuick.Window
Window {
    id: root
    width: 880
    height: 400
    visible: true
    color: "#111a26"
    title: "Preflight exact-state receiving check"
    property bool seedMode: __SEED_MODE__
    property bool completed: false
    property int ticks: 0
    Item {
        id: probe
        objectName: "preflightSemanticProbe"
        anchors.fill: parent
        property string phase: "starting"
        property string report: ""
        Text {
            objectName: "preflightSemanticText"
            anchors.fill: parent
            anchors.margins: 15
            color: "white"
            text: probe.report
            wrapMode: Text.WrapAnywhere
            font.pixelSize: 13
        }
    }
    Timer {
        interval: 300
        repeat: true
        running: !root.completed
        onTriggered: {
            root.ticks++
            if (typeof AccountController === "undefined" || !AccountController) return
            if (AccountController.mode !== "localOnly") {
                if (AccountController.mode === "signedOut")
                    AccountController.continueWithoutAccount()
                return
            }
            if (typeof Collection === "undefined" || !Collection
                    || typeof Progress === "undefined" || !Progress
                    || typeof ProfilePreferences === "undefined" || !ProfilePreferences) return
            if (root.seedMode) {
                var stored = Collection.add("biblio", {
                    id: "preflight:manual119:book:001",
                    type: "book",
                    title: "Preflight Offline Reading Fixture",
                    author: "Offline CI Fixture",
                    cover: "",
                    payload: { book: { id: "preflight:manual119:book:001",
                                       title: "Preflight Offline Reading Fixture",
                                       author: "Offline CI Fixture", cover: "" } }
                })
                if (!stored) {
                    probe.phase = "seed-failed"
                    console.error("PREFLIGHT_SEMANTIC_SEED_COLLECTION_FAILED")
                    root.completed = true
                    return
                }
                Progress.record({
                    id: "preflight:manual119:book:001",
                    kind: "book",
                    caption: "Preflight Offline Reading Fixture",
                    title: "Preflight Offline Reading Fixture",
                    sub: "37%",
                    cover: "",
                    c1: "#2a2440", c2: "#15111f",
                    progress: 0.37,
                    resume: { path: __BOOK_PATH__, book: {
                        id: "preflight:manual119:book:001",
                        title: "Preflight Offline Reading Fixture",
                        author: "Offline CI Fixture", cover: "" } }
                })
                ProfilePreferences.setRememberSearchHistory(false)
            }
            var collection = Collection.items("biblio")
            var item = null
            for (var i=0; i<collection.length; ++i) {
                if (collection[i].id === "preflight:manual119:book:001") {
                    item=collection[i]
                    break
                }
            }
            var record = Progress.get("book", "preflight:manual119:book:001")
            var recents = Progress.recent("book", 200)
            var recentMatched = false
            for (var j=0; j<recents.length; ++j)
                if (recents[j].id === "preflight:manual119:book:001") recentMatched = true
            var snapshot = {
                ownerMode: AccountController.mode,
                library: item ? {
                    id: String(item.id), world: String(item.world), type: String(item.type),
                    title: String(item.title), author: String(item.author),
                    payloadBookId: String(item.payload.book.id)
                } : null,
                readingProgress: record && record.id ? {
                    id: String(record.id), kind: String(record.kind),
                    title: String(record.title), sub: String(record.sub),
                    fraction: Number(record.progress),
                    resumePath: String(record.resume.path),
                    resumeBookId: String(record.resume.book.id),
                    visibleInRecent: recentMatched
                } : null,
                preferences: { rememberSearchHistory: ProfilePreferences.rememberSearchHistory }
            }
            probe.report = JSON.stringify(snapshot)
            probe.phase = "ready"
            root.completed = true
            console.info("PREFLIGHT_SEMANTIC_PROBE " + probe.report)
        }
    }
}
'@
    $boolLiteral = if($seed){'true'}else{'false'}
    $pathJson = ConvertTo-Json -InputObject $bookPath -Compress
    $qml = $qml.Replace('__SEED_MODE__',$boolLiteral).Replace('__BOOK_PATH__',$pathJson)
    [System.IO.File]::WriteAllText($destination,$qml,[Text.UTF8Encoding]::new($false))
}
function Invoke-LanistaRead([string]$object,[string[]]$properties) {
    $socket = [System.IO.Pipes.NamedPipeClientStream]::new('.',$pipeName,[System.IO.Pipes.PipeDirection]::InOut)
    try {
        $socket.Connect(1200)
        $payload = @{seq=119;cmd='qml-get';payload=@{object=$object;props=$properties}}
        $query = (ConvertTo-Json -InputObject $payload -Compress -Depth 6) + [char]10
        $buf = [Text.Encoding]::UTF8.GetBytes($query)
        $socket.Write($buf,0,$buf.Length)
        $socket.Flush()
        $reader = [IO.StreamReader]::new($socket)
        $pending = $reader.ReadLineAsync()
        if (!$pending.Wait(3000)) { throw 'Lanista read timed out' }
        $reply = ConvertFrom-Json -InputObject $pending.Result
        if ($reply.type -ne 'reply' -or $reply.seq -ne 119 -or $null -eq $reply.props)
            { throw "Lanista did not return expected qml-get props: $($pending.Result)" }
        return $reply.props
    } finally { $socket.Dispose() }
}
function Read-RenderedSnapshot([System.Diagnostics.Process]$process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    $last = ''
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) { throw "installed app exited before Lanista state receipt: $($process.ExitCode) $last" }
        try {
            $probe = Invoke-LanistaRead 'preflightSemanticProbe' @('phase','report','visible')
            if ($probe.phase -eq 'seed-failed') { throw 'app-owned collection.add refused fixture' }
            if ($probe.phase -eq 'ready' -and ![string]::IsNullOrWhiteSpace($probe.report)) {
                $visibleText = Invoke-LanistaRead 'preflightSemanticText' @('text','visible')
                Require ($visibleText.visible -eq $true -and $visibleText.text -eq $probe.report) 'visible QML Text does not render actual app owner snapshot'
                return ($probe.report | ConvertFrom-Json -ErrorAction Stop)
            }
            $last = "phase=$($probe.phase)"
        } catch {
            $last = $_.Exception.Message
            if($last -like '*seed-failed*' -or $last -like '*refused fixture*') { throw }
        }
        Start-Sleep -Milliseconds 450
    }
    throw "timed out reading actual app-owned state through Lanista: $last"
}
function Assert-OwnerSnapshot($data,[string]$label) {
    Require ($data.ownerMode -eq 'localOnly') "$label active personal profile not localOnly"
    Require ($null -ne $data.library -and $data.library.id -eq $bookId) "$label Collection.items(biblio) lacks exact book id"
    Require ($data.library.world -eq 'biblio' -and $data.library.type -eq 'book') "$label wrong Collection world/type"
    Require ($data.library.title -eq $bookTitle -and $data.library.author -eq $author -and $data.library.payloadBookId -eq $bookId) "$label Collection field readback mismatch"
    Require ($null -ne $data.readingProgress -and $data.readingProgress.id -eq $bookId) "$label Progress.get(book,id) absent"
    Require ($data.readingProgress.kind -eq 'book' -and $data.readingProgress.title -eq $bookTitle -and $data.readingProgress.sub -eq '37%') "$label Progress text fields mismatch"
    Require ([Math]::Abs([double]$data.readingProgress.fraction - 0.37) -lt 0.000001) "$label reading fraction mismatch"
    Require ($data.readingProgress.resumeBookId -eq $bookId -and $data.readingProgress.resumePath -eq $bookPath) "$label resume book/path mismatch"
    Require ($data.readingProgress.visibleInRecent -eq $true) "$label Progress.recent(book) did not expose saved book"
    Require ($data.preferences.rememberSearchHistory -eq $false) "$label ProfilePreferences.rememberSearchHistory not false"
}
function Start-InstalledWithProbe([string]$qml,[string]$name) {
    $out = Join-Path $env:RUNNER_TEMP "preflight-119-$name.stdout.log"
    $err = Join-Path $env:RUNNER_TEMP "preflight-119-$name.stderr.log"
    return Start-Process -FilePath $exe -WorkingDirectory $installRoot -ArgumentList ('"'+$qml+'"') -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
}
function Stop-InstalledNormally([System.Diagnostics.Process]$process,[string]$stage,[bool]$legacyDiagnostic = $false) {
    $process.Refresh()
    Require (!$process.HasExited) "$stage application vanished before orderly close"
    Require ($process.CloseMainWindow()) "$stage had no closable main QQuickWindow"
    Require ($process.WaitForExit(20000)) "$stage app failed to exit on ordinary window-close"
    $exitRecord = @{stage=$stage; exitCode=$process.ExitCode; legacyDiagnostic=$legacyDiagnostic}
    $exitRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $env:RUNNER_TEMP "preflight-119-semantic-$stage-exit.json") -Encoding utf8
    if ($legacyDiagnostic -and $process.ExitCode -eq -1073741819) {
        # Published 1.1.8 crashes when this diagnostic root closes. Keep the
        # failure visible; a fresh old-app read below must prove persistence.
        Write-Warning "$stage diagnostic close access violation 0xC0000005; NOT graceful-shutdown evidence"
    } else {
        Require ($process.ExitCode -eq 0) "$stage graceful app exit=$($process.ExitCode)"
    }
}
try {
    New-OfflineEpub
    $fixtureSha = Sha $bookPath
    Write-Host "PREFLIGHT_SEMANTIC_OFFLINE_EPUB bytes=$((Get-Item $bookPath).Length) SHA256=$fixtureSha"
    $installOld = Start-Process -FilePath $PreviousInstaller -ArgumentList '/S' -Wait -PassThru
    Require ($installOld.ExitCode -eq 0) "published 1.1.8 install exit=$($installOld.ExitCode)"
    Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.8') 'old installation metadata not 1.1.8'
    Require ((& $exe --version | Out-String).Trim() -eq 'Colosseum 1.1.8' -and $LASTEXITCODE -eq 0) 'actual 1.1.8 app version'
    $oldQml = Join-Path $env:RUNNER_TEMP 'preflight-119-seed.qml'
    New-ProbeQml $true $oldQml
    $old = Start-InstalledWithProbe $oldQml 'old'
    $before = Read-RenderedSnapshot $old
    Assert-OwnerSnapshot $before 'old-1.1.8'
    $beforePath = Join-Path $env:RUNNER_TEMP 'preflight-119-semantic-before.json'
    $before | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $beforePath -Encoding utf8
    Write-Host ('PREFLIGHT_SEMANTIC_BEFORE '+(ConvertTo-Json -InputObject $before -Compress -Depth 9))
    # Progress persistence is debounced. Allow the old app's normal disk writer
    # to run before closing the diagnostic root; no direct store-file seeding.
    Start-Sleep -Seconds 6
    Stop-InstalledNormally $old 'old-1.1.8' $true
    $oldReadQml = Join-Path $env:RUNNER_TEMP 'preflight-119-old-read.qml'
    New-ProbeQml $false $oldReadQml
    $old = Start-InstalledWithProbe $oldReadQml 'old-reopened'
    $oldPersisted = Read-RenderedSnapshot $old
    Assert-OwnerSnapshot $oldPersisted 'old-1.1.8-reopened'
    Require ((ConvertTo-Json -InputObject $oldPersisted -Compress -Depth 9) -eq (ConvertTo-Json -InputObject $before -Compress -Depth 9)) 'old-app restart did not restore seeded owner state'
    $oldPersisted | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $env:RUNNER_TEMP 'preflight-119-semantic-old-reopened.json') -Encoding utf8
    Write-Host ('PREFLIGHT_SEMANTIC_OLD_REOPENED '+(ConvertTo-Json -InputObject $oldPersisted -Compress -Depth 9))
    Stop-InstalledNormally $old 'old-1.1.8-reopened' $true
    Require (Test-Path (Join-Path $profile 'collection-store.ini')) 'old application did not write Collection owner data'
    Require (Test-Path (Join-Path $profile 'progress-store.ini')) 'old application did not persist Progress owner data'
    $collectionShaBefore = Sha (Join-Path $profile 'collection-store.ini')
    $progressShaBefore = Sha (Join-Path $profile 'progress-store.ini')
    $overlay = Start-Process -FilePath $Installer -ArgumentList '/S' -Wait -PassThru
    Require ($overlay.ExitCode -eq 0) "1.1.9 manual overlay exit=$($overlay.ExitCode)"
    Require ((Get-ItemProperty $regPath).DisplayVersion -eq '1.1.9') 'final install registry version'
    Require ((Sha $exe) -eq '0067926231764af1a8005e22ccdee869a8424d84a190aa5aedeae370e403746b') 'final app executable hash'
    Require ((Sha (Join-Path $profile 'collection-store.ini')) -eq $collectionShaBefore) 'Collection owner file changed during overlay'
    Require ((Sha (Join-Path $profile 'progress-store.ini')) -eq $progressShaBefore) 'Progress owner file changed during overlay'
    $newQml = Join-Path $env:RUNNER_TEMP 'preflight-119-read.qml'
    New-ProbeQml $false $newQml
    $new = Start-InstalledWithProbe $newQml 'new'
    $after = Read-RenderedSnapshot $new
    Assert-OwnerSnapshot $after 'new-1.1.9'
    $afterPath = Join-Path $env:RUNNER_TEMP 'preflight-119-semantic-after.json'
    $after | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $afterPath -Encoding utf8
    $beforeCompact = ConvertTo-Json -InputObject $before -Compress -Depth 9
    $afterCompact = ConvertTo-Json -InputObject $after -Compress -Depth 9
    Require ($beforeCompact -eq $afterCompact) 'old/new app-owned semantic readings differ'
    Write-Host ('PREFLIGHT_SEMANTIC_AFTER '+$afterCompact)
    Stop-InstalledNormally $new 'new-1.1.9'
    Write-Host "PREFLIGHT_SEMANTIC_119_OK fixtureId=$bookId fraction=0.37 preferenceRememberSearchHistory=false beforeAfterEqual=true offlineEpubSha256=$fixtureSha collectionIniSha256=$collectionShaBefore progressIniSha256=$progressShaBefore"
} finally {
    foreach($process in @($old,$new)) {
        if($null -ne $process) {
            try { $process.Refresh(); if(!$process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue} } catch {}
        }
    }
    $logPath = Join-Path $profile 'logs\colosseum.log'
    if(Test-Path $logPath) {
        $logLines = @(Get-Content -LiteralPath $logPath | Where-Object {$_ -match 'PREFLIGHT_SEMANTIC|qml_override|QQml|migrate|applog'})
        $logLines | Select-Object -Last 100 | Set-Content -LiteralPath (Join-Path $env:RUNNER_TEMP 'preflight-119-semantic-app.log') -Encoding utf8
    }
    if(Test-Path (Join-Path $installRoot 'uninstall.exe')) {
        try {
            $uninstall = Start-Process -FilePath (Join-Path $installRoot 'uninstall.exe') -ArgumentList '/S' -Wait -PassThru
            Write-Host "PREFLIGHT_SEMANTIC_CLEANUP_UNINSTALL_EXIT=$($uninstall.ExitCode)"
        } catch { Write-Warning "Only CI-owned uninstaller cleanup failed: $($_.Exception.Message)" }
    }
}