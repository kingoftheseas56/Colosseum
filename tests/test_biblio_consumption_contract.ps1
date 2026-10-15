# Static wiring contract for Arc 19 Biblio consumption-first navigation.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$qml = Get-Content -Raw -LiteralPath (Join-Path $root "qml/BiblioBook.qml")
function Require([string]$needle, [string]$message) {
    if ($qml -notlike "*$needle*") {
        Write-Host "FAIL: $message"
        exit 1
    }
}

Require 'property var booksRef:' "Books service seam exists"
Require 'property var bookTorrentsRef:' "BookTorrents service seam exists"
Require 'property int pendingReadGeneration:' "foreground generation guard exists"
Require 'property string pendingReadTransport:' "transport identity is retained"
Require 'property string pendingReadAcquisitionId:' "exact md5/infoHash is retained"
Require 'property string pendingReadBookIdentity:' "book context identity is retained"
Require 'function readBook()' "primary Read orchestration exists"
Require 'function _resolvePendingLookup(gen)' "source discovery continuation exists"
Require 'if (detail.edLoading || detail.torLoading)' "new acquisition waits for both inventories"
Require 'function _finishRead(transport, id, gen)' "exact completion gate exists"
Require 'detail.pendingReadTransport !== transport' "completion checks transport identity"
Require 'detail.pendingReadAcquisitionId !== String(id)' "completion checks acquisition identity"
Require 'detail._finishRead("books", String(md5)' "Books completion funnels through exact gate"
Require 'detail._finishRead("torrent", String(hash).toLowerCase()' "torrent completion funnels through exact gate"
Require 'function downloadEdition(ed)' "explicit edition Download is separate"
Require 'function downloadTorrent(row)' "explicit torrent Download is separate"
Require 'onTriggered: { detail._invalidateReadIntent(); detail.backRequested() }' "Back clears foreground Read only"
Require 'text: detail.primaryReadLabel()' "hero CTA uses consumption-first label"
Require 'onClicked: detail.readBook()' "hero CTA asserts Read, not Download"
Require 'detail.downloadEdition(edRow.modelData)' "edition rows remain acquire-only"
Require 'detail.downloadTorrent(torRow.modelData)' "torrent rows remain acquire-only"
Require 'detail.chooseReadSource(modelData)' "Read source chooser preserves intent"
Require 'detail.cancelReadChoice()' "source chooser can abandon foreground intent"

if ($qml -match 'Get the book|Find the book') {
    Write-Host "FAIL: acquisition-first hero wording remains"
    exit 1
}
if ($qml -match 'TODO|PLACEHOLDER') {
    Write-Host "FAIL: draft contains TODO/PLACEHOLDER scaffolding"
    exit 1
}
Write-Host "Biblio consumption-first static contract: OK"
