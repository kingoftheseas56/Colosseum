// ContinueSeeAllPage — the Continue backlog, whole (spec: haven docs/superpowers/specs/
// 2026-07-11-colosseum-continue-see-all-design.md · mock option A ratified 2026-07-11).
// One page, four doors: every Continue row's "See all ›" opens it scoped to that row —
// home ("", + medium chips), Theatre (video), Tankoban (manga+comic), Biblio (book).
// One grid + exclusive sort chips: Last Watched · A–Z · Z–A · Watched · Not Watched.
// ("Most Watched" returns only when the store grows a real watch counter — ratified.)
// Tiles are the SAME ContinueTile (circle resumes, tile opens detail, hover ✕ forgets);
// chip logic is pure ContinueSeeAll.js (headless-tested).
import QtQuick
import QtQuick.Controls
import "ContinueSeeAll.js" as SeeAll

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors the genre-page layers)
    property Item backdrop: null
    property string scope: "home"        // "home" | "video" | "tankoban" | "book"
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal resumeRequested(var item)     // → win.resumeContinue (same sink as the rows)
    signal detailRequested(var item)     // → win.detailContinue

    Theme { id: theme }

    readonly property string pageTitle: scope === "video" ? "Continue Watching"
                                      : scope === "home"  ? "Continue"
                                      : "Continue Reading"
    readonly property string kicker: scope === "video"    ? "THEATRE · CONTINUE"
                                   : scope === "tankoban" ? "TANKOBAN · CONTINUE"
                                   : scope === "book"     ? "BIBLIO · CONTINUE"
                                   : "EVERYTHING · CONTINUE"
    readonly property bool hasMediumChips: scope === "home"

    // chip state — page-local, resets per open (onScopeChanged) per spec
    property string sortKey: "recent"
    property string mediumKey: ""

    // raw uncapped pull; Progress is a shell context property (absent in the load
    // harness — the typeof guard keeps headless creation warning-free)
    property var rawItems: []
    function refresh() {
        if (typeof Progress === "undefined") { rawItems = []; return }
        if (scope === "video")         rawItems = Progress.recent("video", 0)
        else if (scope === "book")     rawItems = Progress.recent("book", 0)
        else if (scope === "tankoban") rawItems = Progress.recent("manga", 0).concat(Progress.recent("comic", 0))
        // all-scope: 'audiobook' records are resume positions for the reader's read-along,
        // never tiles — the book's own tile represents both (Hemanth 2026-07-18).
        else                           rawItems = Progress.recent("", 0).filter(function(e) { return e.kind !== "audiobook" })
    }
    // naming Progress.revision keeps removals/new progress live (house pattern)
    property int progressRevision: (typeof Progress !== "undefined") ? Progress.revision : 0
    onProgressRevisionChanged: refresh()
    onScopeChanged: { sortKey = "recent"; mediumKey = ""; refresh() }
    Component.onCompleted: refresh()

    readonly property var shownItems: SeeAll.apply(rawItems, sortKey, mediumKey)

    // ---- the page's own wallpaper (a layer over the shell) ----
    Item {
        id: wall
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    // one chip — text pill, gold when on (the house accent, sparing)
    component Chip: Rectangle {
        id: chip
        property string label
        property bool on: false
        signal picked()
        height: 30; radius: 15
        width: chipText.implicitWidth + 28
        color: on ? Qt.rgba(0.94, 0.77, 0.29, 0.10) : Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: on ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : Qt.rgba(1, 1, 1, 0.16)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? theme.gold : (chipMa.containsMouse ? theme.ink : theme.inkDim)
            font.family: theme.ui; font.pixelSize: 13
            font.weight: chip.on ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            id: chipMa; anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: chip.picked()
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 50
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header (the genre-directory treatment) ----
            Item {
                width: parent.width; height: 40
                Row {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    Text {
                        text: "‹"
                        color: backMa.containsMouse ? theme.gold : theme.ink
                        font.family: theme.display; font.pixelSize: 30
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            id: backMa
                            anchors.fill: parent; anchors.margins: -10
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.backRequested()
                        }
                    }
                    Text { text: root.kicker; color: theme.inkDimmer
                           font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6
                           font.weight: Font.DemiBold
                           anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Text { text: root.pageTitle; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14; textFormat: Text.StyledText
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: "<b><font color='#f7f7f5'>" + root.shownItems.length + "</font></b> "
                      + (root.shownItems.length === 1 ? "title" : "titles")
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            // ---- sort chips (exclusive) ----
            Flow {
                width: parent.width
                topPadding: 26
                spacing: 10
                Repeater {
                    model: [
                        { label: "Last Watched", key: "recent" },
                        { label: "A–Z",          key: "az" },
                        { label: "Z–A",          key: "za" },
                        { label: "Watched",      key: "watched" },
                        { label: "Not Watched",  key: "unwatched" }
                    ]
                    delegate: Chip {
                        required property var modelData
                        label: modelData.label
                        on: root.sortKey === modelData.key
                        onPicked: root.sortKey = modelData.key
                    }
                }
            }

            // ---- medium chips (home scope only, exclusive) ----
            Flow {
                width: parent.width
                visible: root.hasMediumChips
                topPadding: root.hasMediumChips ? 12 : 0
                spacing: 10
                Repeater {
                    model: [
                        { label: "All",    key: "" },
                        { label: "Video",  key: "video" },
                        { label: "Manga",  key: "manga" },
                        { label: "Comics", key: "comic" },
                        { label: "Books",  key: "book" }
                    ]
                    delegate: Chip {
                        required property var modelData
                        label: modelData.label
                        on: root.mediumKey === modelData.key
                        onPicked: root.mediumKey = modelData.key
                    }
                }
            }

            Item { width: 1; height: 26 }

            // ---- the grid ----
            Grid {
                id: grid
                width: parent.width
                // 148px world tiles + 20px gap (harmonized to the catalogue gallery poster)
                columns: Math.max(4, Math.floor(width / 168))
                columnSpacing: 20; rowSpacing: 22
                Repeater {
                    model: root.shownItems
                    delegate: ContinueTile {
                        required property var modelData
                        variant: "world"
                        entry: modelData
                        onResumeRequested: root.resumeRequested(modelData)
                        onDetailRequested: root.detailRequested(modelData)
                        onRemoveRequested: if (typeof Progress !== "undefined")
                                               Progress.forget(modelData.kind, modelData.id)
                    }
                }
            }

            // ---- empty states (never a silent blank) ----
            Text {
                topPadding: 30
                visible: root.shownItems.length === 0
                text: root.rawItems.length === 0 ? "Nothing to continue."
                                                 : "Nothing here yet."
                color: theme.inkDim
                font.family: theme.display; font.italic: true; font.pixelSize: 19
            }
        }
    }

    // window chrome (fullscreen rule removed 2026-07-20): the canonical
    // minimize · fullscreen-toggle · power cluster every page carries.
    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: chromeMinMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: chromeMinMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.minimizeRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.fullscreenRequested()
            }
        }
        Item {
            width: 22
            height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22
                sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: chromePowMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: chromePowMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.closeRequested()
            }
        }
    }
}
