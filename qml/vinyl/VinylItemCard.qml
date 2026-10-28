import QtQuick
import QtQuick.Controls
import ".."

KeyboardAction {
    id: root
    focusOnPointer: false
    required property var worldData
    required property var item
    property var recommendationRow: null
    property Flickable pageFlick: null
    property Item exposureRail: null
    property Item backdrop: null
    property bool homePageVisible: false
    property int exposureRevision: 0
    property int feedbackGeneration: -1
    property real feedbackMenuMaxHeight: 360
    property var feedbackOptions: []
    readonly property bool feedbackMenuOpen: feedbackMenu.opened
    readonly property bool feedbackAvailable: !!recommendationRow
        && (recommendationRow.recommendation === true
            || ((recommendationRow.key === "taste-artists" || recommendationRow.key === "saved-artists")
                && recommendationRow.kind === "artists"))
        && !!item && !!item.ref
        && worldData && typeof worldData.setRecommendationFeedback === "function"
        && typeof worldData.recommendationProfileGeneration === "function"
        && typeof worldData.recommendationArtistRef === "function"

    enabled: root.item.unavailable !== true
    contextEnabled: feedbackAvailable
    readonly property string reason: item.reason || ""
    implicitHeight: art.height + 14 + copy.height + 8
    accessibleName: item.title + (item.unavailable ? ", unavailable" : "")
        + (reason ? ". " + reason : "")
    onTriggered: if(worldData) worldData.activate(item)
    onContextRequested: openFeedbackMenu()

    function scheduleExposureCheck() { exposureTimer.restart(); }
    function feedbackActionFor(key) {
        var index=feedbackOptions.findIndex(function(option){return option.key===key;});
        return index>=0 ? feedbackRepeater.itemAt(index) : null;
    }
    function clipTo(bounds, clipItem) {
        var corner=clipItem.mapToItem(null,0,0);
        bounds.left=Math.max(bounds.left,corner.x);
        bounds.top=Math.max(bounds.top,corner.y);
        bounds.right=Math.min(bounds.right,corner.x+clipItem.width);
        bounds.bottom=Math.min(bounds.bottom,corner.y+clipItem.height);
    }
    function actuallyVisible() {
        if(!root.Window.window || !root.Window.window.visible
            || root.Window.window.visibility===Window.Minimized)return false;
        if(!homePageVisible || !root.visible || !pageFlick || !exposureRail
            || !pageFlick.visible || !exposureRail.visible || !recommendationRow
            || recommendationRow.recommendation !== true || !item || !item.ref)
            return false;
        var origin=root.mapToItem(null,0,0);
        var bounds={left:origin.x,top:origin.y,right:origin.x+root.width,bottom:origin.y+root.height};
        clipTo(bounds,pageFlick);
        clipTo(bounds,exposureRail);

        var ancestor=root.parent;
        while(ancestor) {
            if(ancestor.visible===false || Number(ancestor.opacity)<=0) return false;
            if(ancestor.clip) clipTo(bounds,ancestor);
            if(ancestor===pageFlick) break;
            ancestor=ancestor.parent;
        }
        return bounds.right>bounds.left && bounds.bottom>bounds.top;
    }
    function recordExposureIfVisible() {
        if(!feedbackAvailable || typeof worldData.recordVisibleRecommendation!=="function") return false;
        return worldData.recordVisibleRecommendation(recommendationRow,item,
            recommendationRow.profileGeneration,actuallyVisible());
    }
    function openFeedbackMenu() {
        if(!feedbackAvailable || Number(recommendationRow.profileGeneration)
            !== worldData.recommendationProfileGeneration()) return false;
        feedbackGeneration=worldData.recommendationProfileGeneration();
        feedbackMenu.keyboardEntry=feedbackButton.activeFocus || root.activeFocus;
        var options=[];
        var artistFeedbackRow=recommendationRow.key==="taste-artists"
            || recommendationRow.key==="saved-artists";
        if(artistFeedbackRow) {
            var artistName=String(item.artist || item.title || "this artist");
            options.push({key:"exclude-current-artist",action:"exclude-artist",
                label:"Show less from "+artistName,ref:item.ref});
            options.push({key:"allow-current-artist",action:"allow-artist",
                label:"Allow "+artistName+" again",ref:item.ref});
            var suppressed=typeof worldData.suppressedRecommendationsForArtist==="function"
                ? worldData.suppressedRecommendationsForArtist(item.ref) : [];
            var suppressedCount=suppressed && Number.isInteger(Number(suppressed.length))
                ? Math.min(300,Number(suppressed.length)) : 0;
            for(var suppressedIndex=0;suppressedIndex<suppressedCount && options.length<300;suppressedIndex++) {
                var entry=suppressed[suppressedIndex];
                if(!entry || !entry.ref) continue;
                var suppressedTitle=String(entry.title || "this recommendation").trim();
                if(entry.hidden===true && options.length<300)
                    options.push({key:"unhide-suppressed-"+suppressedIndex,action:"unhide",
                        label:"Show "+suppressedTitle+" again",ref:entry.ref});
                if(entry.snoozed===true && options.length<300)
                    options.push({key:"unsnooze-suppressed-"+suppressedIndex,action:"unsnooze",
                        label:"Stop hiding "+suppressedTitle+" for 7 days",ref:entry.ref});
                if(entry.excluded!==true || options.length>=300) continue;
                var excludedArtists=Array.isArray(entry.excludedArtistRefs) ? entry.excludedArtistRefs : [];
                var validExcludedCount=0;
                for(var excludedIndex=0;excludedIndex<excludedArtists.length && options.length<300;excludedIndex++) {
                    var excluded=excludedArtists[excludedIndex];
                    if(!excluded || !excluded.ref || excluded.ref.identity!=="provider"
                        || excluded.ref.kind!=="artist" || !excluded.ref.providerId || !excluded.ref.itemId) continue;
                    var excludedName=String(excluded.name || "").trim();
                    if(!excludedName && typeof worldData.recommendationRefsEqual==="function"
                        && worldData.recommendationRefsEqual(excluded.ref,item.ref))
                        excludedName=artistName;
                    if(!excludedName) continue;
                    validExcludedCount++;
                    options.push({key:"allow-excluded-artist-"+suppressedIndex+"-"+excludedIndex,
                        action:"allow-artist",label:"Allow "+excludedName+" again (from "+suppressedTitle+")",
                        ref:excluded.ref});
                }
                if(validExcludedCount===0 && entry.ref.kind==="artist" && options.length<300) {
                    var excludedRefName=String(entry.title || artistName).trim();
                    options.push({key:"allow-excluded-artist-"+suppressedIndex+"-0",
                        action:"allow-artist",label:"Allow "+excludedRefName+" again (from "+suppressedTitle+")",
                        ref:entry.ref});
                }
            }
        } else {
            options=[
                {key:"like",action:"like",label:"Like this recommendation",ref:item.ref},
                {key:"dislike",action:"dislike",label:"Dislike this recommendation",ref:item.ref},
                {key:"neutral",action:"neutral",label:"Clear feedback",ref:item.ref},
                {key:"hide",action:"hide",label:"Hide this recommendation",ref:item.ref},
                {key:"unhide",action:"unhide",label:"Show this recommendation again",ref:item.ref},
                {key:"snooze-7d",action:"snooze-7d",label:"Hide for 7 days",ref:item.ref},
                {key:"unsnooze",action:"unsnooze",label:"Stop hiding for 7 days",ref:item.ref}
            ];
        }
        var artists=root.item ? root.item.artistRefs : null;
        var artistCount=!artistFeedbackRow && artists && Number.isInteger(Number(artists.length))
            ? Number(artists.length) : 0;
        for(var index=0;index<artistCount;index++) {
            var artist=artists[index];
            var ref=worldData.recommendationArtistRef(artist);
            if(!ref) continue;
            var name=String(artist.name || artist.title || "").trim();
            if(!name) continue;
            options.push({key:"exclude-artist-"+index,action:"exclude-artist",
                label:"Show less from "+name,ref:ref});
            options.push({key:"allow-artist-"+index,action:"allow-artist",
                label:"Allow "+name+" again",ref:ref});
        }
        feedbackOptions=options;

        var overlay=Overlay.overlay;
        var viewportOrigin=pageFlick.mapToItem(overlay,0,0);
        var anchor=feedbackButton.mapToItem(overlay,0,feedbackButton.height+6);
        var viewportWidth=pageFlick.width;
        var viewportHeight=pageFlick.height;
        feedbackMenuMaxHeight=Math.max(80,Math.min(360,viewportHeight-16));
        var menuWidth=feedbackMenu.width;
        var pageX=Math.max(0,Math.min(viewportWidth-menuWidth,
            feedbackButton.mapToItem(pageFlick,0,0).x+feedbackButton.width-menuWidth));
        feedbackMenu.x=viewportOrigin.x+pageX;
        var below=anchor.y+feedbackMenu.height<=viewportOrigin.y+viewportHeight-8;
        feedbackMenu.y=below ? anchor.y : Math.max(viewportOrigin.y+8,
            feedbackButton.mapToItem(overlay,0,0).y-feedbackMenu.height-6);
        feedbackMenu.open();
        return true;
    }

    Theme { id: theme }
    RoundedPosterImage {
        id: art
        objectName: root.objectName + "_cover"
        width: root.width
        height: width / (root.item.aspect || 1)
        radius: root.item.round ? width / 2 : 13
        sources: root.worldData && root.item && root.worldData.artwork(root.item.cover)
            ? [root.worldData.artwork(root.item.cover)] : []
        hovered: root.interactionActive
    }
    Column {
        id: copy
        y: art.height + 14
        width: parent.width
        spacing: 6
        Text {
            width: parent.width
            text: root.item.title
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: root.item.unavailable ? "Unavailable" : root.reason || root.item.subtitle || ""
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.25
        }
    }

    KeyboardAction {
        id: feedbackButton
        objectName: "vinylRecommendationActions_" + String(root.item.id || "item")
        x: art.width-width-7
        y: 7
        z: 2
        width: 36
        height: 36
        visible: root.feedbackAvailable
        enabled: visible && root.enabled
        focusOnPointer: false
        contextEnabled: enabled
        focusRadius: 11
        accessibleName: (root.recommendationRow && (root.recommendationRow.key === "taste-artists"
                || root.recommendationRow.key === "saved-artists")
            ? "Artist feedback options for " : "More recommendation options for ") + root.item.title
        accessibleDescription: "Choose how recommendations learn from this item."
        onTriggered: root.openFeedbackMenu()
        onContextRequested: root.openFeedbackMenu()
        Glass {
            anchors.fill: parent
            backdrop: root.backdrop
            radius: 11
            tint: feedbackButton.interactionActive ? 0.18 : 0.08
            scrim: 0.74
        }
        Row {
            anchors.centerIn: parent
            spacing: 3
            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    width: 3
                    height: 3
                    radius: 2
                    color: theme.ink
                    opacity: feedbackButton.interactionActive ? 1 : 0.8
                }
            }
        }
    }

    Popup {
        id: feedbackMenu
        property bool keyboardEntry: false
        objectName: "vinylRecommendationFeedbackMenu_" + String(root.item.id || "item")
        parent: Overlay.overlay
        focus: true
        padding: 12
        width: Math.max(80,Math.min(280,root.pageFlick ? root.pageFlick.width-16 : 260))
        height: Math.min(root.feedbackMenuMaxHeight,menuActions.implicitHeight+24)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Glass { backdrop: root.backdrop; radius: 16; tint: 0.12; scrim: 0.64 }
        onOpened: {
            if(!keyboardEntry) { menuActions.forceActiveFocus(Qt.PopupFocusReason); return; }
            var first=feedbackRepeater.itemAt(0);
            if(first && first.enabled) first.forceActiveFocus(Qt.PopupFocusReason);
        }
        onClosed: if(keyboardEntry && root.visible && feedbackButton.visible && feedbackButton.enabled)
            feedbackButton.forceActiveFocus(Qt.TabFocusReason)
        contentItem: Flickable {
            id: menuScroll
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: menuActions.implicitHeight
            Column {
                id: menuActions
                width: menuScroll.width
                spacing: 5
                Keys.onPressed: event => {
                    if([Qt.Key_Down,Qt.Key_Up,Qt.Key_Tab].indexOf(event.key)<0)return;
                    var at=event.key===Qt.Key_Up ? feedbackRepeater.count-1 : 0;
                    var target=feedbackRepeater.itemAt(at);
                    if(target)target.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted=true;
                }
                Repeater {
                    id: feedbackRepeater
                    model: root.feedbackOptions
                    delegate: KeyboardAction {
                        id: menuAction
                        required property var modelData
                        required property int index
                        width: menuActions.width
                        height: Math.max(38,actionLabel.implicitHeight+12)
                        focusOnPointer: false
                        focusRadius: 9
                        accessibleName: modelData.label
                        objectName: "vinylFeedbackAction_" + modelData.key
                        KeyNavigation.up: index>0 ? feedbackRepeater.itemAt(index-1) : null
                        KeyNavigation.down: index+1<feedbackRepeater.count
                            ? feedbackRepeater.itemAt(index+1) : null
                        onActiveFocusChanged: if(activeFocus) {
                            if(y<menuScroll.contentY) menuScroll.contentY=y;
                            else if(y+height>menuScroll.contentY+menuScroll.height)
                                menuScroll.contentY=y+height-menuScroll.height;
                        }
                        onTriggered: {
                            // Applying feedback can replace this delegate immediately.
                            var data=root.worldData,ref=modelData.ref,action=modelData.action;
                            var generation=root.feedbackGeneration;
                            feedbackMenu.close();
                            data.setRecommendationFeedback(ref,action,generation);
                        }
                        Glass {
                            anchors.fill: parent
                            backdrop: root.backdrop
                            radius: 9
                            tint: 0.06
                            scrim: menuAction.interactionActive ? 0.24 : 0.08
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: menuAction.interactionActive ? theme.glassTint : "transparent"
                        }
                        Text {
                            id: actionLabel
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.label
                            textFormat: Text.PlainText
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Timer {
        id: exposureTimer
        interval: 120
        repeat: false
        onTriggered: root.recordExposureIfVisible()
    }
    Connections {
        target: root.worldData ? root.worldData.session : null
        ignoreUnknownSignals: true
        function onProfileGenerationChanged() { feedbackMenu.close(); root.scheduleExposureCheck() }
    }
    onVisibleChanged: scheduleExposureCheck()
    onXChanged: scheduleExposureCheck()
    onYChanged: scheduleExposureCheck()
    onWidthChanged: scheduleExposureCheck()
    onHeightChanged: scheduleExposureCheck()
    onItemChanged: scheduleExposureCheck()
    onRecommendationRowChanged: scheduleExposureCheck()
    onHomePageVisibleChanged: scheduleExposureCheck()
    onExposureRevisionChanged: scheduleExposureCheck()
    Component.onCompleted: scheduleExposureCheck()
}
