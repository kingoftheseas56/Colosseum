// TheatreRowControls — the per-shelf edit-mode control strip (spec §11, Harbor parity):
// move up, move down, hide/show, and rename/reset-name. No drag-and-drop. Rename is an inline
// TextInput committed by Enter or focus loss and cancelled by Escape. Impossible moves are
// disabled, never wrapped. Grey/black/white only — geometric glyphs, no colour, no emoji.
import QtQuick

Item {
    id: controls

    property string rowKey: ""
    property string label: ""
    property bool hidden: false
    property bool canMoveUp: true
    property bool canMoveDown: true
    property bool renamed: false          // true when a custom name overrides the default

    signal moveUpRequested()
    signal moveDownRequested()
    signal toggleHiddenRequested()
    signal renameRequested(string label)
    signal resetNameRequested()

    property bool editing: false

    implicitHeight: 34
    implicitWidth: bar.width

    Theme { id: theme }

    function beginEdit() {
        nameField.text = controls.label
        controls.editing = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function commitEdit() {
        if (!controls.editing) return
        controls.editing = false
        controls.renameRequested(nameField.text)
    }
    function cancelEdit() { controls.editing = false }

    // ── a small square glyph button ──
    component GlyphButton: Rectangle {
        id: btn
        property string glyph: ""
        property string tip: ""
        property bool enabledLook: true
        signal clicked()
        width: 28; height: 28; radius: 6
        color: ma.containsMouse && btn.enabledLook ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        border.width: 1
        border.color: btn.enabledLook ? theme.edge : Qt.rgba(1, 1, 1, 0.06)
        opacity: btn.enabledLook ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: btn.glyph
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        MouseArea {
            id: ma; anchors.fill: parent; hoverEnabled: true
            cursorShape: btn.enabledLook ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btn.enabledLook) btn.clicked()
        }
    }

    Row {
        id: bar
        spacing: 6
        anchors.verticalCenter: parent.verticalCenter

        GlyphButton {
            glyph: "▲"                     // ▲ move up
            enabledLook: controls.canMoveUp
            onClicked: controls.moveUpRequested()
        }
        GlyphButton {
            glyph: "▼"                     // ▼ move down
            enabledLook: controls.canMoveDown
            onClicked: controls.moveDownRequested()
        }
        GlyphButton {
            glyph: controls.hidden ? "+" : "–"   // + show / – hide
            tip: controls.hidden ? "Show" : "Hide"
            onClicked: controls.toggleHiddenRequested()
        }

        // rename affordance: a label button that flips into an inline editor
        Rectangle {
            width: 88; height: 28; radius: 6
            visible: !controls.editing
            color: renMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            border.width: 1; border.color: theme.edge
            anchors.verticalCenter: parent.verticalCenter
            Text {
                anchors.centerIn: parent; text: "Rename"
                color: theme.ink; font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
            }
            MouseArea { id: renMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; onClicked: controls.beginEdit() }
        }
        Rectangle {
            width: 160; height: 28; radius: 6
            visible: controls.editing
            color: "#12151c"; border.width: 1; border.color: theme.gold
            anchors.verticalCenter: parent.verticalCenter
            TextInput {
                id: nameField
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                color: theme.ink; font.family: theme.ui; font.pixelSize: 12
                selectByMouse: true; clip: true
                onEditingFinished: controls.commitEdit()      // Enter or focus loss commits
                Keys.onEscapePressed: controls.cancelEdit()
            }
        }

        GlyphButton {
            glyph: "↺"                     // ↺ reset name
            tip: "Reset name"
            enabledLook: controls.renamed
            onClicked: controls.resetNameRequested()
        }
    }
}
