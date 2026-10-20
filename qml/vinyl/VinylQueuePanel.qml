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
    property bool keyboardSelection: false
    signal songRequested()
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(740, parent ? parent.width - 32 : 740)
    height: Math.min(620, parent ? parent.height - 48 : 620)
    padding: width < 540 ? 16 : 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: returnFocus=backdrop.Window.window ? backdrop.Window.window.activeFocusItem : null
    onOpened: {
        keyboardSelection=false;
        list.currentIndex=Math.max(0,pageData.queueIndex);
        list.positionViewAtIndex(list.currentIndex,ListView.Contain);
        list.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: {rowMenu.close();if(returnFocus)returnFocus.forceActiveFocus(Qt.PopupFocusReason);}
    Theme { id: theme }
    function controlIcon(symbol, selected) {
        var paths = {
            shuffle: '<path d="m18 3 3 3-3 3M18 15l3 3-3 3M3 6h3c5 0 7 12 12 12h3M3 18h3c2 0 3-2 5-5M14 8c1-1 2-2 4-2h3"/>',
            repeat: '<path d="m17 2 4 4-4 4M3 11V9a3 3 0 0 1 3-3h15M7 22l-4-4 4-4M21 13v2a3 3 0 0 1-3 3H3"/>',
            clear: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7"/>'
        };
        return "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="'
            + (selected ? theme.gold : theme.inkDim) + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">' + paths[symbol] + '</svg>');
    }
    background: Glass {backdrop:root.backdrop;radius:22;tint:0.1;scrim:0.94}
    Overlay.modal: Rectangle {color:Qt.rgba(0,0,0,0.55)}

    component Action: KeyboardAction {
        id: action
        property string label: ""
        property url icon: ""
        property string symbol: ""
        property bool iconOnly: false
        property bool subtleFocus: false
        property bool selected: false
        width: iconOnly || symbol.length || icon.toString().length ? 34 : Math.max(44,caption.implicitWidth+20)
        height: 34
        focusRadius: 8
        focusOnPointer: false
        showFocusFrame: !subtleFocus
        accessibleName: label
        opacity: enabled ? 1 : 0.3
        Rectangle {anchors.fill:parent;radius:8;color:action.interactionActive ? theme.glassTint : "transparent"}
        Rectangle {x:0;anchors.verticalCenter:parent.verticalCenter;width:2;height:16;radius:1;color:theme.inkDim;visible:action.subtleFocus&&action.activeFocus}
        Image {anchors.centerIn:parent;width:16;height:16;source:action.symbol.length ? root.controlIcon(action.symbol,action.selected) : action.icon;visible:action.symbol.length>0||action.icon.toString().length>0;opacity:0.85}
        Rectangle {anchors.horizontalCenter:parent.horizontalCenter;y:parent.height-3;width:3;height:3;radius:2;color:theme.gold;visible:action.selected}
        Text {id:caption;anchors.centerIn:parent;visible:!action.iconOnly&&!action.symbol.length&&!action.icon.toString().length;text:action.label;textFormat:Text.PlainText;color:action.selected ? theme.gold : theme.inkDim;font.family:theme.ui;font.pixelSize:12}
    }
    contentItem: Item {
        Item {
            id: heading
            width:parent.width;height:48
            Text {text:"Queue";color:theme.ink;font.family:theme.display;font.pixelSize:27}
            Text {
                y:34;text:root.pageData.queue.length+(root.pageData.queue.length===1 ? " track" : " tracks")
                color:theme.inkDimmer;font.family:theme.ui;font.pixelSize:11
            }
            Action {objectName:"vinylQueueClose";anchors.right:parent.right;label:"Close queue";icon:"../../assets/icons/reader2/close.svg";onTriggered:root.close()}
        }
        Flow {
            id: controls
            y:heading.height+14;width:parent.width;spacing:6
            Action {objectName:"vinylQueuePrevious";label:"Previous track";icon:"../../assets/icons/lucide/skip-back.svg";enabled:root.pageData.canPrevious;onTriggered:root.pageData.next(-1)}
            Action {objectName:"vinylQueueNext";label:"Next track";icon:"../../assets/icons/lucide/skip-forward.svg";enabled:root.pageData.canNext;onTriggered:root.pageData.next(1)}
            Action {objectName:"vinylQueueShuffle";symbol:"shuffle";label:"Shuffle";selected:root.pageData.shuffleEnabled;accessibleName:selected ? "Turn shuffle off" : "Turn shuffle on";onTriggered:root.pageData.setShuffle(!root.pageData.shuffleEnabled)}
            Action {
                objectName:"vinylQueueRepeat";symbol:"repeat";label:"Repeat: "+root.pageData.repeatMode
                selected:root.pageData.repeatMode!=="off";onTriggered:root.pageData.cycleRepeat()
                Text {anchors.centerIn:parent;visible:root.pageData.repeatMode==="one";text:"1";font.family:theme.ui;font.pixelSize:9;font.bold:true;color:theme.gold}
            }
            Action {objectName:"vinylQueueClear";symbol:"clear";label:"Clear queue";enabled:root.pageData.queue.length>0;onTriggered:root.pageData.clearQueue()}
        }
        Rectangle {id:rule;y:controls.y+controls.height+12;width:parent.width;height:1;color:theme.edge;opacity:0.45}
        ListView {
            id: list
            objectName: "vinylQueueList"
            y:rule.y+12;width:parent.width;height:Math.max(0,parent.height-y)
            clip:true;spacing:0
            model:root.pageData.queue
            currentIndex:root.pageData.queueIndex
            activeFocusOnTab:true
            Accessible.role:Accessible.List
            Accessible.name:"Music queue"
            Accessible.description:"Up and down select a track. Enter plays it. Delete removes it. Control with up or down moves it."
            Keys.onPressed:event=>{
                if(!count)return;
                var selected=Math.max(0,currentIndex);
                if(event.key===Qt.Key_Up || event.key===Qt.Key_Down){
                    root.keyboardSelection=true;
                    var target=Math.max(0,Math.min(count-1,selected+(event.key===Qt.Key_Up ? -1 : 1)));
                    if(event.modifiers & Qt.ControlModifier)root.pageData.moveQueue(selected,target);
                    currentIndex=target;positionViewAtIndex(target,ListView.Contain);
                }else if(event.key===Qt.Key_Return || event.key===Qt.Key_Enter)root.pageData.playQueue(selected);
                else if(event.key===Qt.Key_Delete){root.pageData.removeQueue(selected);currentIndex=Math.min(selected,count-1);}
                else if(event.key===Qt.Key_Menu || (event.key===Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))){
                    var row=itemAtIndex(selected);if(row)root.openRowMenu(row.modelData.queueId,row);
                }else return;
                event.accepted=true;
            }
            ScrollBar.vertical:ScrollBar {policy:ScrollBar.AlwaysOff}
            delegate:Item {
                id:row
                required property var modelData
                required property int index
                readonly property bool playing:index===root.pageData.queueIndex
                readonly property bool selected:(list.activeFocus && root.keyboardSelection && list.currentIndex===index)||playAction.activeFocus
                width:list.width;height:54
                HoverHandler {id:rowHover}
                Rectangle {anchors.fill:parent;radius:8;color:theme.glassTint;opacity:rowHover.hovered||row.selected ? 0.55 : row.playing ? 0.22 : 0}
                Rectangle {x:0;anchors.verticalCenter:parent.verticalCenter;width:2;height:18;radius:1;color:theme.inkDim;visible:row.selected}
                KeyboardAction {
                    id:playAction
                    objectName:"vinylQueuePlay_"+row.index
                    width:Math.max(0,parent.width-38);height:parent.height
                    focusOnPointer:false;showFocusFrame:false
                    accessibleName:"Play "+row.modelData.title+(row.playing ? ", now playing" : "")
                    onActiveFocusChanged:if(activeFocus)list.currentIndex=row.index
                    onTriggered:{root.keyboardSelection=false;list.currentIndex=row.index;list.forceActiveFocus(Qt.MouseFocusReason);root.pageData.playQueue(row.index);}
                    Text {x:8;anchors.verticalCenter:parent.verticalCenter;width:25;text:String(row.index+1).padStart(2,"0");color:row.playing ? theme.gold : theme.inkDimmer;font.family:theme.ui;font.pixelSize:11}
                    RoundedPosterImage {x:40;y:9;width:36;height:36;radius:5;sources:(row.modelData.cover || row.modelData.edition.cover) ? [root.pageData.artwork(row.modelData.cover || row.modelData.edition.cover)] : []}
                    Column {
                        x:88;anchors.verticalCenter:parent.verticalCenter;width:Math.max(0,parent.width-x-58);spacing:3
                        Text {width:parent.width;text:row.modelData.title;textFormat:Text.PlainText;elide:Text.ElideRight;color:theme.ink;font.family:theme.ui;font.pixelSize:13;font.weight:row.playing ? Font.DemiBold : Font.Normal}
                        Text {width:parent.width;text:row.modelData.artist+" · "+row.modelData.edition.name+(row.modelData.edition.discs.length>1 ? " · Disc "+row.modelData.discNumber : "");textFormat:Text.PlainText;elide:Text.ElideRight;color:theme.inkDimmer;font.family:theme.ui;font.pixelSize:10}
                    }
                    Text {anchors.right:parent.right;anchors.rightMargin:10;anchors.verticalCenter:parent.verticalCenter;text:row.modelData.seconds>0 ? root.pageData.formatTime(row.modelData.seconds) : "";color:theme.inkDimmer;font.family:theme.ui;font.pixelSize:11}
                }
                Action {
                    objectName:"vinylQueueMore_"+row.index
                    anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter
                    label:"Track actions for "+row.modelData.title
                    iconOnly:true
                    onTriggered:root.openRowMenu(row.modelData.queueId,parent)
                    Text {anchors.centerIn:parent;text:"⋯";color:theme.inkDim;font.family:theme.ui;font.pixelSize:20}
                }
            }
        }
        Text {
            visible:list.count===0;anchors.centerIn:list;width:Math.min(300,list.width)
            text:"Your queue is empty";color:theme.inkDim;font.family:theme.display;font.pixelSize:22;horizontalAlignment:Text.AlignHCenter;wrapMode:Text.WordWrap
        }
    }
    function openRowMenu(queueId,row){
        rowMenu.queueId=queueId;
        var point=row.mapToItem(root.contentItem,0,0);
        rowMenu.x=root.availableWidth-rowMenu.width;
        rowMenu.y=Math.max(0,Math.min(root.availableHeight-rowMenu.height,point.y+row.height));
        rowMenu.open();
    }
    Popup {
        id:rowMenu
        parent:root.contentItem
        objectName:"vinylQueueTrackMenu"
        property string queueId:""
        readonly property int queueIndex:root.pageData.queue.findIndex(row=>row.queueId===queueId)
        width:200;padding:6;focus:true
        closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {radius:12;color:theme.biblioWashTop;border.color:theme.edge}
        onOpened:(menuUp.enabled ? menuUp : menuDown.enabled ? menuDown : menuRemove).forceActiveFocus(Qt.PopupFocusReason)
        onClosed:if(root.opened)list.forceActiveFocus(Qt.PopupFocusReason)
        contentItem:Column {
            Action {id:menuUp;objectName:"vinylQueueMenuUp";width:parent.width;label:"Move up";subtleFocus:true;enabled:rowMenu.queueIndex>0;Keys.onDownPressed:(menuDown.enabled ? menuDown : menuRemove).forceActiveFocus(Qt.TabFocusReason);onTriggered:{var i=rowMenu.queueIndex;rowMenu.close();root.pageData.moveQueue(i,i-1);}}
            Action {id:menuDown;objectName:"vinylQueueMenuDown";width:parent.width;label:"Move down";subtleFocus:true;enabled:rowMenu.queueIndex>=0&&rowMenu.queueIndex<root.pageData.queue.length-1;Keys.onDownPressed:menuRemove.forceActiveFocus(Qt.TabFocusReason);Keys.onUpPressed:if(menuUp.enabled)menuUp.forceActiveFocus(Qt.BacktabFocusReason);onTriggered:{var i=rowMenu.queueIndex;rowMenu.close();root.pageData.moveQueue(i,i+1);}}
            Action {id:menuRemove;objectName:"vinylQueueMenuRemove";width:parent.width;label:"Remove from queue";subtleFocus:true;enabled:rowMenu.queueIndex>=0;Keys.onUpPressed:(menuDown.enabled ? menuDown : menuUp).forceActiveFocus(Qt.BacktabFocusReason);onTriggered:{var i=rowMenu.queueIndex;rowMenu.close();root.pageData.removeQueue(i);}}
        }
    }
}
