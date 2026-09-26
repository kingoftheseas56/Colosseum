import QtQuick
import QtQuick.Layouts
import ".." as Colosseum

Item {
    id: mast
    required property var controller
    required property Item backdrop
    readonly property real u: controller.unit
    height: 6 * u
    z: 40

    RowLayout {
        anchors { fill: parent; leftMargin: controller.marginX; rightMargin: controller.marginX }
        spacing: 1 * u

        Item {
            Layout.preferredWidth: 2.9 * u
            Layout.preferredHeight: 2.9 * u
            Rectangle {
                anchors.fill: parent
                radius: 0.9 * u
                color: Qt.rgba(1,1,1,0.055)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.18)
            }
            Image {
                anchors.centerIn: parent
                width: 2.1 * u; height: 2.1 * u
                source: Qt.resolvedUrl("../../assets/icons/colosseum.svg")
                fillMode: Image.PreserveAspectFit
            }
        }
        Text {
            text: "Portico"
            color: controller.ink
            font.family: controller.displayFont
            font.pixelSize: 2.35 * u
            font.weight: Font.Medium
            Layout.leftMargin: 0.1 * u
        }
        Item { Layout.preferredWidth: 0.9 * u }
        Colosseum.Glass {
            id: lensesGlass
            backdrop: mast.backdrop
            Layout.preferredHeight: 3.25 * u
            Layout.preferredWidth: lensRow.implicitWidth + 0.625 * u
            radius: height / 2
            tint: 0.10
            scrim: 0
            edge: Qt.rgba(1,1,1,0.18)
            blurAmount: 1.0
            Row {
                id: lensRow
                anchors.centerIn: parent
                spacing: 0.375 * u
                Repeater {
                    model: [{k:"all",n:"All"},{k:"watch",n:"Watch"},{k:"listen",n:"Listen"},{k:"read",n:"Read"}]
                    delegate: PorticoCombinedPill {
                        required property var modelData
                        unit: u
                        label: modelData.n
                        selected: controller.lens === modelData.k
                        onTriggered: controller.selectLens(modelData.k)
                        onEntered: controller.selectLens(modelData.k)
                    }
                }
            }
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            Layout.preferredWidth: 15.3 * u
            Layout.preferredHeight: 2.9 * u
            radius: height / 2
            color: Qt.rgba(1,1,1,0.055)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.12)
            Row {
                anchors.centerIn: parent
                spacing: 0.7 * u
                Image {
                    width: 1.2 * u; height: width
                    source: Qt.resolvedUrl("../../assets/icons/search.svg")
                    fillMode: Image.PreserveAspectFit
                    opacity: 0.8
                }
                Text { text: "Search"; color: controller.mist; font.family: theme.ui; font.pixelSize: 0.95 * u }
                Text { text: "or just start typing"; color: controller.slate; font.family: theme.ui; font.pixelSize: 0.88 * u }
            }
            Colosseum.KeyboardAction {
                anchors.fill: parent
                showFocusFrame: false
                onTriggered: controller.openSearch("")
            }
        }
        Item {
            Layout.preferredWidth: 2.9 * u
            Layout.preferredHeight: 2.9 * u
            Rectangle { anchors.fill: parent; radius: width/2; color: Qt.rgba(1,1,1,0.055); border.width:1; border.color:Qt.rgba(1,1,1,0.14) }
            Canvas {
                anchors.centerIn: parent; width: 1.3*u; height: 1.3*u
                onPaint: {
                    var c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=controller.mist; c.lineWidth=1.7; c.beginPath();
                    c.arc(width/2,height*0.35,width*0.18,0,Math.PI*2); c.stroke(); c.beginPath(); c.arc(width/2,height*1.05,width*0.38,Math.PI*1.12,Math.PI*1.88); c.stroke()
                }
            }
            Colosseum.KeyboardAction { anchors.fill: parent; showFocusFrame:false; onTriggered: controller.viewState = "account" }
        }
        Text {
            text: controller.clockText()
            color: controller.ink
            font.family: controller.displayFont
            font.pixelSize: 1.7 * u
            font.weight: Font.Medium
        }
        Text {
            text: controller.clockSuffix()
            color: controller.mist
            font.family: theme.ui
            font.pixelSize: 0.85 * u
            Layout.leftMargin: -0.7 * u
            Layout.alignment: Qt.AlignVCenter
        }
    }
    Colosseum.Theme { id: theme }
}
