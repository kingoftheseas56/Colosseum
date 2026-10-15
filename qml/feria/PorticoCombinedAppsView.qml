import QtQuick
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit

    Rectangle { anchors.fill:parent; color:Qt.rgba(3/255,4/255,7/255,0.60) }

    Rectangle {
        anchors { top:parent.top; bottom:parent.bottom; right:parent.right }
        width:Math.min(34*u, parent.width*0.92)
        gradient:Gradient {
            GradientStop { position:0;color:Qt.rgba(12/255,15/255,24/255,0.985) }
            GradientStop { position:1;color:Qt.rgba(6/255,7/255,11/255,0.995) }
        }
        border.width:1
        border.color:Qt.rgba(1,1,1,0.12)

        Flickable {
            anchors.fill:parent
            contentWidth:width
            contentHeight:body.implicitHeight+7*u
            clip:true
            boundsBehavior:Flickable.StopAtBounds

            Column {
                id:body
                x:1.6*u;y:1.8*u
                width:parent.width-3.2*u
                spacing:0.5*u
                Rectangle {
                    width:done.implicitWidth+1.7*u;height:2.5*u;radius:height/2;color:Qt.rgba(0,0,0,0.45);border.width:1;border.color:Qt.rgba(1,1,1,0.16)
                    Colosseum.BackAction { id:done;anchors.centerIn:parent;variant:"plain";label:"Done";idleColor:controller.mist;hoverColor:controller.ink;labelSize:0.9*u;onTriggered:controller.back() }
                }
                Text { topPadding:0.9*u;text:"Your apps";color:controller.ink;font.family:controller.displayFont;font.pixelSize:2.2*u;font.weight:Font.Medium }
                Text {
                    width:parent.width
                    text:"Apps you turn on appear in the row and feed their shelves. Each service keeps its own sign-in inside its page; Colosseum never signs in for you."
                    color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.9*u;lineHeight:1.5;wrapMode:Text.WordWrap
                }
                Repeater {
                    model:["watch","listen","read"]
                    delegate:Column {
                        required property string modelData
                        width:body.width
                        spacing:0.25*u
                        Text { topPadding:0.9*u;text:modelData[0].toUpperCase()+modelData.slice(1);color:controller.mist;font.family:controller.displayFont;font.pixelSize:1.3*u;font.weight:Font.Medium }
                        Repeater {
                            model:Data.CATALOG[modelData]
                            delegate:Rectangle {
                                required property string modelData
                                required property int index
                                width:body.width;height:4.4*u;radius:0.9*u
                                color:Qt.rgba(1,1,1,0.0)
                                opacity:Data.P[modelData].app?0.5:1
                                border.width:controller.appManagerIndex===Data.CATALOG.watch.concat(Data.CATALOG.listen,Data.CATALOG.read).indexOf(modelData)?0.1875*u:0
                                border.color:controller.gold
                                Row {
                                    anchors{fill:parent;leftMargin:0.9*u;rightMargin:0.9*u}
                                    spacing:0.9*u
                                    Rectangle { width:3*u;height:3*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.05);border.width:1;border.color:Qt.rgba(1,1,1,0.10);anchors.verticalCenter:parent.verticalCenter
                                        PorticoCombinedGlyph{anchors.centerIn:parent;width:1.6*u;height:1.6*u;glyphKey:modelData;tone:controller.ink}
                                    }
                                    Column { anchors.verticalCenter:parent.verticalCenter;width:parent.width-8*u
                                        Text{text:controller.providerName(modelData);color:controller.ink;font.family:controller.uiFont;font.pixelSize:1.02*u;font.weight:Font.DemiBold}
                                        Text{text:Data.P[modelData].app?"Reads only in its own app, so it can't open here":Data.P[modelData].d;color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.82*u;elide:Text.ElideRight;width:parent.width}
                                    }
                                    Rectangle {
                                        anchors.verticalCenter:parent.verticalCenter
                                        width:2.8*u;height:1.6*u;radius:height/2
                                        color:controller.activeApps.indexOf(modelData)>=0?controller.gold:Qt.rgba(1,1,1,0.14)
                                        Rectangle { width:1.2*u;height:1.2*u;radius:width/2;y:0.2*u;x:controller.activeApps.indexOf(modelData)>=0?1.4*u:0.2*u;color:controller.activeApps.indexOf(modelData)>=0?"#171205":controller.mist;Behavior on x{NumberAnimation{duration:140}} }
                                    }
                                }
                                MouseArea { anchors.fill:parent;enabled:!Data.P[modelData].app;hoverEnabled:true;onEntered:controller.appManagerIndex=Data.CATALOG.watch.concat(Data.CATALOG.listen,Data.CATALOG.read).indexOf(modelData);onClicked:controller.toggleApp(modelData) }
                            }
                        }
                    }
                }
            }
        }
    }
}
