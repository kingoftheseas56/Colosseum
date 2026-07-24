// ComicReaderSettingsSheet — the reader's glass side sheet (Task 12, mockup surface 02). Slides from
// the right over a dimmed page; sectioned in the player's letter-spaced label voice, gold marking
// ONLY the active choice. "Lineage layout, player soul": glass-deep panel, Segoe UI chrome, sparing
// gold. Opens from the HUD settings pill or right-click (shell wires settingsRequested -> open()).
//
// SLICE 1: frame + dismiss + the DISPLAY section's Mode + Direction, which ride the shell's existing
// persisted seams. Like the HUD, it WRITES only reader.persistedMode / reader.persistedDirection
// (never reader.mode / reader.rtl) so a crossing's load() still owns the actual toggle. Night veil,
// the mode-specific sections, the tool grid and the danger row land in later slices (they need new
// settings plumbing + vendored icons).
//
// PRESENTATION + INTENTS ONLY. Reads reading state off the injected `reader` seam; every `reader.`
// use is guarded so a null/partial seam never errors. Dismiss (X / scrim / close()) emits dismissed()
// and clears `opened`; a tap on the sheet BODY is swallowed (floating-panel/click-swallower law).

import QtQuick
import QtQuick.Window   // Window attached property (reliable overlay sizing)
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    // Fill the shell explicitly (anchors.fill resolved to 0×0 for this overlay mount — a QML anchor
    // quirk; an explicit size binding tracks the parent reliably and follows a resize/fullscreen).
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= the shell seam =================
    property var reader: null

    // ---- open state (source of truth; the shell binds modalOpen off `opened`) ----
    property bool _open: false
    readonly property bool opened: _open
    signal dismissed()
    function open()  { _open = true }
    function close() { _open = false }
    function toggle() { _open = !_open }
    function dismiss() { close(); dismissed() }

    // the sheet only intercepts input while open (a closed overlay is inert over the reader)
    visible: opened || scrim.opacity > 0.001

    // ---- read-only reading state ----
    readonly property string mode: reader ? reader.mode : "long_strip"
    readonly property bool   rtl:  reader ? reader.rtl  : false

    // ---- writes to the persisted seams (never mode/rtl directly) ----
    function setMode(m)      { if (reader) reader.persistedMode = m }
    function setDirection(d) { if (reader) reader.persistedDirection = d }

    Theme { id: theme }

    // mock's exact glass micro-values (surface 02 is the binding contract; finer than Theme's kit)
    readonly property color cGlassDeep:  Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.94)
    readonly property color cEdge:       Qt.rgba(1, 1, 1, 0.18)
    readonly property color cEdgeSoft:   Qt.rgba(1, 1, 1, 0.09)
    readonly property color cRowLine:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color cGoldChipBg: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
    readonly property color cGoldBorder: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
    readonly property color cDim:        Qt.rgba(0, 0, 0, 0.44)

    // ============================================================================================
    // sheet vocabulary
    // ============================================================================================
    // a single choice chip — gold ONLY when active (the active-choice-only-gold law)
    component Chip: Rectangle {
        id: chip
        property bool active: false
        property string label: ""
        signal tapped()
        implicitHeight: 22
        implicitWidth: chipText.implicitWidth + 20
        radius: 7
        color: chip.active ? root.cGoldChipBg : "transparent"
        border.width: 1
        border.color: chip.active ? root.cGoldBorder : root.cEdgeSoft
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.active ? theme.gold : theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 12
            font.bold: chip.active
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: chip.tapped() }
    }

    // a settings row: left label, right a set of chips; a hairline under it
    component SettingRow: Item {
        id: srow
        default property alias setContent: setRow.data
        property string label: ""
        implicitHeight: 37
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: srow.label
            color: theme.inkDim
            font.family: theme.hud
            font.pixelSize: 12
        }
        Row {
            id: setRow
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.cRowLine }
    }

    component SectionLabel: Text {
        color: theme.inkDimmer
        font.family: theme.hud
        font.pixelSize: 10
        font.letterSpacing: 2
        font.capitalization: Font.AllUppercase
    }

    // ============================================================================================
    // dim scrim — tap to dismiss (fades with open)
    // ============================================================================================
    Rectangle {
        id: scrim
        objectName: "settingsScrim"
        anchors.fill: parent
        color: root.cDim
        opacity: root.opened ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        signal tapped()
        onTapped: root.dismiss()
        MouseArea { anchors.fill: parent; enabled: root.opened; onClicked: scrim.tapped() }
    }

    // ============================================================================================
    // the glass sheet (slides from the right)
    // ============================================================================================
    Rectangle {
        id: body
        objectName: "settingsBody"
        // Size + right-anchor off the SCRIM. The scrim fills the sheet via anchors.fill and paints
        // reliably, so its geometry tracks the real reader area; binding the body's own width/height/
        // anchors to the root did NOT repaint here (a scene-graph quirk with this deeply-nested overlay
        // mount). A right-edge full-height glass column. The whole sheet fades in with `opened`
        // (root.visible gate), so no per-panel slide is needed.
        width: 352
        height: scrim.height
        y: scrim.y
        x: scrim.x + scrim.width - width
        color: root.cGlassDeep
        // left edge line
        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.cEdge }

        // click-swallower: a tap on the sheet body must NOT fall through to the scrim beneath
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

        Column {
            id: content
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 20
            spacing: 0

            // ---- title row: "Reader settings" + close X ----
            Item {
                width: parent.width
                height: 34
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Reader settings"
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 0.3
                }
                Item {
                    id: closeX
                    objectName: "settingsCloseX"
                    width: 24; height: 24
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    signal tapped()
                    onTapped: root.dismiss()
                    ComicReaderIcon {
                        anchors.centerIn: parent
                        kind: "close"
                        width: 15; height: 15
                        ink: theme.inkDimmer
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: closeX.tapped() }
                }
            }

            // ============ DISPLAY ============
            SectionLabel { text: "Display"; height: 32; verticalAlignment: Text.AlignVCenter }

            SettingRow {
                width: parent.width
                label: "Mode"
                Chip { objectName: "settingsModeDouble"; label: "Double"; active: root.mode === "double_page"; onTapped: root.setMode("double_page") }
                Chip { objectName: "settingsModeStrip";  label: "Strip";  active: root.mode === "long_strip";  onTapped: root.setMode("long_strip") }
            }

            SettingRow {
                width: parent.width
                label: "Direction"
                Chip { objectName: "settingsDirRtl"; label: "RTL · manga"; active: root.rtl;  onTapped: root.setDirection("rtl") }
                Chip { objectName: "settingsDirLtr"; label: "LTR";             active: !root.rtl; onTapped: root.setDirection("ltr") }
            }
        }
    }
}
