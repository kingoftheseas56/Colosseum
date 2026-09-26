import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: controller.dusk }
            GradientStop { position: 1; color: controller.night }
        }
    }

    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 6.2 * u
        z: 4

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: controller.dusk }
                GradientStop { position: 0.72; color: controller.dusk }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        Row {
            anchors { left: parent.left; leftMargin: m; verticalCenter: parent.verticalCenter }
            spacing: 1.2 * u
            Rectangle {
                width: 6.4 * u
                height: 2.5 * u
                radius: height / 2
                color: Qt.rgba(0,0,0,0.42)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.16)
                Text {
                    anchors.centerIn: parent
                    text: "‹   Portico"
                    color: controller.mist
                    font.family: "Segoe UI"
                    font.pixelSize: 0.9 * u
                }
                MouseArea { anchors.fill: parent; onClicked: controller.back() }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Your Portico"
                color: controller.ink
                font.family: controller.displayFont
                font.pixelSize: 2.35 * u
                font.weight: Font.Medium
            }
        }

        Rectangle {
            anchors.centerIn: parent
            height: 3.25 * u
            width: tabs.implicitWidth + 0.625 * u
            radius: height / 2
            color: Qt.rgba(1,1,1,0.10)
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.18)
            Row {
                id: tabs
                anchors.centerIn: parent
                spacing: 0.375 * u
                Repeater {
                    model: [{k:"history",n:"History"},{k:"highlights",n:"Highlights"},{k:"stats",n:"Stats"},{k:"apps",n:"Apps"}]
                    delegate: PorticoPill {
                        required property var modelData
                        unit: u
                        label: modelData.n
                        selected: controller.accountTab === modelData.k
                        onTriggered: controller.accountTab = modelData.k
                        onEntered: controller.accountTab = modelData.k
                    }
                }
            }
        }
    }

    PorticoParityAccountHistory {
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        controller: root.controller
        visible: controller.accountTab === "history"
    }

    Item {
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom }
        visible: controller.accountTab !== "history"
        Column {
            x: m
            y: 0.5 * u
            width: parent.width - 2*m
            spacing: 1.2 * u
            Text {
                text: controller.accountTab === "apps" ? "Apps" :
                      controller.accountTab === "stats" ? "Stats" : "Highlights"
                color: controller.ink
                font.family: controller.displayFont
                font.pixelSize: 1.75 * u
            }
            Rectangle {
                width: parent.width
                height: 15 * u
                radius: 1.1 * u
                color: Qt.rgba(1,1,1,0.035)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.08)
                Text {
                    anchors.centerIn: parent
                    text: controller.accountTab === "apps"
                          ? controller.activeApps.length + " services in your row"
                          : controller.accountTab === "stats"
                            ? "Your Portico usage"
                            : "September"
                    color: controller.mist
                    font.family: controller.displayFont
                    font.pixelSize: 1.8 * u
                }
            }
        }
    }
}
