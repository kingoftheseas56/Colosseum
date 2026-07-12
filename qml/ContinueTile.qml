// ContinueTile — THE Continue tile. One component, two variants (spec: haven docs/superpowers/
// specs/2026-07-05-colosseum-continue-tiles-design.md · ratified mock
// agents/colosseum-continue-tiles-mock.html, rev 2):
//
//   "home"   340×148 landscape glass — badge + info column beside a 112px art slot.
//            Needs `backdrop` (+`track`) for the live Glass blur.
//   "world"  132×196 portrait cover — ALL metadata inside the tile over a bottom scrim
//            (Hemanth 2026-07-05: nothing floats outside a card).
//
// Shared everywhere: clamped gold progress, 46px resume circle, whole-tile hover (film + gold
// edge), hover-revealed ✕ remove, ✓ watched face, and the reliable cover pipeline — instant
// c1→c2 gradient beneath, decode-capped async cover fading in over 220ms, ONE retry on error,
// gradient persists on failure so a tile never renders blank or broken. AniList title-fallback
// runs for manga ONLY (coverless western comics keep the gradient — they were being searched
// on AniList as manga).
//
// Interaction contract (same as always): the circle RESUMES; anywhere else opens detail;
// the ✕ asks the row to forget the entry.

import QtQuick
import QtQuick.Effects
import "ContinueCovers.js" as ContinueCovers

Item {
    id: tile

    property string variant: "world"      // "home" | "world"
    property var entry: ({})              // Progress record: id/kind/title|caption/sub/cover/c1/c2/progress/watched
    property Item backdrop: null          // home only (Glass requires a backdrop)
    property real track: 0                // home only — scroll offset for the live blur

    signal resumeRequested()
    signal detailRequested()
    signal removeRequested()

    readonly property bool isHome: variant === "home"
    readonly property string kind: entry.kind !== undefined ? entry.kind : ""
    readonly property string label: (entry.title !== undefined && ("" + entry.title).length)
                                    ? entry.title : (entry.caption !== undefined ? entry.caption : "")
    readonly property string sub: entry.sub !== undefined ? entry.sub : ""
    // optional source tag (comics: GetComics) — shown in the badge when set
    readonly property string source: entry.source !== undefined ? ("" + entry.source) : ""
    readonly property real prog: Math.max(0, Math.min(1, entry.progress !== undefined ? Number(entry.progress) : 0))
    readonly property bool watched: entry.watched === true
    readonly property color c1: entry.c1 !== undefined ? entry.c1 : "#444"
    readonly property color c2: entry.c2 !== undefined ? entry.c2 : "#111"
    property string cover: (entry.cover !== undefined && ("" + entry.cover).length) ? entry.cover : ""

    width: isHome ? 340 : 132
    height: isHome ? 148 : 196

    Theme { id: theme }

    // cover fallback: manga only — a coverless comic keeps its gradient
    Component.onCompleted: if (!cover && kind === "manga")
        ContinueCovers.fetch(label, function(u) { tile.cover = u })

    function badgeFor(k) {
        return ({ video: "VIDEO", manga: "MANGA", comic: "COMIC", book: "BOOK" })[k]
               || (k ? k.toUpperCase() : "")
    }

    // ── the reliable cover: gradient instantly, cover fades over it, one retry, never blank ──
    component CoverArt: Item {
        property real decodeW: 264
        property real decodeH: 392
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: tile.c1 }
                GradientStop { position: 1; color: tile.c2 }
            }
        }
        Image {
            id: art
            property bool retried: false
            anchors.fill: parent
            source: tile.cover
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: parent.decodeW; sourceSize.height: parent.decodeH
            opacity: status === Image.Ready ? (tile.watched ? 0.72 : 1) : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
            onStatusChanged: if (status === Image.Error && !retried && tile.cover.length) {
                retried = true
                var s = tile.cover; source = ""; source = s   // one clean retry, then the gradient stays
            }
        }
    }

    // ═════════════════ HOME variant — landscape glass card ═════════════════
    Loader {
        anchors.fill: parent
        active: tile.isHome && tile.backdrop !== null
        sourceComponent: Glass { backdrop: tile.backdrop; track: tile.track; radius: 14 }
    }
    Item {
        visible: tile.isHome
        anchors.fill: parent
        // art slot — masked so the cover honors the card's 14px left corners (audit fix: Home art
        // was unclipped). Same mask pattern Glass uses; right edge extends past the mask radius
        // so only the outer corners round.
        Item {
            id: homeArtMask
            width: 112; height: tile.height
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; anchors.rightMargin: -14; radius: 14; color: "white" }
        }
        Item {
            width: 112; height: parent.height
            CoverArt { anchors.fill: parent; decodeW: 224; decodeH: 296 }
            layer.enabled: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: homeArtMask }
        }
        // info column — badge pinned top, title/sub/progress pinned bottom (no fragile spacers)
        Item {
            anchors.left: parent.left; anchors.leftMargin: 112 + 15
            anchors.right: parent.right; anchors.rightMargin: 15
            anchors.top: parent.top; anchors.topMargin: 14
            anchors.bottom: parent.bottom; anchors.bottomMargin: 12
            Text {
                anchors.top: parent.top
                text: tile.badgeFor(tile.kind) + (tile.source.length ? " · " + tile.source : "")
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.3
            }
            Column {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                Text {
                    width: parent.width
                    text: tile.label; color: theme.ink
                    font.family: theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold
                    elide: Text.ElideRight; maximumLineCount: 1
                }
                Text {
                    width: parent.width
                    text: tile.sub; color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 12
                    elide: Text.ElideRight; maximumLineCount: 1
                    topPadding: 4; bottomPadding: 8
                }
                Rectangle {
                    width: parent.width; height: 4; radius: 2; color: Qt.rgba(1, 1, 1, 0.2)
                    Rectangle { width: parent.width * tile.prog; height: parent.height; radius: 2; color: theme.gold }
                }
            }
        }
    }

    // ═════════════════ WORLD variant — portrait cover tile ═════════════════
    Rectangle {
        visible: !tile.isHome
        anchors.fill: parent
        radius: 12; clip: true
        color: "transparent"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
        CoverArt { anchors.fill: parent }
        // bottom scrim keeps the in-tile metadata readable over ANY cover art
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 78
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.4; color: Qt.rgba(0, 0, 0, 0.35) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.82) }
            }
        }
        Text {   // title — inside the tile
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 9; anchors.rightMargin: 9
            anchors.bottomMargin: tile.sub.length > 0 ? 26 : 12
            text: tile.label; color: "#ffffff"
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
            wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 2
            style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.85)
        }
        Text {   // chapter / episode / percent — inside the tile (Hemanth 2026-07-05)
            visible: tile.sub.length > 0
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 9; anchors.rightMargin: 9; anchors.bottomMargin: 11
            text: tile.sub; color: Qt.rgba(0.97, 0.97, 0.96, 0.78)
            font.family: theme.ui; font.pixelSize: 11
            elide: Text.ElideRight; maximumLineCount: 1
        }
        Rectangle {   // progress — full bottom edge
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 4; color: Qt.rgba(1, 1, 1, 0.2)
            Rectangle { width: parent.width * tile.prog; height: parent.height; color: theme.gold }
        }
    }

    // ── shared: whole-tile hover (film + gold edge) ──
    Rectangle {
        anchors.fill: parent
        radius: tile.isHome ? 14 : 12
        color: rootMa.containsMouse ? Qt.rgba(1, 1, 1, tile.isHome ? 0.05 : 0.10) : "transparent"
        border.width: tile.isHome ? 1 : 2
        border.color: rootMa.containsMouse ? (tile.isHome ? Qt.rgba(0.94, 0.77, 0.29, 0.55) : theme.gold)
                                           : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    MouseArea {   // anywhere on the tile → detail (resume/remove sit on top)
        id: rootMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.detailRequested()
    }

    // ── shared: ✓ watched face ──
    Rectangle {
        visible: tile.watched
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
        height: 20; radius: 10
        width: watchedRow.implicitWidth + 16
        color: Qt.rgba(0, 0, 0, 0.62)
        border.width: 1; border.color: Qt.rgba(0.94, 0.77, 0.29, 0.5)
        Row {
            id: watchedRow; anchors.centerIn: parent; spacing: 4
            Text { text: "✓"; color: theme.gold; font.pixelSize: 10; font.weight: Font.DemiBold }
            Text { visible: tile.isHome; text: "WATCHED"; color: theme.gold
                   font.family: theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
        }
    }

    // ── shared: 46px resume circle — centered on the art ──
    Rectangle {
        width: 46; height: 46; radius: 23
        x: tile.isHome ? 56 - width / 2 : (parent.width - width) / 2
        anchors.verticalCenter: parent.verticalCenter
        color: rbHov.hovered ? Qt.rgba(0, 0, 0, 0.80) : Qt.rgba(0, 0, 0, 0.55)
        border.width: 1.5; border.color: Qt.rgba(1, 1, 1, 0.9)
        scale: rbHov.hovered ? 1.08 : 1.0
        Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
        Image {
            anchors.centerIn: parent
            width: tile.kind === "video" ? 18 : 21; height: width
            source: tile.kind === "video" ? "../assets/icons/play.svg"
                  : tile.kind === "book"  ? "../assets/icons/books.svg"
                  : "../assets/icons/manga.svg"
            fillMode: Image.PreserveAspectFit
        }
        HoverHandler { id: rbHov }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tile.resumeRequested() }
    }

    // ── shared: hover-revealed ✕ remove ──
    Rectangle {
        id: removeBtn
        width: 24; height: 24; radius: 12
        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8
        color: rmMa.containsMouse ? Qt.rgba(0, 0, 0, 0.85) : Qt.rgba(0, 0, 0, 0.62)
        border.width: 1; border.color: theme.edge
        opacity: rootMa.containsMouse || rmMa.containsMouse ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "✕"
               color: rmMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 11 }
        MouseArea { id: rmMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: tile.removeRequested() }
        // tooltip (hand-rolled, same grammar as BackAction)
        Rectangle {
            visible: rmMa.containsMouse && rmTipDelay.done
            anchors.top: parent.bottom; anchors.topMargin: 7; anchors.right: parent.right
            width: rmTip.implicitWidth + 18; height: 24; radius: 6
            color: Qt.rgba(0.04, 0.04, 0.05, 0.92)
            border.width: 1; border.color: theme.edge
            Text { id: rmTip; anchors.centerIn: parent; text: "Remove from Continue"
                   color: theme.ink; font.family: theme.ui; font.pixelSize: 11 }
        }
        Timer { id: rmTipDelay; property bool done: false; interval: 550
                running: rmMa.containsMouse; onTriggered: done = true }
        Connections { target: rmMa; function onContainsMouseChanged() { if (!rmMa.containsMouse) rmTipDelay.done = false } }
    }

    Accessible.role: Accessible.Button
    Accessible.name: tile.label + (tile.sub.length ? " — " + tile.sub : "")
}
