import QtQuick
import QtQuick.Layouts
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX

    Rectangle {
        anchors.fill:parent
        gradient:Gradient {
            GradientStop{position:0;color:controller.dusk}
            GradientStop{position:1;color:controller.night}
        }
    }

    Item {
        id: head
        anchors { left:parent.left;right:parent.right;top:parent.top }
        height:6*u
        z:4
        Rectangle {
            anchors.fill:parent
            gradient:Gradient {
                GradientStop{position:0;color:controller.dusk}
                GradientStop{position:0.78;color:Qt.rgba(12/255,15/255,24/255,0.94)}
                GradientStop{position:1;color:"transparent"}
            }
        }
        RowLayout {
            anchors{fill:parent;leftMargin:m;rightMargin:m}
            spacing:1.4*u
            Row {
                spacing:1.2*u
                Rectangle {
                    width:back.implicitWidth+1.6*u;height:2.5*u;radius:height/2;color:Qt.rgba(0,0,0,0.45);border.width:1;border.color:Qt.rgba(1,1,1,0.16)
                    Colosseum.BackAction { id:back;anchors.centerIn:parent;variant:"plain";label:"Portico";idleColor:controller.mist;hoverColor:controller.ink;labelSize:0.9*u;onTriggered:controller.back() }
                }
                Text { anchors.verticalCenter:parent.verticalCenter;text:"Your Portico";color:controller.ink;font.family:controller.displayFont;font.pixelSize:2.35*u;font.weight:Font.Medium }
            }
            Item { Layout.fillWidth:true }
            Rectangle {
                Layout.preferredHeight:3.25*u
                Layout.preferredWidth:tabs.implicitWidth+0.625*u
                radius:height/2
                color:Qt.rgba(1,1,1,0.10)
                border.width:1;border.color:Qt.rgba(1,1,1,0.18)
                Row {
                    id:tabs;anchors.centerIn:parent;spacing:0.375*u
                    Repeater {
                        model:[{k:"history",n:"History"},{k:"highlights",n:"Highlights"},{k:"stats",n:"Stats"},{k:"apps",n:"Apps"}]
                        delegate:PorticoPill {
                            required property var modelData
                            unit:u;label:modelData.n;selected:controller.accountTab===modelData.k
                            onTriggered:controller.accountTab=modelData.k
                            onEntered:controller.accountTab=modelData.k
                        }
                    }
                }
            }
            Item { Layout.fillWidth:true }
        }
    }

    PorticoAccountHistory { anchors{left:parent.left;right:parent.right;top:head.bottom;bottom:parent.bottom};controller:controller;visible:controller.accountTab==="history" }
    PorticoAccountHighlights { anchors{left:parent.left;right:parent.right;top:head.bottom;bottom:parent.bottom};controller:controller;visible:controller.accountTab==="highlights" }
    PorticoAccountStats { anchors{left:parent.left;right:parent.right;top:head.bottom;bottom:parent.bottom};controller:controller;visible:controller.accountTab==="stats" }
    PorticoAccountApps { anchors{left:parent.left;right:parent.right;top:head.bottom;bottom:parent.bottom};controller:controller;visible:controller.accountTab==="apps" }
}
