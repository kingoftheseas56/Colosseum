// SelectionMenu.qml â€” the native glass popover for the paper's pen (TASK 9).
//
// Round 1 shipped: 3 highlight COLOR dots + Copy, on a live text selection. Round 2 adds
// the rest of the mock's set â€” Note + Define â€” plus a second MODE for tapping an EXISTING
// highlight (Delete, with optional re-color). Like the rest of the reader2 chrome it is
// BRIDGE-FREE: it takes its data via properties (the selection/highlight rect) and reports
// actions via signals only, so ReaderShell (which owns the paper + native stores) does the
// real work and this stays instantiable headless (chrome smoke).
//
// Two modes (menu.mode):
//   "select"   â€” a fresh text selection: color dots Â· Note Â· Define Â· Copy   (the mock)
//   "existing" â€” a tapped highlight:     color dots (re-color) Â· Delete
//
// The Note action expands the card in place into a small glass note editor (a native
// TextEdit â€” no QtQuick.Controls dependency); Save emits noteSaved(text). Esc / tap-outside
// cancels the editor (â†’ dismissed()).
//
// Geometry: an anchors.fill overlay; the CARD is positioned by the pure
// Reader2Logic.selectionMenuPos() (centered on the selection, clamped in-frame, above else
// below). A transparent backdrop below the card dismisses on tap-outside; the card carries
// its OWN click-swallow MouseArea (house doctrine) so taps on it never fall through to the
// paper's double-click-toggle beneath.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import QtQuick.Window
import "Reader2Logic.js" as L

ReaderOverlay {
    overlayActive: shown
    nativeWindow: true
    id: menu

    // ---- inputs ----
    // the paper 'selection' / 'highlightTapped' rect { x, y, w, h }, in overlay coordinates.
    property var sel: ({ x: 0, y: 0, w: 0, h: 0 })
    property bool shown: false
    property bool androidHost: Qt.platform.os === "android"
    readonly property bool phoneLayout: androidHost && Math.min(width, height) < 600
    property var inputMethod: Qt.inputMethod
    readonly property real keyboardOverlap: {
        if (!phoneLayout || !noteEditing || !inputMethod || !inputMethod.visible) return 0
        const keyboard = inputMethod.keyboardRectangle
        if (!keyboard || keyboard.height <= 0) return 0
        const top = menu.mapFromItem(null, 0, keyboard.y).y
        return Math.min(height, Math.max(0, height - top))
    }
    readonly property real availableHeight: Math.max(0, height - keyboardOverlap)
    // "select" (fresh selection) | "existing" (a tapped highlight). Drives which actions show.
    property string mode: "select"

    // ---- signals up (ReaderShell wires these) ----
    signal colorPicked(string color)   // a dot: highlight-in-color (select) OR re-color (existing)
    signal copyRequested()
    signal defineRequested()           // Define â†’ ReaderShell extracts the word + looks it up
    signal noteSaved(string note)      // Note editor Save â†’ ReaderShell writes the annotation
    signal deleteRequested()           // existing-mode Delete â†’ ReaderShell removes the highlight
    signal dismissed()

    visible: shown

    // Reset the note editor whenever the menu opens/closes or its anchor changes, so a fresh
    // selection never inherits the previous note draft or a half-open editor.
    property bool noteEditing: false
    function focusBelongsHere(item) {
        var p = item
        while (p) { if (p === menu) return true; p = p.parent }
        return false
    }
    function trapTab(event) {
        var tab = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
        if (!shown || !tab) return false
        var backwards = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier)
        var w = menu.Window.window
        var current = w ? w.activeFocusItem : null
        var next = current
        for (var i = 0; i < 128; ++i) {
            if (!next || !focusBelongsHere(next)) {
                var first = colorRepeater.itemAt(0); if (first) first.focusKeyboard()
                event.accepted = true; return true
            }
            next = next.nextItemInFocusChain(!backwards)
            if (!next || next === current) break
            if (focusBelongsHere(next) && next.activeFocusOnTab && next.visible && next.enabled) {
                next.forceActiveFocus(Qt.OtherFocusReason); event.accepted = true; return true
            }
        }
        event.accepted = true; return true
    }
    onShownChanged: {
        noteEditing = false; noteInput.text = ""
        if (shown) Qt.callLater(function() {
            var first = colorRepeater.itemAt(0)
            if (first) first.focusKeyboard()
        })
    }
    Keys.onPressed: function(event) {
        if (menu.trapTab(event)) return
        if (shown && event.key === Qt.Key_Escape) { menu.dismissed(); event.accepted = true }
    }
    onSelChanged: noteEditing = false
    onNoteEditingChanged: if (noteEditing) Qt.callLater(function () { noteInput.forceActiveFocus() })

    // The three highlight colors (chrome mock .hl.c-gold/.c-slate/.c-moss). `value` is the
    // STORED/painted color (plain 6-digit hex â†’ the web overlayer applies its own ~0.3 wash,
    // the QML Highlights pane a solid rule); `display` is only the dot's on-menu fill.
    readonly property var swatches: [
        { display: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.85), value: "#F0C24A" },  // gold
        { display: Qt.rgba(148 / 255, 166 / 255, 196 / 255, 0.85), value: "#94A6C4" }, // slate
        { display: Qt.rgba(151 / 255, 187 / 255, 152 / 255, 0.85), value: "#97BB98" }  // moss
    ]

    readonly property int cardH: noteEditing
        ? (phoneLayout ? Math.min(180, Math.max(0, availableHeight - 20)) : 128)
        : (phoneLayout ? 124 : 44)
    readonly property int cardW: Math.min(Math.max(0, width - 24),
        phoneLayout ? 330 : (noteEditing ? 300 : cardRow.implicitWidth + 28))
    readonly property var pos: L.selectionMenuPos(menu.sel, menu.width, menu.availableHeight,
                                                  menu.cardW, menu.cardH, 12, 10)

    // ---------- backdrop: tap-outside dismiss (below the card) ----------
    // Only armed while shown, so the paper owns all pointer input in the normal reading state;
    // it only intercepts once a selection / highlight-tap has opened the menu.
    ReaderKeyboardArea {
        anchors.fill: parent
        enabled: menu.shown
        acceptedButtons: Qt.LeftButton
        onClicked: menu.dismissed()
    }

    // ---------- the card ----------
    Rectangle {
        id: card
        objectName: "reader2SelectionCard"
        x: menu.pos.x
        y: menu.pos.y
        width: menu.cardW
        height: menu.cardH
        radius: 12
        color: menu.phoneLayout ? Qt.rgba(13 / 255, 13 / 255, 16 / 255, 0.985) : Theme.bar
        border.color: Theme.barBorder
        border.width: 1
        antialiasing: true

        // OWN click-swallow (house doctrine): presses/wheel on the card never reach the paper.
        // Child MouseAreas (declared after this) sit on top and still get their clicks.
        ReaderKeyboardArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // ===== action row (both modes; hidden while the note editor is open) =====
        Item {
            id: cardRow
            visible: !menu.noteEditing
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            implicitWidth: colorActions.implicitWidth + textActions.implicitWidth + 25
            width: menu.phoneLayout ? parent.width - 28 : implicitWidth
            height: menu.phoneLayout ? 104 : 44
            Row {
                id: colorActions
                x: menu.phoneLayout ? (cardRow.width - width) / 2 : 0
                height: menu.phoneLayout ? 48 : 44
                spacing: 12
            Repeater {
                id: colorRepeater
                model: menu.swatches
                delegate: Item {
                    id: dot
                    required property var modelData
                    required property int index
                    width: menu.phoneLayout ? 48 : 20
                    height: menu.phoneLayout ? 48 : card.height
                    function focusKeyboard() { dotMa.forceActiveFocus(Qt.OtherFocusReason) }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        radius: 9
                        color: dot.modelData.display
                        border.width: dotMa.containsMouse ? 2 : 0
                        border.color: Theme.ink
                        antialiasing: true
                    }
                    ReaderKeyboardArea {
                        id: dotMa
                        objectName: dot.index === 0 ? "reader2SelectionFirstColor" : ""
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        keyboardLabel: "Highlight color"
                        onClicked: menu.colorPicked(String(dot.modelData.value))
                    }
                }
            }

            }
            Rectangle {
                visible: !menu.phoneLayout
                x: colorActions.width + 12
                anchors.verticalCenter: parent.verticalCenter
                width: 1; height: 20; color: Theme.barBorder
            }
            Row {
                id: textActions
                x: menu.phoneLayout ? 0 : colorActions.width + 25
                y: menu.phoneLayout ? 56 : 0
                height: menu.phoneLayout ? 48 : 44
                spacing: 12
            // ---- select-mode text actions: Note Â· Define Â· Copy ----
            TextAction {
                objectName: "reader2SelectionNote"
                width: menu.phoneLayout ? (menu.mode === "existing" ? cardRow.width : (cardRow.width - 24) / 3) : implicitWidth
                btnHeight: menu.phoneLayout ? 48 : 44
                label: "Note"
                visible: menu.mode === "select"
                onTriggered: menu.noteEditing = true
            }
            TextAction {
                objectName: "reader2SelectionDefine"
                width: menu.phoneLayout ? (menu.mode === "existing" ? cardRow.width : (cardRow.width - 24) / 3) : implicitWidth
                btnHeight: menu.phoneLayout ? 48 : 44
                label: "Define"
                visible: menu.mode === "select"
                onTriggered: menu.defineRequested()
            }
            TextAction {
                objectName: "reader2SelectionCopy"
                width: menu.phoneLayout ? (menu.mode === "existing" ? cardRow.width : (cardRow.width - 24) / 3) : implicitWidth
                btnHeight: menu.phoneLayout ? 48 : 44
                label: "Copy"
                visible: menu.mode === "select"
                onTriggered: menu.copyRequested()
            }

            // ---- existing-mode action: Delete ----
            TextAction {
                width: menu.phoneLayout ? (menu.mode === "existing" ? cardRow.width : (cardRow.width - 24) / 3) : implicitWidth
                btnHeight: menu.phoneLayout ? 48 : 44
                label: "Delete"
                visible: menu.mode === "existing"
                onTriggered: menu.deleteRequested()
            }            }
        }

        // ===== note editor (select mode only; opened by Note) =====
        Item {
            id: noteEditor
            visible: menu.noteEditing
            anchors.fill: parent
            anchors.margins: 10

            // glass field holding the editable note text.
            Rectangle {
                id: noteField
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Math.max(0, parent.height - (menu.phoneLayout ? 58 : 34))
                radius: 8
                color: Qt.rgba(1, 1, 1, 0.05)
                border.color: Theme.barBorder
                border.width: 1

                Flickable {
                    id: noteFlick
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    contentWidth: width
                    contentHeight: noteInput.implicitHeight
                    interactive: contentHeight > height

                    TextEdit {
                        id: noteInput
                        objectName: "reader2SelectionNoteInput"
                        width: noteFlick.width
                        wrapMode: TextEdit.Wrap
                        font.family: Theme.ui
                        font.pixelSize: 13
                        color: Theme.ink
                        selectionColor: Theme.gold
                        selectByMouse: true
                        // Tab stays inside the popover; every other editing key keeps TextEdit precedence.
                        Keys.onPressed: function(event) {
                            if (menu.trapTab(event)) return
                            if (event.key === Qt.Key_Escape) { menu.dismissed(); event.accepted = true }
                        }
                    }
                }

                // placeholder while empty
                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 8
                    visible: noteInput.text.length === 0
                    text: "Add a noteâ€¦"
                    font.family: Theme.ui
                    font.pixelSize: 13
                    color: Theme.inkFaint
                }
            }

            // Cancel Â· Save
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 16
                TextAction {
                    label: "Cancel"
                    width: menu.phoneLayout ? 64 : implicitWidth
                    btnHeight: menu.phoneLayout ? 48 : 24
                    onTriggered: menu.dismissed()
                }
                Item {
                    width: menu.phoneLayout ? 64 : saveText.implicitWidth + 8
                    height: menu.phoneLayout ? 48 : 24
                    readonly property bool canSave: noteInput.text.trim().length > 0
                    Text {
                        id: saveText
                        anchors.centerIn: parent
                        text: "Save"
                        font.family: Theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: parent.canSave ? (saveMa.containsMouse ? Theme.gold : Theme.ink)
                                              : Theme.inkGhost
                    }
                    ReaderKeyboardArea {
                        id: saveMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: parent.canSave ? Qt.PointingHandCursor : Qt.ArrowCursor
                        keyboardLabel: "Save note"
                        onClicked: if (parent.canSave) menu.noteSaved(noteInput.text.trim())
                    }
                }
            }
        }
    }

    // A small text button used in both the action row and the editor's Cancel/Save.
    component TextAction: Item {
        property alias label: t.text
        property int btnHeight: 44
        signal triggered()
        implicitWidth: t.implicitWidth + 8
        width: implicitWidth
        height: btnHeight
        Text {
            id: t
            anchors.centerIn: parent
            font.family: Theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: ma.containsMouse ? Theme.ink : Theme.inkDim
        }
        ReaderKeyboardArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            keyboardLabel: parent.label
            onClicked: parent.triggered()
        }
    }
}
