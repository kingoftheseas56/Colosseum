import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var it: controller.titleObj(controller.hostTitle)

    Rectangle { anchors.fill: parent; color: "#000000" }

    Rectangle {
        id: strip
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 4.2 * u
        color: Qt.rgba(6/255,7/255,11/255,0.90)
        border.width: 0

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Qt.rgba(1,1,1,0.10)
        }

        RowLayout {
            anchors { fill: parent; leftMargin: m; rightMargin: m }
            spacing: 1.2 * u

            Rectangle {
                Layout.preferredWidth: 6.4 * u
                Layout.preferredHeight: 2.5 * u
                radius: height / 2
                color: Qt.rgba(1,1,1,0.04)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.16)
                Text {
                    anchors.centerIn: parent
                    text: "‹   Feria"
                    color: controller.mist
                    font.family: controller.uiFont
                    font.pixelSize: 0.9 * u
                }
                MouseArea { anchors.fill: parent; onClicked: controller.back() }
            }

            PorticoCombinedGlyph {
                Layout.preferredWidth: 1.3 * u
                Layout.preferredHeight: 1.3 * u
                glyphKey: controller.hostApp || "plus"
                tone: controller.ink
            }
            Text {
                text: controller.providerName(controller.hostApp)
                color: controller.ink
                font.family: controller.uiFont
                font.pixelSize: 1.0 * u
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: controller.hostUrl || controller.providerName(controller.hostApp)
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.85 * u
                elide: Text.ElideRight
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(52 * u, parent.width - 2*m)
        spacing: 1.0 * u
        Text {
            width: parent.width
            text: controller.providerName(controller.hostApp)
            color: Qt.rgba(247/255,247/255,245/255,0.50)
            font.family: controller.displayFont
            font.pixelSize: 4.0 * u
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: "Provider browsing is not connected to Feria yet."
            color: controller.ink
            font.family: controller.uiFont
            font.pixelSize: 1.0 * u
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: "The destination is ready. This screen will hand it to the provider's real site inside Colosseum when the WebView2 host is connected."
            color: controller.slate
            font.family: controller.uiFont
            font.pixelSize: 0.9 * u
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: tag.implicitWidth + 1.8 * u
            height: 2.1 * u
            radius: height / 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.14)
            Text {
                id: tag
                anchors.centerIn: parent
                text: "No provider page is loaded"
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.78 * u
            }
        }
    }

}
