$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -Raw -LiteralPath (Join-Path $root "qml/Main.qml")
$downloads = Get-Content -Raw -LiteralPath (Join-Path $root "qml/DownloadsPage.qml")
$localDownloads = Get-Content -Raw -LiteralPath (Join-Path $root "native/engine/LocalDownloads.cpp")

function Require-In([string]$text, [string]$needle, [string]$message) {
    if ($text -notlike "*$needle*") {
        Write-Host "FAIL: $message"
        exit 1
    }
}

# One physical EPUB must be one Biblio session whether entered from its detail page or Downloads.
Require-In $main '"target": { "path": path, "book": b, "id": path }' "book session identity must be the local file path"

# Downloads must preserve enough persisted metadata for Reader2/audiobook pairing and a useful taskbar title.
Require-In $main '"title": item.title || "", "author": item.author || ""' "Downloads Read must pass title and author"


# The durable Books index must feed the Biblio Downloads shelf, whose completed row exposes Read.
Require-In $localDownloads 'm_books->downloadedBooks()' "LocalDownloads must enumerate completed Books"
Require-In $localDownloads 'QStringLiteral("world"), QStringLiteral("biblio")' "completed books must enter the Biblio lane"
Require-In $downloads 'text: row.rowData.world === "theatre" ? "Play" : "Read"' "Biblio completed rows must expose Read"
Require-In $downloads 'onClicked: root.openRequested(row.rowData)' "completed row Read must emit the exact persisted row"

Write-Host "Biblio Downloads->Reader route: OK"