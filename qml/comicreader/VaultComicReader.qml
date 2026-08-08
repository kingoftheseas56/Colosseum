import QtQuick

// VaultComicReader — the standalone reader host for a single loose comic file opened
// from the Vault (execution plan Slice 8). A local CBZ has no series page behind it, so
// — exactly like ComicReaderPreview / BakeoffStripHost — we mount a bare ComicReaderShell
// and inject a page store. The store delegates page listing to the app's C++ VaultPageStore
// (which mirrors the Tankoban volume lane's descriptors), so the CBZ reads with ZERO reader
// edits. Window verbs surface as signals the window wires to the session-aware shell verbs.
Item {
    id: root
    anchors.fill: parent

    property string archivePath: ""     // the CBZ on disk == the shell's chapterId (store key)
    property string vaultId: ""         // "vault:<sha1>" — the session/progress identity
    property string title: "Comic"

    signal minimizeRequested()
    signal closeRequested()
    signal backRequested()
    signal fullscreenRequested()

    // The injected store: delegate localPages to the real C++ VaultPageStore, and supply the
    // no-op acquisition seams + download-line signals the shell's Connections expect. A local
    // file is always "ready" (localPages returns the pages), so the download path never fires —
    // but the seams must exist so a stray call can never throw and wedge the reader.
    QtObject {
        id: vaultStore
        signal progress(string cid, int done, int total)
        signal finished(string cid)
        signal failed(string cid, string reason)
        function localPages(entryId) {
            return (typeof VaultPageStore !== "undefined") ? VaultPageStore.localPages(entryId) : []
        }
        function downloadChapter(cid, seriesId, seriesTitle, label) {}
        function downloadIssue(cid, url, seriesId, seriesTitle, label, bytes) {}
        function startDownload(cid) {}
    }

    ComicReaderShell {
        id: shell
        anchors.fill: parent
        focus: true

        seriesTitle: root.title
        seriesId: root.vaultId          // progress namespaces to the content id (resume is Slice 9)
        chapters: [ { "id": root.archivePath, "number": 1, "name": root.title } ]
        chapterId: root.archivePath      // store.localPages(chapterId) → the CBZ pages, in order
        chapterLabel: root.title
        entryKind: "manga"
        western: false
        pageStore: vaultStore
        // Slice 8 opens only; a Vault comic must not write into the catalogue reading history
        // yet (Open Recent + reopen semantics is Slice 9) — null the Progress sink like the preview.
        progress: null

        onMinimizeRequested: root.minimizeRequested()
        onCloseRequested: root.closeRequested()
        onBackRequested: root.backRequested()
        onFullscreenRequested: root.fullscreenRequested()
    }
}
