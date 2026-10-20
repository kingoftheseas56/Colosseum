import QtQuick
import QtQuick.Controls
import ".."

Popup {
    id: root
    objectName: "vinylArtistSearchDialog"
    required property var artistData
    property Item backdrop: null
    property alias query: input.text
    readonly property var matches: artistData && artistData.search ? artistData.search(query) : []
    signal matchRequested(var match)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(740, parent ? parent.width - 40 : 740)
    height: Math.min(650, parent ? parent.height - 48 : 650)
    padding: width < 540 ? 18 : 28
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Theme { id: theme }
    background: Glass { objectName:"vinylArtistSearchSurface"; backdrop:root.backdrop; radius:24; tint:0.12; scrim:0.86 }
    Overlay.modal: Rectangle { color: Qt.rgba(0,0,0,0.64) }
    onOpened: { results.currentIndex=-1; results.positionViewAtBeginning(); input.forceActiveFocus(Qt.PopupFocusReason); }
    onMatchesChanged: if(results) { results.currentIndex=-1; results.positionViewAtBeginning(); }
    function focusResult(index) {
        if(index<0){input.forceActiveFocus(Qt.BacktabFocusReason);return;}
        if(index>=results.count)return;
        results.currentIndex=index;results.positionViewAtIndex(index,ListView.Contain);
        Qt.callLater(function(){var row=results.itemAtIndex(index);if(row)row.forceActiveFocus(Qt.TabFocusReason);});
    }
    function activate(index) {
        if(index<0||index>=matches.length)return;
        var selected=matches[index];close();matchRequested(selected);
    }
    function coverFor(match) {
        var record=match.record || {};
        return artistData.artwork(record.cover || (record.album || {}).cover || ((record.song || {}).album || {}).cover || "");
    }
    contentItem: Item {
        Item {
            id: heading; width:parent.width;height:44
            Text {
                anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter
                width:parent.width-60;text:"Search "+root.artistData.record.name
                color:theme.ink;font.family:theme.display;font.pixelSize:28;elide:Text.ElideRight
            }
            KeyboardAction {
                id: closeAction;objectName:"vinylArtistSearchClose";anchors.right:parent.right
                width:40;height:40;focusRadius:20;accessibleName:"Close search";onTriggered:root.close()
                Rectangle {anchors.fill:parent;radius:20;color:closeAction.interactionActive ? theme.glassHi : theme.glassTint;border.width:1;border.color:theme.edge}
                Image {anchors.centerIn:parent;width:16;height:16;source:"../../assets/icons/reader2/close.svg"}
            }
        }
        TextField {
            id:input;objectName:"vinylArtistSearchInput";y:heading.height+20;width:parent.width;height:54
            maximumLength:200;selectByMouse:true;color:theme.ink;placeholderTextColor:theme.inkDimmer
            font.family:theme.ui;font.pixelSize:16;leftPadding:46;rightPadding:16
            placeholderText:"Albums, tracks and videos"
            background:Rectangle {radius:14;color:"#191c24";border.width:1;border.color:theme.edge}
            Image {x:16;anchors.verticalCenter:parent.verticalCenter;width:18;height:18;source:"../../assets/icons/search.svg";opacity:0.7}
            FocusRing {anchors.fill:parent;radius:14;shown:input.activeFocus}
            Keys.onDownPressed:event=>{root.focusResult(0);event.accepted=true;}
            onAccepted:root.activate(0)
        }
        Text {
            id:count;y:input.y+input.height+16;width:parent.width
            text:root.matches.length+ (root.matches.length===1 ? " result" : " results")
            color:theme.inkDimmer;font.family:theme.ui;font.pixelSize:12
        }
        ListView {
            id:results;objectName:"vinylArtistSearchResultsList"
            y:count.y+count.height+12;width:parent.width;height:Math.max(0,parent.height-y)
            clip:true;spacing:6;model:root.matches.length;currentIndex:-1
            boundsBehavior:Flickable.StopAtBounds
            ScrollGlide {flick:results}
            delegate:KeyboardAction {
                id:row;required property int index
                readonly property var match:root.matches[index]
                objectName:"vinylArtistSearchResult_"+index
                x:6;width:results.width-12;height:80;focusRadius:12
                accessibleName:match.label+", "+match.type;spaceActivates:true
                onTriggered:root.activate(index)
                onActiveFocusChanged:if(activeFocus){results.currentIndex=index;results.positionViewAtIndex(index,ListView.Contain);}
                Keys.onDownPressed:event=>{root.focusResult(index+1);event.accepted=true;}
                Keys.onUpPressed:event=>{root.focusResult(index-1);event.accepted=true;}
                Rectangle {anchors.fill:parent;radius:12;color:row.interactionActive ? Qt.rgba(0.94,0.77,0.29,0.12) : Qt.rgba(1,1,1,0.035)}
                RoundedPosterImage {
                    id:art;objectName:row.objectName+"_art";x:10;y:10;width:60;height:60;radius:8
                    sources:root.coverFor(row.match) ? [root.coverFor(row.match)] : []
                    hovered:row.interactionActive
                }
                Column {
                    x:84;anchors.verticalCenter:parent.verticalCenter;width:Math.max(0,parent.width-x-kind.width-26);spacing:6
                    Text {width:parent.width;text:row.match.label;textFormat:Text.PlainText;color:theme.ink;font.family:theme.ui;font.pixelSize:15;font.weight:Font.DemiBold;wrapMode:Text.WordWrap;maximumLineCount:2;elide:Text.ElideRight}
                    Text {width:parent.width;visible:text.length>0;text:row.match.extra || "";textFormat:Text.PlainText;color:theme.inkDim;font.family:theme.ui;font.pixelSize:12;elide:Text.ElideRight}
                }
                Text {id:kind;anchors.right:parent.right;anchors.rightMargin:14;anchors.verticalCenter:parent.verticalCenter;text:row.match.type;color:theme.gold;font.family:theme.ui;font.pixelSize:11}
            }
            Text {
                objectName:"vinylArtistSearchEmpty"
                visible:results.count===0;anchors.centerIn:parent;width:parent.width
                text:root.artistData.loading===true ? "Loading…" : "No matches";horizontalAlignment:Text.AlignHCenter
                color:theme.inkDim;font.family:theme.display;font.pixelSize:24
            }
        }
    }
}
