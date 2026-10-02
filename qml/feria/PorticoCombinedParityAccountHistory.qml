import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property var sessions: controller.allSessions()

    function sessionIndexForAction(actionIndex) {
        var target = actionIndex - 2
        if (target < 0) return -1
        var seen = 0
        for (var i = 0; i < sessions.length; ++i) {
            if (!sessions[i].id) continue
            if (seen === target) return i
            ++seen
        }
        return -1
    }
    function actionIndexForSession(sessionIndex) {
        var actionIndex = 2
        for (var i = 0; i < sessionIndex; ++i) {
            if (sessions[i].id) ++actionIndex
        }
        return actionIndex
    }
    function revealFocus(actionIndex) {
        var target = null
        if (actionIndex === 0) target = pauseButton
        else if (actionIndex === 1) target = clearButton
        else {
            var sessionIndex = sessionIndexForAction(actionIndex)
            if (sessionIndex >= 0) target = sessionRepeater.itemAt(sessionIndex)
        }
        if (!target) return
        var point = target.mapToItem(flick.contentItem, 0, 0)
        var padding = 0.4 * u
        var top = point.y - padding
        var bottom = point.y + target.height + padding
        var maxY = Math.max(0, flick.contentHeight - flick.height)
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top)
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(maxY, bottom - flick.height)
    }

    Flickable {
        id: flick
        objectName: "account-history-flick"
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
                    text:"Recorded by Feria from your first visit: what you opened, on which service, and how long its page stayed open. Feria can't see anything from before, or inside a service's page."
                    color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.9*u;lineHeight:1.5;wrapMode:Text.WordWrap
                }
                Rectangle {
                    id: pauseButton
                    objectName: "account-history-pause"
                    Layout.preferredWidth:8.2*u;Layout.preferredHeight:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06)
                    border.width:controller.accountHistoryFocusIndex===0?0.1875*u:1
                    border.color:controller.accountHistoryFocusIndex===0?controller.gold:Qt.rgba(1,1,1,0.14)
                    Text{anchors.centerIn:parent;text:controller.recording?"Pause recording":"Resume recording";color:controller.ink;font.family:controller.uiFont;font.pixelSize:0.88*u}
                    MouseArea{
                        anchors.fill:parent
                        hoverEnabled:true
                        onEntered:controller.setAccountHistoryFocus(0)
                        onClicked:{controller.setAccountHistoryFocus(0);controller.toggleRecording()}
                    }
                }
                Rectangle {
                    id: clearButton
                    objectName: "account-history-clear"
                    Layout.preferredWidth:6.8*u;Layout.preferredHeight:2.5*u;radius:0.8*u;color:Qt.rgba(1,1,1,0.06)
                    border.width:controller.accountHistoryFocusIndex===1?0.1875*u:1
                    border.color:controller.accountHistoryFocusIndex===1?controller.gold:Qt.rgba(1,1,1,0.14)
                    Text{anchors.centerIn:parent;text:controller.clearPending?"Confirm clear":"Clear history";color:controller.ink;font.family:controller.uiFont;font.pixelSize:0.88*u}
                    MouseArea{
                        anchors.fill:parent
                        hoverEnabled:true
                        onEntered:controller.setAccountHistoryFocus(1)
                        onClicked:{controller.setAccountHistoryFocus(1);controller.clearHistory()}
                    }
                }
            }

            Text {
                visible: !controller.recording
                width: parent.width
                text: "Recording is paused. Nothing you open is added until you resume."
                color: controller.gold
                font.family: controller.uiFont
                font.pixelSize: 0.9*u
            }
            Text {
                visible: root.sessions.length === 0
                width: parent.width
                text: "Nothing recorded yet. Open something from Feria and it appears here."
                color: controller.mist
                font.family: controller.uiFont
                font.pixelSize: 1.2*u
            }

            Repeater {
                id: sessionRepeater
                model:root.sessions
                delegate:Item {
                    id: sessionDelegate
                    required property var modelData
                    required property int index
                    property bool newDay:index===0||controller.sessionDay(root.sessions[index-1])!==controller.sessionDay(modelData)
                    property int historyActionIndex:root.actionIndexForSession(index)
                    width:body.width
                    height:(newDay?3.2*u:0)+5.8*u+0.6*u
                    Text {
                        visible:parent.newDay
                        x:0;y:0
                        text:controller.sessionDay(modelData)
                        color:controller.mist
                        font.family:controller.displayFont
                        font.pixelSize:1.35*u
                        font.weight:Font.Medium
                    }
                    Rectangle {
                        id: sessionCard
                        objectName:modelData.id?"account-history-session-action-"+sessionDelegate.historyActionIndex:"account-history-session-passive-"+index
                        x:0;y:parent.newDay?2.2*u:0
                        width:parent.width;height:5.8*u;radius:1*u
                        color:Qt.rgba(1,1,1,0.035)
                        border.width:modelData.id&&controller.accountHistoryFocusIndex===sessionDelegate.historyActionIndex?0.1875*u:1
                        border.color:modelData.id&&controller.accountHistoryFocusIndex===sessionDelegate.historyActionIndex?controller.gold:Qt.rgba(1,1,1,0.08)
                        RowLayout {
                            anchors { fill: parent; leftMargin: 0.7*u; rightMargin: 1.3*u }
                            spacing: 1.2*u
                            Rectangle {
                                Layout.preferredWidth:3.4*u;Layout.preferredHeight:3.4*u;radius:modelData.id&&Data.T[modelData.id]&&Data.T[modelData.id].k==="artist"?1.7*u:0.55*u;clip:true;color:Qt.rgba(1,1,1,0.05)
                                Image{anchors.fill:parent;visible:modelData.id&&status===Image.Ready;source:modelData.id?controller.artUrl(Data.T[modelData.id],false):"";fillMode:Image.PreserveAspectCrop;asynchronous:true}
                                PorticoCombinedGlyph{anchors.centerIn:parent;width:1.7*u;height:1.7*u;visible:!modelData.id;glyphKey:modelData.pk;tone:controller.ink}
                            }
                            ColumnLayout {
                                Layout.fillWidth:true;spacing:0.25*u
                                Text{text:modelData.id?Data.T[modelData.id].t:controller.providerName(modelData.pk)+" home page";color:controller.ink;font.family:controller.uiFont;font.pixelSize:1.08*u;font.weight:Font.DemiBold;elide:Text.ElideRight;Layout.fillWidth:true}
                                Text{text:"Opened on "+controller.providerName(modelData.pk)+" at "+Qt.formatTime(new Date(modelData.at),"h:mm AP")+(modelData.sample?"":"  ·  Recorded in this prototype");color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.88*u}
                            }
                            Column {
                                Layout.alignment:Qt.AlignRight|Qt.AlignVCenter
                                Text{text:controller.durationText(modelData.mins);color:controller.ink;font.family:controller.displayFont;font.pixelSize:1.45*u;font.weight:Font.Medium;horizontalAlignment:Text.AlignRight;width:implicitWidth}
                                Text{text:"on "+controller.providerName(modelData.pk);color:controller.slate;font.family:controller.uiFont;font.pixelSize:0.8*u;horizontalAlignment:Text.AlignRight;width:parent.width}
                            }
                        }
                        MouseArea{
                            anchors.fill:parent
                            enabled:!!modelData.id
                            hoverEnabled:!!modelData.id
                            onEntered:if(modelData.id)controller.setAccountHistoryFocus(sessionDelegate.historyActionIndex)
                            onClicked:{
                                if(modelData.id){
                                    controller.setAccountHistoryFocus(sessionDelegate.historyActionIndex)
                                    controller.openTitle(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
