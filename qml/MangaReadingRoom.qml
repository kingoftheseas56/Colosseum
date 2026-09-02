// MangaReadingRoom - the compact series masthead above the Pages/Flow volume continuum.
//
// v2.3 adoption (arc-08, 2026-08-21, re-derived against the LANDED catalogue-independence
// tree). Governing docs, in force order: POLISH-DELTA.md over DESIGN-CONTRACT.md, against the
// approved v2.3 oracle reference/visual/colosseum-manga-series-volume-flow-mock-v2.html
// (Preflight arc-08). Eyes-on verdict: "perfect" (Hemanth, 2026-08-20).
//
// The series context is compressed into a story masthead. The collection owns the rest of the
// screen so the reader-derived volume flow can lead.
//
// Reconciled against LIVE drift the arc's own candidate could not see (it was briefed against
// "post-Slice-2", but Slice 4's truthful three-way primary action landed independently):
//   - `primaryAction`/`continueText` keep the LIVE three-way truth table ("open"/"get"/
//     "search", catalogue-independence Slice 4) rather than the candidate's stale two-way
//     stand-in. `readPrimary()` routing stays owned by MangaSeries.qml, unchanged.
//   - `chapters`/`openChapterRequested`/`chapterDownloadRequested` do not exist on this
//     component at all — catalogue-independence Slice 5 already deleted the chapter surface
//     wholesale, and MangaSeries.qml no longer binds them onto this instance. There is nothing
//     here for the arc candidate's "kept declared, unemitted, for MangaSeries.qml compat" note
//     to apply to.
//   - the masthead's single contextual action is split by the shelf's own truth, not removed
//     outright: when the series HAS a known shelf, the "Get / Read / Retry / progress" action
//     lives on the flow's own action bar (root.library's currentActionLabel/activateCurrent) —
//     the masthead carries no second, redundant CTA. When the series has NO known shelf at all
//     (primaryAction === "search"), the flow renders zero rows and reserves no action bar
//     (showVolumes false), so the masthead keeps the one honest action button
//     (`tankobanSeriesPrimaryAction`) as the ONLY way to reach it — "one contextual action"
//     never becomes zero. The committed tankoban-catalogue-smoke scenario exercises exactly
//     this split: it presses `tankobanVolumeCard_1` for a shelved series (One Piece) and
//     `tankobanSeriesPrimaryAction` for a shelf-less one (Berserk).
//   - `tankobanReadingRoomBack` stays on BackAction (catalogue-independence Slice 3 naming
//     law) — the committed scenarios navigate away from a series page through it.
import QtQuick

Item {
    id: root
    objectName: "mangaReadingRoom"

    property Item backdrop
    property string seriesId: ""
    property string seriesTitle: ""
    // The catalogue identity threaded through from MangaSeries (Slice 2, amended
    // 2026-08-20) — "" when unresolved. Not read by this file's own visual tree yet
    // (the masthead binds seriesTitle/author/etc directly); carried so a future caller
    // that needs the numeric identity here does not need a new property added later.
    property string malId: ""
    // The truthful primary-button verdict from MangaSeries.qml (Slice 2's truth-table,
    // completed by catalogue-independence Slice 4, 2026-08-20): "open" (volume 1 ready) /
    // "get" (shelf present, not yet downloaded) / "search" (no known shelf — series-level
    // nyaa search). Drives continueText below and whether the masthead's own action button
    // shows at all (see storyActions); the actual click routing lives in MangaSeries.qml's
    // readPrimary().
    property string primaryAction: "open"
    property string banner: ""
    property string cover: ""
    property string author: ""
    property string status: ""
    property int year: 0
    property string synopsis: ""
    property string errorText: ""
    property var genres: []
    property real score: 0
    property var collectionEntry: null
    property var service: null
    property var progress: null
    property var downloader: null
    // Arc 19: page-owned foreground intent mirrored into the volume action bar.
    property string pendingReadVolumeId: ""
    property bool synopsisExpanded: false
    readonly property real contentHeight: root.height

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal primaryRequested()
    signal readVolumeRequested(string volumeId)
    signal sourcesRequested(var context)
    signal batchRequested(var numbers, string label)
    signal chapterModeRequested()

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#050608" }

    MangaSeriesSharedHeader {
        id: sharedHeader
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        tankobanMode: true
        seriesTitle: root.seriesTitle
        banner: root.banner
        cover: root.cover
        author: root.author
        status: root.status
        year: root.year
        score: root.score
        synopsis: root.synopsis
        collectionEntry: root.collectionEntry
        onBackRequested: root.backRequested()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onCloseRequested: root.closeRequested()
        onChapterRequested: root.chapterModeRequested()
    }

    Item {
        id: collectionSurface
        anchors.top: sharedHeader.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 0; anchors.rightMargin: 0; anchors.topMargin: 0; anchors.bottomMargin: 0
        MangaTankobanLibrary {
            id: tankLib
            objectName: "readingRoomLibrary"
            anchors.fill: parent
            seriesId: root.seriesId; seriesTitle: root.seriesTitle
            service: root.service; progress: root.progress; downloader: root.downloader
            pendingReadVolumeId: root.pendingReadVolumeId
            onReadVolumeRequested: (volumeId) => root.readVolumeRequested(volumeId)
            onSourcesRequested: (ctx) => root.sourcesRequested(ctx)
            onBatchRequested: (numbers, label) => root.batchRequested(numbers, label)
        }
        Rectangle {
            visible: root.errorText.length > 0; z: 30
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 34
            color: Qt.rgba(0.08, 0.04, 0.05, 0.92)
            Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter
                text: root.errorText; color: "#e6a3a3"; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight; maximumLineCount: 1 }
        }
    }

    property alias library: tankLib
    readonly property string continueText: {
        if (root.library.continueVolumeId.length) {
            var n = root.library.currentNumber
            if (root.library.continueMax > 0)
                return "Continue · Vol. " + n + " · p. " + root.library.continuePage
            return "Continue · Vol. " + n
        }
        // The truthful three-way label (Slice 2's promise, completed Slice 4, 2026-08-20):
        // "open"/"get" both imply a known shelf (library.showVolumes); "search" is the
        // shelf-less honest fallback. Chapters are gone entirely (catalogue-independence
        // Slice 5, 2026-08-20) so the only unmodeled case left is a not-yet-resolved page,
        // where "Open volume 1" is still the honest eventual promise.
        if (root.primaryAction === "get") return "Read volume 1"
        if (root.primaryAction === "search") return "Search nyaa"
        return "Open volume 1"
    }
}
