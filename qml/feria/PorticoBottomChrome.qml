import QtQuick
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    z: 70

    Rectangle {
        x: Math.max(18, controller.width * 0.045)
        y: parent.height - height - 1 * u
        height: 4 * u
        width: 11.3 * u
        radius: height / 2
        color: Qt.rgba(5/255,5/255,8/255,0.76)
        border.width: 1
        border.color: Qt.rgba(1,1,1,0.16)

        Row {
            anchors.centerIn: parent
            spacing: 0.75 * u

            Item {
                width: 2.9 * u; height: 2.9 * u
                Rectangle { anchors.fill: parent; radius: width/2; color: Qt.rgba(1,1,1,0.045); border.width:1; border.color:Qt.rgba(1,1,1,0.08) }
                Canvas {
                    anchors.centerIn: parent; width:1.4*u; height:1.4*u
                    onPaint: {
                        var c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=controller.mist; c.lineWidth=1.6
                        c.beginPath(); c.moveTo(width*0.18,height*0.45); c.lineTo(width*0.5,height*0.2); c.lineTo(width*0.82,height*0.45); c.lineTo(width*0.82,height*0.82); c.moveTo(width*0.32,height*0.82); c.lineTo(width*0.32,height*0.5); c.lineTo(width*0.68,height*0.5); c.lineTo(width*0.68,height*0.82); c.stroke()
                    }
                }
            }
            Item {
                width: 2.9 * u; height: 2.9 * u
                Rectangle { anchors.fill: parent; radius:0.8*u; color:Qt.rgba(1,1,1,0.045); border.width:1; border.color:Qt.rgba(1,1,1,0.08) }
                Image { anchors.centerIn: parent; width:1.4*u; height:1.4*u; source:Qt.resolvedUrl("../../assets/icons/vault-folder.svg"); opacity:0.8 }
            }
            Item {
                width: 2.9 * u; height: 2.9 * u
                Rectangle { anchors.fill: parent; radius:0.8*u; color:Qt.rgba(1,1,1,0.10); border.width:1; border.color:Qt.rgba(1,1,1,0.20) }
                Canvas {
                    anchors.centerIn: parent; width:1.4*u; height:1.4*u
                    onPaint: {
                        var c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=controller.ink; c.lineWidth=1.6
                        c.beginPath(); c.moveTo(width*0.2,height*0.84); c.lineTo(width*0.2,height*0.46); c.bezierCurveTo(width*0.2,height*0.16,width*0.8,height*0.16,width*0.8,height*0.46); c.lineTo(width*0.8,height*0.84); c.stroke()
                        c.beginPath(); c.moveTo(width*0.38,height*0.84); c.lineTo(width*0.38,height*0.64); c.bezierCurveTo(width*0.38,height*0.49,width*0.62,height*0.49,width*0.62,height*0.64); c.lineTo(width*0.62,height*0.84); c.stroke()
                    }
                }
                Rectangle { anchors.horizontalCenter:parent.horizontalCenter; anchors.top:parent.bottom; anchors.topMargin:0.12*u; width:1*u; height:0.18*u; radius:height/2; color:controller.gold }
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1.35 * u
        height: 2.6 * u
        width: hintRow.implicitWidth + 2.2 * u
        radius: height / 2
        color: Qt.rgba(5/255,5/255,8/255,0.78)
        border.width: 1
        border.color: Qt.rgba(1,1,1,0.12)
        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: 1.05 * u
            Repeater {
                model: controller.viewState === "home"
                       ? [{k:"Enter",l:"Open app"},{k:"Space",l:"Reorder"},{k:"Esc",l:"Home"},{k:"A–Z",l:"Search"},{k:"[  ]",l:"Filter"},{k:"1–9",l:"App"}]
                       : controller.viewState === "title"
                         ? [{k:"Enter",l:"Open in Colosseum"},{k:"Esc",l:"Back"}]
                         : [{k:"Esc",l:"Back"}]
                delegate: Row {
                    required property var modelData
                    spacing: 0.4 * u
                    Rectangle {
                        width: keyText.implicitWidth + 0.7 * u
                        height: 1.45 * u
                        radius: 0.35 * u
                        color: Qt.rgba(1,1,1,0.04)
                        border.width: 1
                        border.color: Qt.rgba(1,1,1,0.22)
                        Text { id:keyText; anchors.centerIn:parent; text:modelData.k; color:controller.mist; font.family:"Segoe UI"; font.pixelSize:0.72*u; font.weight:Font.DemiBold }
                    }
                    Text { anchors.verticalCenter:parent.verticalCenter; text:modelData.l; color:controller.mist; font.family:"Segoe UI"; font.pixelSize:0.82*u }
                }
            }
        }
    }

    Rectangle {
        anchors { right:parent.right; bottom:parent.bottom; rightMargin:1.1*u; bottomMargin:1.1*u }
        width: 5.2 * u; height: 2.2 * u; radius:0.5*u
        color: Qt.rgba(0,0,0,0.70); border.width:1; border.color:Qt.rgba(1,1,1,0.35)
        Text { anchors.centerIn:parent; text:"Prototype"; color:controller.slate; font.family:"Segoe UI"; font.pixelSize:0.75*u }
    }
}
