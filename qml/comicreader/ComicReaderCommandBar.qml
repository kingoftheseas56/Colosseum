// ComicReaderCommandBar — the ONE flat command layer over the book (Task 5, plan 2026-07-28).
//
// Hemanth approved this shape section by section and corrected an earlier draft of it personally:
//
//   "Cover's simplicity is not 'hide everything inside a modern drawer'. It is one shallow layer:
//    large, plainly named actions across the top; one unmistakable progress bar at the bottom; no
//    pill soup, no nested control architecture."
//
// So: ONE Row of plainly named actions. No pill backgrounds, no glass boxes, no segmented chips, no
// second level. Six commands, fixed order — Bookmark, Pages, Loupe, Image, the current Layout, the
// current Order. The last two are READOUTS as well as commands: they say what the book is doing
// right now, which is why their label and glyph are derived from shell state rather than fixed.
//
// GOLD IS SPARING and structural: only the command whose temporary surface is actually open wears
// it (plus Bookmark on a bookmarked page). Order never does — it is a direct toggle with no surface,
// and gold there would claim a panel is open when none is.
//
// PRESENTATION + INTENTS ONLY. This component owns no state and touches no core: it reads the
// shell's layout/order/activeOverlay/bookmark facts through plain properties and raises
// commandTriggered(name). The HUD turns that into semantic intents; the SHELL decides what opens.
// Everything below `trigger()` is pure and callable from the offscreen harness, so the tested logic
// is the shipped logic (the ComicReaderInput house pattern).

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: bar
    objectName: "readerCommandBar"

    // ---- shell facts, bound in by the HUD (never written back) ----
    property string layout: "long_strip"      // single_page | paired_pages | long_strip
    property string order: "ltr"              // ltr (comic) | rtl (manga)
    property string activeOverlay: ""         // "" | pages | image | layout | loupe
    property bool   bookmarked: false         // is the CURRENT page bookmarked

    // The approved six, in the approved order. Fixed, not configurable: this is the shallow layer,
    // and a seventh command is a design decision, not a runtime one.
    readonly property var commands: ["bookmark", "pages", "loupe", "image", "layout", "order"]

    signal commandTriggered(string command)

    implicitHeight: 46
    height: implicitHeight

    Theme { id: theme }

    // ================= pure command vocabulary =================
    // What each command is CALLED. Layout and Order render the live state, which is the whole point
    // of putting them in the bar: "The current layout name is a direct toolbar command."
    function labelFor(command) {
        switch (command) {
        case "bookmark": return "Bookmark"
        case "pages":    return "Pages"
        case "loupe":    return "Loupe"
        case "image":    return "Image"
        case "layout":   return layout === "single_page" ? "Single page"
                              : layout === "paired_pages" ? "Paired pages" : "Long strip"
        case "order":    return order === "rtl" ? "Manga order" : "Comic order"
        }
        return ""
    }

    // ...and which ComicReaderIcon glyph it wears (see ComicReaderIcon.fileForKind).
    function glyphFor(command) {
        switch (command) {
        case "bookmark": return "bookmark"
        case "pages":    return "pages"
        case "loupe":    return "loupe"
        case "image":    return "image"
        case "layout":   return layout === "single_page" ? "layoutSingle"
                              : layout === "paired_pages" ? "layoutPaired" : "layoutStrip"
        case "order":    return order === "rtl" ? "orderRtl" : "orderLtr"
        }
        return ""
    }

    // Gold, and only here. `order` is deliberately absent from both arms.
    function activeFor(command) {
        if (command === "bookmark") return bookmarked
        if (command === "pages" || command === "image" || command === "layout" || command === "loupe")
            return activeOverlay === command
        return false
    }

    // The one door. An unknown name is inert rather than falling through onto a neighbour.
    function trigger(command) {
        if (commands.indexOf(String(command)) < 0) return
        commandTriggered(String(command))
    }

    // The glyph inventory the semantic-icon-audit oracle enumerates (the HUD folds this into its
    // own iconKinds): every mark in this bar is a ComicReaderIcon, never a text character.
    readonly property var iconKinds: {
        var out = []
        for (var i = 0; i < commands.length; i++) out.push(glyphFor(commands[i]))
        return out
    }

    // ================= the flat row =================
    // Right-aligned, matching the approved mock. There is no left group and no sidebar to balance
    // against: the title strip owns the left edge, the comic owns everything below.
    Row {
        id: row
        anchors.right: parent.right
        anchors.rightMargin: 26
        anchors.verticalCenter: parent.verticalCenter
        spacing: 26

        Repeater {
            model: bar.commands
            delegate: Item {
                id: cmd
                required property string modelData
                readonly property bool on: bar.activeFor(modelData)
                // width/height come from the content: a command is its icon and its name, nothing else.
                width: cmdRow.implicitWidth
                height: 30

                Row {
                    id: cmdRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 9

                    ComicReaderIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        kind: bar.glyphFor(cmd.modelData)
                        accessibleName: bar.labelFor(cmd.modelData)
                        width: 18; height: 18
                        // NO pill, NO chip: the state lives in the ink, exactly as the approved mock
                        // renders it (idle near-white, active gold, hover full white).
                        ink: cmd.on ? theme.gold : (cmdMa.containsMouse ? theme.ink : theme.inkDim)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.labelFor(cmd.modelData)
                        color: cmd.on ? theme.gold : (cmdMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.hud
                        font.pixelSize: 14
                        font.bold: cmd.on
                    }
                }

                MouseArea {
                    id: cmdMa
                    anchors.fill: parent
                    // a comfortable target without drawing a box for it
                    anchors.topMargin: -8
                    anchors.bottomMargin: -8
                    anchors.leftMargin: -8
                    anchors.rightMargin: -8
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.trigger(cmd.modelData)
                }
            }
        }
    }
}
