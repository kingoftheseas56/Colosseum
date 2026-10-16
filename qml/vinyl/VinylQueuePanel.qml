import QtQuick
import QtQuick.Controls
import QtQuick.Window
import ".."

Popup {
    id: root
    objectName: "vinylQueuePanel"
    required property var pageData
    required property Item backdrop
    property Item returnFocus: null
    signal songRequested()
    width: Math.min(720, parent ? parent.width - 32 : 720)
    height: Math.min(590, parent ? parent.height - 32 : 590)
    x: parent ? (parent.width-width)/2 : 0
    y: parent ? (parent.height-height)/2 : 0
    modal: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {returnFocus=backdrop.Window.window ? backdrop.Window.window.activeFocusItem : null;}
    onOpened: list.forceActiveFocus()
    onClosed: {if(returnFocus) returnFocus.forceActiveFocus();}
    Theme {id: theme}
    background: Rectangle {color: theme.biblioWashBottom;radius:22;border.color:theme.edge}
    component Action: KeyboardAction {
        id: action
        property string label
        width: Math.max(46, textItem.implicitWidth+20)
        height: 34
        accessibleName: label
        opacity: enabled ? 1 : 0.35
        Rectangle {anchors.fill:parent;radius:9;color:action.interactionActive ? theme.glassHi : theme.glassTint;border.color:theme.edge}
        Text {id:textItem;anchors.centerIn:parent;text:action.label;color:theme.ink;font.family:theme.ui;font.pixelSize:11}
    }
    contentItem: Column {
        spacing: 14
        Row {
            width: parent.width
            spacing: 14
            BackAction {id:closeButton;objectName:"vinylQueueClose";onTriggered:root.close()}
            Text {text:"Queue · "+root.pageData.queue.length;color:theme.ink;font.family:theme.display;font.pixelSize:25}
        }
        Flow {
            id: controls
            width: parent.width
            spacing: 7
            Action {objectName:"vinylQueuePrevious";label:"Previous";enabled:root.pageData.canPrevious;onTriggered:root.pageData.next(-1)}
            Action {objectName:"vinylQueueNext";label:"Next";enabled:root.pageData.canNext;onTriggered:root.pageData.next(1)}
            Action {objectName:"vinylQueueShuffle";label:root.pageData.shuffleEnabled ? "Shuffle on" : "Shuffle off";onTriggered:root.pageData.setShuffle(!root.pageData.shuffleEnabled)}
            Action {objectName:"vinylQueueRepeat";label:"Repeat: "+root.pageData.repeatMode;onTriggered:root.pageData.cycleRepeat()}
            Action {objectName:"vinylQueueClear";label:"Clear";enabled:root.pageData.queue.length>0;onTriggered:root.pageData.clearQueue()}
        }
        Text {
            width:parent.width;visible:root.pageData.queue.length===0
            text:"Your queue is empty. Play a release or add tracks from an album."
            color:theme.inkDim;font.family:theme.ui;font.pixelSize:14;wrapMode:Text.WordWrap
        }
        ListView {
            id: list
            objectName: "vinylQueueList"
            width: parent.width
            height: Math.max(60,root.availableHeight-controls.height-64)
            clip:true
            spacing:6
            model:root.pageData.queue
            currentIndex:root.pageData.queueIndex
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: "Music queue"
            Accessible.description: "Up and down select a track. Enter plays it. Delete removes it. Control with up or down moves it."
            Keys.onPressed: event => {
                if(!count) return;
                var selected=Math.max(0,currentIndex);
                if(event.key===Qt.Key_Up || event.key===Qt.Key_Down) {
                    var target=Math.max(0,Math.min(count-1,selected+(event.key===Qt.Key_Up ? -1 : 1)));
                    if(event.modifiers & Qt.ControlModifier) root.pageData.moveQueue(selected,target);
                    currentIndex=target;positionViewAtIndex(target,ListView.Contain);
                } else if(event.key===Qt.Key_Return || event.key===Qt.Key_Enter) root.pageData.playQueue(selected);
                else if(event.key===Qt.Key_Delete) {root.pageData.removeQueue(selected);currentIndex=Math.min(selected,count-1);}
                else return;
                event.accepted=true;
            }
            ScrollBar.vertical: ScrollBar {policy:ScrollBar.AlwaysOff}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width:list.width;height:68;radius:10
                color:index===root.pageData.queueIndex ? theme.glassHi : theme.glassTint
                border.color:list.activeFocus && list.currentIndex===index ? theme.gold : theme.edge
                KeyboardAction {
                    objectName:"vinylQueuePlay_"+row.index
                    x:10;y:4;width:Math.max(80,parent.width-160);height:60
                    accessibleName:"Play "+row.modelData.title
                    onTriggered:root.pageData.playQueue(row.index)
                    Column {
                        anchors.verticalCenter:parent.verticalCenter;width:parent.width;spacing:4
                        Text {width:parent.width;text:(row.index===root.pageData.queueIndex ? "▶  " : "")+row.modelData.title;elide:Text.ElideRight;color:theme.ink;font.family:theme.ui;font.pixelSize:13}
                        Text {width:parent.width;text:row.modelData.artist+" · "+row.modelData.edition.name+" · Disc "+row.modelData.discNumber;elide:Text.ElideRight;color:theme.inkDim;font.family:theme.ui;font.pixelSize:10}
                    }
                }
                Row {
                    anchors.right:parent.right;anchors.rightMargin:9;anchors.verticalCenter:parent.verticalCenter;spacing:5
                    Action {objectName:"vinylQueueUp_"+row.index;label:"↑";accessibleName:"Move up "+row.modelData.title;enabled:row.index>0;onTriggered:root.pageData.moveQueue(row.index,row.index-1)}
                    Action {objectName:"vinylQueueDown_"+row.index;label:"↓";accessibleName:"Move down "+row.modelData.title;enabled:row.index<root.pageData.queue.length-1;onTriggered:root.pageData.moveQueue(row.index,row.index+1)}
                    Action {objectName:"vinylQueueRemove_"+row.index;label:"×";accessibleName:"Remove "+row.modelData.title;onTriggered:root.pageData.removeQueue(row.index)}
                }
            }
        }
    }
}
