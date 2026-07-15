// Headless contract for the alternate-sources picker: show() starts one auto
// search and builds the edition identity rail; partial/final rows land with
// coverage/trust evidence intact; a weak row needs explicit confirmation
// before the AUTOMATIC pack-selection path is called; a stale issueId is
// ignored; the typed pack outcomes (ambiguous/incomplete/combined) each
// render their own state and never auto-download; a safe auto-decision (or a
// resumed manual choice) closes the page on the next progress/finished tick;
// Back during any live state cancels the download exactly once.
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
        property int chooseFilesCount: 0
        property int confirmCombinedCount: 0
        property var lastChosenIndices: []
        property string lastEditionTitle: ""
        property string lastIsbn: ""
        property string lastCollects: ""
        property string lastInfoHash: ""
        signal torrentSourcesUpdated(string issueId, var rows, bool complete)
        signal torrentSourceSearchFailed(string issueId, string reason)
        signal torrentArchiveSelectionRequired(string issueId, var files)
        signal torrentCombinedArchiveConfirmationRequired(string issueId, var files)
        signal torrentIncompleteIssueSetDetected(string issueId, var missingIssues)
        signal progress(string issueId, double done, double total)
        signal finished(string issueId)
        signal failed(string issueId, string reason)
        function searchTorrentSources(issueId, seriesTitle, editionTitle, isbn, collects) { autoSearchCount++ }
        function searchTorrentSourcesQuery(issueId, query) { manualSearchCount++ }
        function cancelTorrentSourceSearch(issueId) { cancelCount++ }
        function downloadTorrentEdition(issueId, seriesId, seriesTitle, editionTitle, isbn, collects, infoHash, magnetUri) {
            downloadCount++
            lastEditionTitle = editionTitle; lastIsbn = isbn; lastCollects = collects; lastInfoHash = infoHash
        }
        function cancelDownload(issueId) { cancelDownloadCount++ }
        function chooseTorrentFiles(issueId, indices) { chooseFilesCount++; lastChosenIndices = indices }
        function confirmCombinedArchive(issueId) { confirmCombinedCount++ }
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
                if (page.selectionState !== "results") return fail("show() must start in the results state", 2)

                var strongRow = { infoHash: "a", title: "Saga Book One 1-18 CBZ",
                                  confidence: "strong", magnetUri: "magnet:strong",
                                  evidence: ["TITLE", "ISSUES"], sizeText: "180 MB", seeders: 8,
                                  coverage: true, uploader: "TrustedGuy", trustTier: 1 }
                var weakRow = { infoHash: "b", title: "Annihilation Saga Issue 1",
                                confidence: "weak", magnetUri: "magnet:weak",
                                evidence: ["ARCHIVE"], sizeText: "40 MB", seeders: 3,
                                coverage: false, uploader: "", trustTier: 99 }

                page.applySources("gc:saga:book-one", [strongRow, weakRow], true)
                if (page.visibleRows.length !== 2) return fail("every returned row must stay visible", 2)

                // Coverage/trust evidence must survive into the row model unchanged
                // (the page's badges bind directly to these fields).
                if (page.visibleRows[0].coverage !== true) return fail("coverage must reach the row model", 2)
                if (page.visibleRows[0].trustTier !== 1) return fail("trustTier must reach the row model", 2)
                if (page.visibleRows[0].uploader !== "TrustedGuy") return fail("uploader must reach the row model", 2)
                if (page.visibleRows[0].evidence.indexOf("ISSUES") < 0) return fail("ISSUES evidence must reach the row model", 2)

                // A stale update for another edition must be ignored.
                page.applySources("some-other-id", [strongRow], true)
                if (page.visibleRows.length !== 2) return fail("stale issueId must be ignored", 2)

                // A weak row requires explicit confirmation before the automatic path runs.
                page.selectRow(weakRow)
                if (!page.confirmingWeak) return fail("weak selection must ask to confirm", 2)
                if (fakeApi.downloadCount !== 0) return fail("weak selection must not download yet", 2)
                page.confirmWeakSelection()
                if (fakeApi.downloadCount !== 1) return fail("confirming a weak row runs the automatic pack path", 2)
                if (fakeApi.lastEditionTitle !== "Saga: Book One")
                    return fail("canonical edition title, not release title, is the match identity", 2)
                if (fakeApi.lastIsbn !== "9781632150783" || fakeApi.lastCollects !== "Saga #1-18")
                    return fail("canonical isbn/collects must be passed to the automatic pack path", 2)
                if (page.selectionState !== "inspecting") return fail("a chosen row enters the inspecting state", 2)
                if (!page.acquiring) return fail("a chosen row is a live acquisition", 2)

                // Back during "inspecting" cancels the live download exactly once.
                var cancelsBefore = fakeApi.cancelDownloadCount
                page.hide()
                if (fakeApi.cancelDownloadCount !== cancelsBefore + 1)
                    return fail("Back during inspecting must cancel the download exactly once", 2)
                if (page.open) return fail("Back must close the page", 2)

                // ---- ambiguous: opens the archive picker, resolves via chooseTorrentFiles ----
                page.show({ issueId: "gc:saga:book-one", seriesId: "gc:saga",
                            seriesTitle: "Saga", editionTitle: "Saga: Book One",
                            isbn: "9781632150783", collects: "Saga #1-18", year: "2014", cover: "" })
                page.applySources("gc:saga:book-one", [strongRow], true)
                page.selectRow(strongRow)
                if (page.selectionState !== "inspecting") return fail("a strong row enters inspecting immediately", 2)

                // Pack-shaped candidates: {index, path, bytes} (no name/extension/sizeText).
                fakeApi.torrentArchiveSelectionRequired("gc:saga:book-one",
                    [{ index: 0, path: "Saga v01.cbz", bytes: 94371840 },
                     { index: 1, path: "Saga v02.cbz", bytes: 96468992 }])
                if (page.selectionState !== "ambiguous") return fail("an ambiguous manifest opens the archive picker", 2)
                page.chooseArchive(1)
                if (fakeApi.chooseFilesCount !== 1) return fail("chosen archive index forwards to chooseTorrentFiles", 2)
                if (String(fakeApi.lastChosenIndices) !== String([1]))
                    return fail("the chosen index must forward as a single-element list", 2)
                if (page.selectionState !== "inspecting") return fail("resolving an ambiguous pick resumes inspecting", 2)
                if (!page.acquiring) return fail("a resumed pick is still a live acquisition", 2)

                // A safe resumption closes the page on the next progress tick, WITHOUT cancelling.
                cancelsBefore = fakeApi.cancelDownloadCount
                fakeApi.progress("gc:saga:book-one", 10, 100)
                if (page.open) return fail("a safe progress tick must close the page", 2)
                if (fakeApi.cancelDownloadCount !== cancelsBefore)
                    return fail("a safe resumption must NOT cancel the download", 2)

                // ---- incomplete: names the missing issues, makes NO download call ----
                page.show({ issueId: "gc:saga:book-one", seriesId: "gc:saga",
                            seriesTitle: "Saga", editionTitle: "Saga: Book One",
                            isbn: "9781632150783", collects: "Saga #1-18", year: "2014", cover: "" })
                page.applySources("gc:saga:book-one", [strongRow], true)
                page.selectRow(strongRow)
                var downloadsBefore = fakeApi.downloadCount
                fakeApi.torrentIncompleteIssueSetDetected("gc:saga:book-one", ["Saga #4", "Saga #9"])
                if (page.selectionState !== "incomplete") return fail("an incomplete issue set opens the incomplete state", 2)
                if (String(page.missingIssues) !== String(["Saga #4", "Saga #9"]))
                    return fail("the incomplete state must name the missing issues", 2)
                if (fakeApi.downloadCount !== downloadsBefore)
                    return fail("an incomplete issue set must make NO download call automatically", 2)
                cancelsBefore = fakeApi.cancelDownloadCount
                page.rejectIncomplete(false)
                if (fakeApi.cancelDownloadCount !== cancelsBefore + 1)
                    return fail("rejecting an incomplete set cancels the abandoned pack once", 2)
                if (page.selectionState !== "results" || !page.open)
                    return fail("rejecting an incomplete set returns to browsing, page stays open", 2)

                // ---- combined: requires a second, explicit confirmation ----
                page.applySources("gc:saga:book-one", [strongRow], true)
                page.selectRow(strongRow)
                fakeApi.torrentCombinedArchiveConfirmationRequired("gc:saga:book-one",
                    [{ index: 0, path: "Saga Compendium 1-3.cbz", bytes: 314572800 }])
                if (page.selectionState !== "combined") return fail("a combined-only manifest opens the combined state", 2)
                if (fakeApi.confirmCombinedCount !== 0)
                    return fail("a combined-only manifest must not confirm itself", 2)
                page.confirmCombined()
                if (fakeApi.confirmCombinedCount !== 1) return fail("the explicit confirm calls confirmCombinedArchive", 2)
                if (page.selectionState !== "inspecting") return fail("confirming combined resumes inspecting", 2)
                fakeApi.finished("gc:saga:book-one")
                if (page.open) return fail("a finished signal must also close the page safely", 2)

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
