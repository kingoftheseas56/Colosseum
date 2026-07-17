// BottomRail.qml — the reader's bottom chrome: a thin GOLD progress rail with a knob,
// chapter ticks (Reader2Logic.railTicks), drag-to-scrub, and a two-part meta row
// ("Page N of M in chapter" left / "X% of book" right). A glass "Return to page N"
// ghost chip appears after a jump. Pixel contract: the chrome mock's `.bottombar`,
// `.rail`, `.railmeta`, `.returnchip`. Glass over the paper; fades in with the reveal.
//
// [Agent 2 (Claude), biblio]
import QtQuick

Item {
    id: root

    // ---- inputs ----
    property real fraction: 0                 // 0..1 book progress (fill + knob)
    property int pageInChapter: 0
    property int pagesInChapter: 0
    property int percentOfBook: 0
    property var ticks: []                     // array of 0..1 chapter-mark fractions
    property bool returnVisible: false
    property string returnPageLabel: ""
    property bool shown: false

    // ---- signals up ----
    signal scrubbed(real fraction)
    signal returnRequested()

    // total height: rail row (22) + meta gap (8) + meta (~16) + bottom padding (18)
    height: 22 + 8 + metaRow.height + 18

    enabled: shown
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: root.shown ? 0 : 6
        Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    }

    readonly property real clampedFraction: Math.max(0, Math.min(1, fraction))
    readonly property real displayFraction: scrubArea.dragging ? scrubArea.preview : clampedFraction

    // ---------- the rail ----------
    Item {
        id: railRow
        height: 22
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18 + metaRow.height + 8

        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 3
            radius: 2
            color: Theme.track

            // chapter ticks
            Repeater {
                model: root.ticks
                delegate: Rectangle {
                    required property var modelData
                    width: 1
                    height: 7
                    y: -2
                    x: Math.round(track.width * Math.max(0, Math.min(1, modelData)))
                    color: Theme.tick
                }
            }

            // gold fill
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * root.displayFraction
                radius: 2
                color: Theme.gold
            }

            // gold knob with a soft ring (mock box-shadow 0 0 0 4px rgba(gold,.18))
            Rectangle {
                width: 19; height: 19; radius: 9.5
                color: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.18)
                x: parent.width * root.displayFraction - width / 2
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    anchors.centerIn: parent
                    width: 11; height: 11; radius: 5.5
                    color: Theme.gold
                }
            }
        }

        MouseArea {
            id: scrubArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            property bool dragging: false
            property real preview: 0
            function fracAt(mx) { return Math.max(0, Math.min(1, width > 0 ? mx / width : 0)) }
            onPressed: (m) => { dragging = true; preview = fracAt(m.x) }
            onPositionChanged: (m) => { if (dragging) preview = fracAt(m.x) }
            onReleased: (m) => {
                if (dragging) { preview = fracAt(m.x); dragging = false; root.scrubbed(preview) }
            }
            onCanceled: dragging = false
        }
    }

    // ---------- meta row ----------
    Item {
        id: metaRow
        height: 18
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18

        // left: "Page N of M in chapter"
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            visible: root.pagesInChapter > 0
            Text { text: "Page "; color: Theme.inkFaint; font.family: Theme.ui; font.pixelSize: 13 }
            Text {
                text: root.pageInChapter + " of " + root.pagesInChapter
                color: Theme.inkDim; font.family: Theme.ui; font.weight: Font.Medium; font.pixelSize: 13
            }
            Text { text: " in chapter"; color: Theme.inkFaint; font.family: Theme.ui; font.pixelSize: 13 }
        }

        // right: "X% of book"
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                text: root.percentOfBook + "%"
                color: Theme.inkDim; font.family: Theme.ui; font.weight: Font.Medium; font.pixelSize: 13
            }
            Text { text: " of book"; color: Theme.inkFaint; font.family: Theme.ui; font.pixelSize: 13 }
        }
    }

    // ---------- return-to-page ghost chip (after a jump) ----------
    Rectangle {
        id: returnChip
        visible: opacity > 0.01
        opacity: (root.returnVisible && root.shown) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        anchors.right: railRow.right
        anchors.bottom: railRow.top
        anchors.bottomMargin: 12
        radius: height / 2
        height: 34
        width: chipRow.width + 32
        color: Theme.bar
        border.color: Theme.barBorder
        border.width: 1

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 8
            Image {
                anchors.verticalCenter: parent.verticalCenter
                source: Qt.resolvedUrl("../../assets/icons/reader2/return.svg")
                width: 15; height: 15
                sourceSize.width: 30; sourceSize.height: 30
                fillMode: Image.PreserveAspectFit
                smooth: true
                opacity: chipMa.containsMouse ? 1.0 : 0.62
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.returnPageLabel !== "" ? ("Return to page " + root.returnPageLabel) : "Return"
                color: chipMa.containsMouse ? Theme.ink : Theme.inkDim
                font.family: Theme.ui
                font.weight: Font.DemiBold
                font.pixelSize: 13
            }
        }
        MouseArea {
            id: chipMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.returnRequested()
        }
    }
}
