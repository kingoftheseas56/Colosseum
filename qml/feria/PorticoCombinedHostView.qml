import QtQuick
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit

    Rectangle { anchors.fill: parent; color: "#000000" }

    Rectangle {
        anchors { left:parent.left; right:parent.right; top:parent.top }
        height: 4.2 * u
        color: Qt.rgba(6/255,7/255,11/255,0.90)
        border.width: 0
        z: 3
        Row {
            anchors { fill:parent; leftMargin:controller.marginX; rightMargin:controller.marginX }
            spacing: 1.2 * u
            Colosseum.BackAction {
                anchors.verticalCenter: parent.verticalCenter
                variant: "capsule"
                tip: "Back to Portico"
                onTriggered: controller.back()
            }
            PorticoCombinedGlyph {
                anchors.verticalCenter: parent.verticalCenter
                width: 1.3 * u; height: width
                glyphKey: controller.hostApp || "plus"
                tone: controller.ink
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: controller.providerName(controller.hostApp)
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: 1.05 * u
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - 24*u)
                text: controller.hostTitle && controller.titleObj(controller.hostTitle)
                      ? controller.titleObj(controller.hostTitle).t : "Provider home"
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.85 * u
                elide: Text.ElideRight
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(52 * u, parent.width - 2 * controller.marginX)
        spacing: 0.8 * u
        Text {
            width: parent.width
            text: controller.providerName(controller.hostApp)
            color: controller.ink
            font.family: controller.displayFont
            font.pixelSize: 4 * u
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: controller.hostTitle && controller.titleObj(controller.hostTitle)
                  ? controller.titleObj(controller.hostTitle).t : "Home"
            color: controller.mist
            font.family: controller.uiFont
            font.pixelSize: 1.1 * u
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: "The provider's own page renders here in the production WebView2 host."
            color: controller.slate
            font.family: controller.uiFont
            font.pixelSize: 0.95 * u
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
