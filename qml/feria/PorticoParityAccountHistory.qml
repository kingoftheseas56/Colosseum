import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var sessions: controller.sampleSessions()

    Flickable {
        anchors.fill:parent
        contentWidth:width
        contentHeight:body.implicitHeight+8*u
        clip:true
        boundsBehavior:Flickable.StopAtBounds

        Column {
            id:body
            x:m;y:0.4*u;width:parent.width-2*m
            spacing:0.6*u

            RowLayout {
                width:parent.width
                spacing:0.7*u
                Text {
                    Layout.fillWidth:true
                    text:"Recorded by Portico from your first visit: what you opened, on which service, and how long its page stayed open. Portico can't see anything from before, or inside a service's page."
                    color:controller.slate;font.family:"Segoe UI";font.pixelSize:0.9*u;lineHeight:1.5;wrapMode:Text.WordWrap
                }
                Rectangle {
                    Layout.preferredWidth:8.2*u;Layout.preferredHeight:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06);border.width:1;border.color:Qt.rgba(1,1,1,0.14)
                    Text{anchors.centerIn:parent;text:controller.recording?"Pause recording":"Resume recording";color:controller.ink;font.family:"Segoe UI";font.pixelSize:0.88*u}
                    MouseArea{anchors.fill:parent;onClicked:controller.recording=!controller.recording}
                }
                Rectangle {
                    Layout.preferredWidth:6.8*u;Layout.preferredHeight:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06);border.width:1;border.color:Qt.rgba(1,1,1,0.14)
                    Text{anchors.centerIn:parent;text:"Clear history";color:controller.ink;font.family:"Segoe UI";font.pixelSize:0.88*u}
                }
            }

            Repeater {
                model:root.sessions
                delegate:Item {
                    required property var modelData
                    required property int index
                    property bool newDay:index===0||root.sessions[index-1].day!==modelData.day
                    width:body.width
                    height:(newDay?3.2*u:0)+5.8*u+0.6*u
                    Text {
                        visible:parent.newDay
                        x:0;y:0
                        text:modelData.day
                        color:controller.mist
                        font.family:controller.displayFont
                        font.pixelSize:1.35*u
                        font.weight:Font.Medium
                    }
                    Rectangle {
                        x:0;y:parent.newDay?2.2*u:0
                        width:parent.width;height:5.8*u;radius:1*u
                        color:Qt.rgba(1,1,1,0.035);border.width:1;border.color:Qt.rgba(1,1,1,0.08)
                        RowLayout {
                            anchors { fill: parent; leftMargin: 0.7*u; rightMargin: 1.3*u }
                            spacing: 1.2*u
                            Rectangle {
                                Layout.preferredWidth:3.4*u;Layout.preferredHeight:3.4*u;radius:modelData.id&&Data.T[modelData.id]&&Data.T[modelData.id].k==="artist"?1.7*u:0.55*u;clip:true;color:Qt.rgba(1,1,1,0.05)
                                Image{anchors.fill:parent;visible:modelData.id&&status===Image.Ready;source:modelData.id?controller.artUrl(Data.T[modelData.id],false):"";fillMode:Image.PreserveAspectCrop;asynchronous:true}
                                PorticoGlyph{anchors.centerIn:parent;width:1.7*u;height:1.7*u;visible:!modelData.id;glyphKey:modelData.pk;tone:controller.ink}
                            }
                            ColumnLayout {
                                Layout.fillWidth:true;spacing:0.25*u
                                Text{text:modelData.id?Data.T[modelData.id].t:controller.providerName(modelData.pk)+" home page";color:controller.ink;font.family:"Segoe UI";font.pixelSize:1.08*u;font.weight:Font.DemiBold;elide:Text.ElideRight;Layout.fillWidth:true}
                                Text{text:"Opened on "+controller.providerName(modelData.pk)+" at "+modelData.time;color:controller.slate;font.family:"Segoe UI";font.pixelSize:0.88*u}
                            }
                            Column {
                                Layout.alignment:Qt.AlignRight|Qt.AlignVCenter
                                Text{text:controller.durationText(modelData.mins);color:controller.ink;font.family:controller.displayFont;font.pixelSize:1.45*u;font.weight:Font.Medium;horizontalAlignment:Text.AlignRight;width:implicitWidth}
                                Text{text:"on "+controller.providerName(modelData.pk);color:controller.slate;font.family:"Segoe UI";font.pixelSize:0.8*u;horizontalAlignment:Text.AlignRight;width:parent.width}
                            }
                        }
                        MouseArea{anchors.fill:parent;enabled:!!modelData.id;onClicked:if(modelData.id)controller.openTitle(modelData.id)}
                    }
                }
            }
        }
    }
}
