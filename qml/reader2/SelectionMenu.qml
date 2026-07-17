// SelectionMenu.qml — the native glass popover that appears when you select text in the
// paper (TASK 9, Round 1). Like the rest of the reader2 chrome it is BRIDGE-FREE: it
// takes its data via properties (the selection rect) and reports actions via signals
// only, so ReaderShell (which owns the paper + the native stores) does the real work and
// this stays instantiable headless (chrome smoke).
//
// Round-1 content (Hemanth's ratified set): three highlight COLOR dots + a Copy button.
// Note / Define / footnotes are Round 2 — deliberately NOT wired here.
//
// Geometry: an anchors.fill overlay; the CARD is positioned by the pure
// Reader2Logic.selectionMenuPos() (centered on the selection, clamped in-frame, above
// else below). A transparent backdrop below the card dismisses on tap-outside; the card
// carries its OWN click-swallow MouseArea (house doctrine) so taps on it never fall
// through to the paper's double-click-toggle beneath.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: menu

    // ---- inputs ----
    // the paper 'selection' event's rect { x, y, w, h }, in this overlay's coordinates.
    property var sel: ({ x: 0, y: 0, w: 0, h: 0 })
    property bool shown: false

    // ---- signals up (ReaderShell wires these) ----
    signal colorPicked(string color)
    signal copyRequested()
    signal dismissed()

    visible: shown

    // The three Round-1 highlight colors (chrome mock .hl.c-gold/.c-slate/.c-moss). `value`
    // is the STORED/painted color: a plain 6-digit hex that reads correctly in BOTH the web
    // overlayer (which applies its own ~0.3 highlight opacity, so a solid hex paints as a
    // translucent wash) AND the QML Highlights-pane edge-rule (a solid 3px rule). `display`
    // is only the dot's on-menu fill (the mock's rgba at 0.85).
    readonly property var swatches: [
        { display: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.85), value: "#F0C24A" },  // gold
        { display: Qt.rgba(148 / 255, 166 / 255, 196 / 255, 0.85), value: "#94A6C4" }, // slate
        { display: Qt.rgba(151 / 255, 187 / 255, 152 / 255, 0.85), value: "#97BB98" }  // moss
    ]

    readonly property int cardH: 44
    readonly property int cardW: cardRow.implicitWidth + 28   // 14px padding each side
    readonly property var pos: L.selectionMenuPos(menu.sel, menu.width, menu.height,
                                                  menu.cardW, menu.cardH, 12, 10)

    // ---------- backdrop: tap-outside dismiss (below the card) ----------
    // Only armed while shown, so the paper owns all pointer input in the normal reading
    // state (the pointer rework); it only intercepts once a selection has opened the menu.
    MouseArea {
        anchors.fill: parent
        enabled: menu.shown
        acceptedButtons: Qt.LeftButton
        onClicked: menu.dismissed()
    }

    // ---------- the card ----------
    Rectangle {
        id: card
        x: menu.pos.x
        y: menu.pos.y
        width: menu.cardW
        height: menu.cardH
        radius: 12
        color: Theme.bar
        border.color: Theme.barBorder
        border.width: 1
        antialiasing: true

        // OWN click-swallow (house doctrine): presses/wheel on the card never reach the
        // paper (no stray double-click-toggle, no scroll leak). Buttons above still get
        // their clicks (child MouseAreas are declared after this and sit on top).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        Row {
            id: cardRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            spacing: 12

            // three color dots
            Repeater {
                model: menu.swatches
                delegate: Item {
                    id: dot
                    required property var modelData
                    width: 20
                    height: card.height
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
                    MouseArea {
                        id: dotMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: menu.colorPicked(String(dot.modelData.value))
                    }
                }
            }

            // divider (leaves visual room; Note/Define arrive in Round 2 to its right)
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: 20
                color: Theme.barBorder
            }

            // Copy — trivial + makes the menu feel real (ReaderShell copies via Clipboard)
            Item {
                width: copyText.implicitWidth + 8
                height: card.height
                Text {
                    id: copyText
                    anchors.centerIn: parent
                    text: "Copy"
                    font.family: Theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: copyMa.containsMouse ? Theme.ink : Theme.inkDim
                }
                MouseArea {
                    id: copyMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: menu.copyRequested()
                }
            }
        }
    }
}
