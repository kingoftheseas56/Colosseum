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

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#050608" }
    Image {
        anchors.fill: parent
        source: root.banner.length ? root.banner : root.cover
        sourceSize: Qt.size(Math.ceil(width * 0.72), Math.ceil(height * 0.72))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true; cache: true
        opacity: status === Image.Ready ? 0.20 : 0.0
    }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: root.backdrop
        live: true; hideSource: false
        visible: root.backdrop !== null
        opacity: 0.12
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.015, 0.02, 0.032, 0.20) }
            GradientStop { position: 0.50; color: Qt.rgba(0.01, 0.012, 0.018, 0.52) }
            GradientStop { position: 1.0; color: Qt.rgba(0.005, 0.006, 0.01, 0.76) }
        }
    }

    Item {
        id: chrome
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 58

        BackAction {
            // World-namespaced automation reach (catalogue-independence Slice 3, 2026-08-20):
            // the Lanista scenario needs to leave a series page to drive a second one / prove
            // a reopen regression, and BackAction carries no objectName of its own.
            objectName: "tankobanReadingRoomBack"
            x: theme.margin; anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Back to series"
            Keys.onReturnPressed: root.backRequested()
            Keys.onEnterPressed: root.backRequested()
            onTriggered: root.backRequested()
        }
        Text {
            anchors.centerIn: parent
            text: "COLOSSEUM · TANKOBAN"
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: theme.margin
            anchors.verticalCenter: parent.verticalCenter; spacing: 12
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Minimize window"
                Keys.onReturnPressed: root.minimizeRequested(); Keys.onEnterPressed: root.minimizeRequested()
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/minimize.svg"; opacity: minMouse.containsMouse ? 1 : 0.72 }
                MouseArea { id: minMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Enter full screen"
                Keys.onReturnPressed: root.fullscreenRequested(); Keys.onEnterPressed: root.fullscreenRequested()
                Image {
                    anchors.centerIn: parent; width: 20; height: 20
                    source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                    opacity: fullscreenMouse.containsMouse ? 1 : 0.72
                }
                MouseArea { id: fullscreenMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
            }
            Item {
                width: 36; height: 36; activeFocusOnTab: true
                Accessible.role: Accessible.Button; Accessible.name: "Close series view"
                Keys.onReturnPressed: root.closeRequested(); Keys.onEnterPressed: root.closeRequested()
                Image { anchors.centerIn: parent; width: 20; height: 20; source: "../assets/icons/power.svg"; opacity: closeMouse.containsMouse ? 1 : 0.72 }
                MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
            }
        }
    }

    // Un-boxed (POLISH-DELTA #11): identity floats over the wallpaper, exactly like the series
    // page's own masthead — no card, no fill, no border behind it.
    Item {
        id: storyMasthead
        anchors.top: chrome.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        readonly property int baseHeight: root.height < 760 ? 118 : 104
        // Grows only for the synopsis block (2-line clamp + its own MORE/LESS chip line).
        // Everything else in the strip keeps its original fixed geometry.
        height: root.synopsis.length > 0 ? baseHeight + synopsisBlock.height + 8 : baseHeight

        Item {
            id: storyIdentity
            anchors.left: parent.left; anchors.right: storyActions.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.leftMargin: 0; anchors.topMargin: 15; anchors.bottomMargin: 14
            Text {
                id: storyTitle
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.rightMargin: 20
                text: root.seriesTitle; color: theme.ink; font.family: theme.display; font.pixelSize: 30; font.weight: Font.DemiBold
                elide: Text.ElideRight; maximumLineCount: 1
            }
            Row {
                id: metaRow
                anchors.top: parent.top; anchors.topMargin: storyTitle.implicitHeight + 12; anchors.left: parent.left; spacing: 7
                Text { visible: root.author.length > 0; text: root.author; color: theme.ink; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                Text { visible: root.author.length > 0 && root.status.length > 0; text: "·"; color: theme.inkDimmer }
                Text { visible: root.status.length > 0; text: root.status; color: theme.inkDim }
                Text { visible: root.year > 0; text: "· " + root.year; color: theme.inkDim }
                Image { visible: root.score > 0; source: "../assets/icons/rating-star.svg"; width: 12; height: 12 }
                Text { visible: root.score > 0; text: root.score.toFixed(1); color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold }
                // Anime-Planet stat line: volumes promoted to full ink (mock .meta .stat). No
                // chapter count in v2.3 — chapters are not a Tankoban concept here.
                Text {
                    visible: root.library.showVolumes && (root.author.length > 0 || root.status.length > 0 || root.year > 0 || root.score > 0)
                    text: "·"; color: theme.inkDimmer
                }
                Text {
                    visible: root.library.showVolumes
                    text: root.library.volumeRows.length + " volumes"; color: theme.ink; font.family: theme.ui; font.pixelSize: 11
                }
            }
            // Synopsis (ruling #13/#15): a real 2-line clamp with proper line height, and a small
            // glass MORE/LESS chip on its OWN line beneath the text — never dangling off the last
            // sentence, and only shown when there is actually more to reveal.
            Item {
                id: synopsisBlock
                anchors.top: metaRow.bottom; anchors.topMargin: 10
                anchors.left: parent.left; anchors.right: parent.right; anchors.rightMargin: 20
                visible: root.synopsis.length > 0
                height: visible ? (synopsisText.height + (moreChip.visible ? moreChip.height + 10 : 0)) : 0

                Text {
                    id: synopsisText
                    width: parent.width
                    text: root.synopsis
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14
                    lineHeight: 1.5
                    wrapMode: Text.WordWrap
                    maximumLineCount: root.synopsisExpanded ? 100000 : 2
                    elide: Text.ElideRight
                }
                Rectangle {
                    id: moreChip
                    objectName: "synopsisMoreChip"
                    anchors.top: synopsisText.bottom; anchors.topMargin: 10
                    // Only a truncated (or already-expanded) synopsis earns the control — a short
                    // synopsis that fits in two lines never grows a dead MORE chip.
                    visible: root.synopsisExpanded || synopsisText.truncated
                    width: moreLabel.implicitWidth + 24; height: 22; radius: 11
                    color: moreMa.containsMouse ? theme.glassHi : theme.glassTint
                    border.width: 1
                    border.color: moreMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.45) : theme.edge
                    Text {
                        id: moreLabel
                        anchors.centerIn: parent
                        text: root.synopsisExpanded ? "LESS" : "MORE"
                        color: moreMa.containsMouse ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.6
                    }
                    MouseArea {
                        id: moreMa
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.synopsisExpanded = !root.synopsisExpanded
                    }
                }
            }
        }

        // The masthead's own action surface. "+Library" is always the compact masthead
        // action per the v1 contract. The Get/Read/Retry/progress verb is NOT duplicated
        // here when the flow below can carry it (root.library.showVolumes true) — the flow's
        // own action bar is the one contextual action in that case. When the series has no
        // known shelf at all (primaryAction "search"), the flow renders nothing and reserves
        // no action bar, so this is the ONLY place the one contextual action can live —
        // dropping it here would leave a shelf-less series with zero ways to search nyaa.
        Item {
            id: storyActions
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: 20
            anchors.topMargin: 15
            anchors.bottomMargin: 14
            width: actionsRow.implicitWidth

            Row {
                id: actionsRow
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Rectangle {
                    // World-namespaced automation reach (catalogue-independence Slice 4,
                    // 2026-08-20): the Lanista scenario needs to press the honest
                    // search action on a shelf-less series without knowing its label text.
                    objectName: "tankobanSeriesPrimaryAction"
                    visible: !root.library.showVolumes
                    width: visible ? primaryLabel.implicitWidth + 36 : 0
                    height: 42; radius: 11; color: theme.gold
                    activeFocusOnTab: visible
                    Accessible.role: Accessible.Button; Accessible.name: root.continueText
                    Keys.onReturnPressed: root.primaryRequested(); Keys.onEnterPressed: root.primaryRequested()
                    Row { id: primaryLabel; anchors.centerIn: parent; spacing: 8
                        Image { source: "../assets/icons/play-dark.svg"; width: 14; height: 14; visible: root.primaryAction !== "search"; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: root.continueText; color: "#171205"; font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.primaryRequested() }
                }

                Loader {
                    anchors.verticalCenter: parent.verticalCenter
                    active: root.collectionEntry !== null && typeof Collection !== "undefined"
                    sourceComponent: LibraryButton { world: "tankoban"; entry: root.collectionEntry }
                }
            }
        }
    }

    Item {
        id: collectionSurface
        anchors.top: storyMasthead.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 0; anchors.rightMargin: 0; anchors.topMargin: 10; anchors.bottomMargin: 0
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
