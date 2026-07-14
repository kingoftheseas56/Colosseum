// Headless contract for the alternate-sources picker: show() starts one auto
// search and builds the edition identity rail; partial/final rows land; a weak
// row needs explicit confirmation before downloading; an ambiguous manifest
// opens the archive picker; a stale issueId is ignored.
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: false

    // Fake global Comics: records the facade calls the page is supposed to make.
    QtObject {
        id: fakeApi
        property int autoSearchCount: 0
        property int manualSearchCount: 0
        property int cancelCount: 0
        property int downloadCount: 0
        property int cancelDownloadCount: 0
        property int chosenFileIndex: -1
        property string lastPickerTitle: ""
        signal torrentSourcesUpdated(string issueId, var rows, bool complete)
        signal torrentSourceSearchFailed(string issueId, string reason)
        signal torrentArchiveSelectionRequired(string issueId, var files)
        signal torrentArchiveSelected(string issueId, string fileName, bool automatic)
        function searchTorrentSources(issueId, seriesTitle, editionTitle, isbn, collects) { autoSearchCount++ }
        function searchTorrentSourcesQuery(issueId, query) { manualSearchCount++ }
        function cancelTorrentSourceSearch(issueId) { cancelCount++ }
        function downloadTorrentSource(issueId, seriesId, seriesTitle, issueLabel, infoHash, releaseTitle, magnetUri) {
            downloadCount++; lastPickerTitle = issueLabel
        }
        function cancelDownload(issueId) { cancelDownloadCount++ }
        function chooseTorrentArchive(issueId, fileIndex) { chosenFileIndex = fileIndex }
    }

    Loader {
        id: loader
        anchors.fill: parent
        source: "../qml/ComicTorrentSourcesPage.qml"
        onLoaded: {
            var page = loader.item
            page.comicsApi = fakeApi

            function fail(msg, code) { console.log("COMIC_TORRENT_SOURCES_PAGE_FAIL " + msg); Qt.exit(code) }

            page.show({ issueId: "gc:saga:book-one", seriesId: "gc:saga",
                        seriesTitle: "Saga", editionTitle: "Saga: Book One",
                        isbn: "9781632150783", collects: "Saga #1-18",
                        year: "2014", cover: "" })

            Qt.callLater(function() {
                if (fakeApi.autoSearchCount !== 1) return fail("show() must start one auto search", 2)
                if (page.identityLine.indexOf("9781632150783") < 0) return fail("identity rail missing ISBN", 2)

                var strongRow = { infoHash: "a", title: "Saga Book One 1-18 CBZ",
                                  confidence: "strong", magnetUri: "magnet:strong",
                                  evidence: ["TITLE", "ISSUES"], sizeText: "180 MB", seeders: 8 }
                var weakRow = { infoHash: "b", title: "Annihilation Saga Issue 1",
                                confidence: "weak", magnetUri: "magnet:weak",
                                evidence: ["ARCHIVE"], sizeText: "40 MB", seeders: 3 }

                page.applySources("gc:saga:book-one", [strongRow, weakRow], true)
                if (page.visibleRows.length !== 2) return fail("every returned row must stay visible", 2)

                // A stale update for another edition must be ignored.
                page.applySources("some-other-id", [strongRow], true)
                if (page.visibleRows.length !== 2) return fail("stale issueId must be ignored", 2)

                // A weak row requires explicit confirmation before it downloads.
                page.selectRow(weakRow)
                if (!page.confirmingWeak) return fail("weak selection must ask to confirm", 2)
                if (fakeApi.downloadCount !== 0) return fail("weak selection must not download yet", 2)
                page.confirmWeakSelection()
                if (fakeApi.downloadCount !== 1) return fail("confirming a weak row downloads it", 2)
                if (fakeApi.lastPickerTitle !== "Saga: Book One")
                    return fail("canonical edition title, not release title, is the picker title", 2)

                // A strong row downloads immediately, no confirmation.
                page.selectRow(strongRow)
                if (page.confirmingWeak) return fail("a strong row must not require confirmation", 2)
                if (fakeApi.downloadCount !== 2) return fail("a strong row downloads immediately", 2)

                // An ambiguous manifest opens the archive picker.
                page.applyArchiveChoices("gc:saga:book-one",
                    [{ index: 0, name: "Saga v01.cbz", extension: "cbz", sizeText: "90 MB" },
                     { index: 1, name: "Saga v02.cbz", extension: "cbz", sizeText: "92 MB" }])
                if (!page.archiveChoosing) return fail("ambiguous manifest opens the archive picker", 2)
                page.chooseArchive(1)
                if (fakeApi.chosenFileIndex !== 1) return fail("chosen archive index forwards to the facade", 2)

                // Backing out of a live acquisition (selected, not yet handed off) must
                // tear down the download, not just the search.
                if (!page.acquiring) return fail("a selected-but-unfinished acquisition is live", 2)
                page.hide()
                if (fakeApi.cancelDownloadCount !== 1) return fail("Back must cancel the live download", 2)

                // A successful archive handoff closes the page WITHOUT cancelling.
                page.show({ issueId: "gc:saga:book-one", seriesId: "gc:saga",
                            seriesTitle: "Saga", editionTitle: "Saga: Book One",
                            isbn: "9781632150783", collects: "Saga #1-18", year: "2014", cover: "" })
                page.applySources("gc:saga:book-one", [strongRow], true)
                page.selectRow(strongRow)
                if (!page.acquiring) return fail("a strong-row selection is a live acquisition", 2)
                fakeApi.torrentArchiveSelected("gc:saga:book-one", "Saga.cbz", true)
                if (page.open || page.acquiring) return fail("archive handoff closes the page", 2)
                if (fakeApi.cancelDownloadCount !== 1) return fail("a successful handoff must NOT cancel the download", 2)

                console.log("COMIC_TORRENT_SOURCES_PAGE_OK")
                Qt.exit(0)
            })
        }
        onStatusChanged: {
            if (loader.status === Loader.Error) {
                console.log("COMIC_TORRENT_SOURCES_PAGE_FAIL component failed to load")
                Qt.exit(4)
            }
        }
    }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            console.log("COMIC_TORRENT_SOURCES_PAGE_FAIL timeout")
            Qt.exit(3)
        }
    }
}
