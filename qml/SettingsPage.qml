// SettingsPage — the shell's global preferences surface, entered from the taskbar gear.
// Task 2 (Tankoban Discover): host-owned full page over the wallpaper, same chrome
// vocabulary as the Extensions/Downloads pages (back · minimize · fullscreen · power).
// One section for now — Content — carrying the single explicit-content switch. The
// switch reads and writes the shell's ONE ContentPreferences instance (passed in as
// `preferences`); the page owns no state of its own. Threading the preference into the
// worlds is Task 9 — this page only sets it.
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property Item backdrop: null
    property var preferences: null
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    Theme { id: theme }

    // swallow clicks so nothing behind this page receives them
    MouseArea { anchors.fill: parent }
    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- live shell wallpaper (the same backdrop sampling the store page uses) ----
    Item {
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

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 14
            spacing: 0

            // ---- header ----
            Text { text: "COLOSSEUM · SETTINGS"; color: theme.inkDimmer
                   font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold }
            Text { text: "Settings"; color: theme.ink; topPadding: 8
                   font.family: theme.display; font.pixelSize: 56; font.letterSpacing: -1 }
            Text {
                topPadding: 14
                font.family: theme.display; font.italic: true; font.pixelSize: 18
                color: theme.inkDim
                text: "How the house behaves — set once, honoured everywhere."
            }
            Item { width: 1; height: 20 }
            Rectangle { width: 34; height: 3; radius: 2; color: theme.gold }

            // ============ CONTENT ============
            Text {
                topPadding: 46
                text: "CONTENT"
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 10
                font.letterSpacing: 2.4; font.bold: true
            }
            Item { width: 1; height: 16 }

            Rectangle {
                width: col.width
                radius: 18
                color: Qt.rgba(0.04, 0.045, 0.065, 0.48)
                border.width: 1; border.color: theme.edge
                height: contentCard.implicitHeight + 44

                Row {
                    id: contentCard
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 28
                    anchors.rightMargin: 28
                    spacing: 24

                    Column {
                        width: parent.width - 46 - 24
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text {
                            text: "Explicit Content"
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 16; font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: "Show sexually explicit titles across Theatre, Tankoban, and Biblio. Violence, horror, mature themes, and standard age ratings are not filtered."
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13
                            lineHeight: 1.25
                        }
                    }

                    // the on/off switch — gold when carrying, the house toggle language
                    // (same as the Extensions installed rows). Checked = the live setting;
                    // a click writes the new value straight to the shell's preference store.
                    Rectangle {
                        id: sw
                        width: 46; height: 26; radius: 13
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property bool checked: root.preferences ? root.preferences.showExplicit : false
                        color: checked ? Qt.rgba(0.94, 0.77, 0.29, 0.85) : Qt.rgba(1, 1, 1, 0.12)
                        border.width: 1
                        border.color: checked ? Qt.rgba(0.94, 0.77, 0.29, 0.5) : theme.edge
                        Rectangle {
                            width: 20; height: 20; radius: 10
                            anchors.verticalCenter: parent.verticalCenter
                            x: sw.checked ? parent.width - width - 3 : 3
                            color: sw.checked ? "#141207" : theme.inkDim
                            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            enabled: root.preferences !== null
                            onClicked: root.preferences.showExplicit = !sw.checked
                        }
                    }
                }
            }
        }
    }

    // ---- top chrome: minimize · fullscreen · power (fullscreen-only vocabulary) ----
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: theme.margin
        width: chromeRow.implicitWidth
        height: 30
        Row {
            id: chromeRow
            spacing: 22
            Text { text: "—"; color: mMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: mMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() } }
            Text { text: "⛶"; color: fMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: fMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() } }
            Text { text: "⏻"; color: pMa.containsMouse ? theme.ink : theme.inkDim; font.pixelSize: 17
                   MouseArea { id: pMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() } }
        }
    }
    BackAction {
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 21
        anchors.leftMargin: theme.margin - 10
        onTriggered: root.backRequested()
    }
}
