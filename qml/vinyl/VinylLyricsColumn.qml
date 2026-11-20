import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylLyricsColumn"
    required property var pageData
    property alias contentY: scroll.contentY
    readonly property real contentHeight: scroll.contentHeight
    readonly property bool following: pageData.timedLyrics && pageData.lyricsFollowing !== false
    readonly property bool timedDocument: pageData.timedLyrics || pageData.lyricsStatus === "line_synced"
    readonly property real footerHeight: (timedDocument || (showSourceCredit && pageData.lyricsSource === "lrclib")) ? 34 : 0
    readonly property string gestureContext: JSON.stringify([pageData.readingContextKey || "",
        pageData.lyricsDocumentId || "",pageData.timedLyrics])
    property bool showSourceCredit: true
    property bool ready: false
    property bool syncing: false
    property bool positioned: false
    readonly property string lyricFontFamily: lyricFace.name || theme.ui
    clip: true
    Theme { id: theme }
    FontLoader { id: lyricFace; source: Qt.resolvedUrl("../../assets/fonts/Roboto-Medium.ttf") }

    function rememberPosition() {
        if(typeof pageData.lyricsScrollY === "number")pageData.lyricsScrollY=scroll.contentY;
    }
    function followCurrentLine() { syncPosition(true); }
    function syncPosition(animated) {
        if(!ready || !visible)return;
        lines.forceLayout();
        var target=pageData.lyricsScrollY || 0;
        if(following) {
            var current=repeater.itemAt(Math.max(0,pageData.currentLineIndex));
            if(current)target=lines.y+current.y-scroll.height*0.34;
        }
        target=Math.max(0,Math.min(Math.max(0,scroll.contentHeight-scroll.height),target));
        followMotion.stop();
        if(animated === true && positioned && following && Math.abs(target-scroll.contentY)>0.5) {
            followMotion.from=scroll.contentY;followMotion.to=target;followMotion.start();
        }else {
            syncing=true;scroll.contentY=target;rememberPosition();syncing=false;
        }
        positioned=true;
    }
    function pauseFollowing() {
        followMotion.stop();
        if(pageData.pauseLyrics)pageData.pauseLyrics(scroll.contentY);
    }
    function scrollTo(y) {
        pauseFollowing();
        scroll.contentY=Math.max(0,Math.min(Math.max(0,scroll.contentHeight-scroll.height),y));
    }
    function activateLine(index) {
        if (!pageData.lyrics[index]) return;
        var followAuto = pageData.autoNotes === true && pageData.manualNoteLine < 0
            && pageData.timedLyrics && pageData.isCurrent !== false;
        if (pageData.timedLyrics) {
            scroll.cancelFlick();glide.cancelGlide("lyric-activation");
            pageData.seekToLine(index);
            if (pageData.resumeLyrics) pageData.resumeLyrics();
        }
        if (!followAuto) {
            if (typeof pageData.selectNoteLine === "function") pageData.selectNoteLine(index);
            else pageData.showNotes(index);
        }
    }
    onGestureContextChanged: {followMotion.stop();positioned=false;glide.cancelGlide("lyrics-context");Qt.callLater(syncPosition);}
    onFollowingChanged: {if(!following)followMotion.stop();Qt.callLater(followCurrentLine);}
    onWidthChanged: Qt.callLater(syncPosition)
    onHeightChanged: Qt.callLater(syncPosition)
    onVisibleChanged: {followMotion.stop();positioned=false;glide.cancelGlide("lyrics-visibility");if(visible)Qt.callLater(syncPosition);}
    Component.onCompleted: {ready=true;Qt.callLater(syncPosition);}

    NumberAnimation {
        id: followMotion
        target: scroll; property: "contentY"
        duration: 320; easing.type: Easing.OutCubic
        onFinished: root.rememberPosition()
    }
    Connections {
        target: root.pageData
        ignoreUnknownSignals: true
        function onCurrentLineIndexChanged() {if(root.following)Qt.callLater(root.followCurrentLine);}
        function onLyricsChanged() {followMotion.stop();Qt.callLater(root.syncPosition);}
        function onLyricsScrollYChanged() {if(!root.following)Qt.callLater(root.syncPosition);}
    }
    Flickable {
        id: scroll
        objectName: "vinylLyricsScroll"
        anchors.fill: parent
        anchors.bottomMargin: root.footerHeight > 0 ? root.footerHeight+4 : 0
        clip: true
        contentWidth: width
        contentHeight: Math.max(height,lines.y+lines.height+(root.timedDocument?height*0.62:0))
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        onHeightChanged: Qt.callLater(root.syncPosition)
        Accessible.role: Accessible.List
        Accessible.name: "Lyrics"
        onDraggingChanged: if(dragging)root.pauseFollowing()
        onContentYChanged: {
            if(root.ready && root.visible && !root.syncing) {
                if(followMotion.running)root.rememberPosition();
                else if(root.pageData.lyricsFollowing === false && root.pageData.pauseLyrics)root.pageData.pauseLyrics(contentY);
            }
        }
        Keys.onPressed: event => {
            var y=contentY;
            if(event.key===Qt.Key_Home)y=0;
            else if(event.key===Qt.Key_End)y=contentHeight-height;
            else if(event.key===Qt.Key_PageDown)y+=height*0.8;
            else if(event.key===Qt.Key_PageUp)y-=height*0.8;
            else if(event.key===Qt.Key_Down)y+=44;
            else if(event.key===Qt.Key_Up)y-=44;
            else return;
            root.scrollTo(y);event.accepted=true;
        }
        ScrollGlide { id: glide; flick: scroll; onWheelInput: root.pauseFollowing() }
        Column {
            id: lines
            width: scroll.width
            y: root.timedDocument?scroll.height*0.15:0
            onHeightChanged: Qt.callLater(root.syncPosition)
            Repeater {
                id: repeater
                model: root.pageData.lyrics
                delegate: Item {
                    id: line
                    required property var modelData
                    required property int index
                    objectName: "vinylLyricLine_"+index
                    width: lines.width
                    height: Math.max(48,lineText.implicitHeight+14)
                    readonly property bool hasAnnotation: !!(root.pageData.notes[index] && root.pageData.notes[index].length)
                    readonly property bool isPlayingCue: root.pageData.timedLyrics && root.pageData.currentLineIndex===line.index
                    opacity: {
                        if(!root.following)return 1;
                        var local=lines.y+y-scroll.contentY;
                        return Math.max(0,Math.min(1,local/Math.max(1,scroll.height*0.12),
                            (scroll.height-local)/Math.max(1,scroll.height*0.15)));
                    }
                    Text {
                        id: lineText
                        objectName: "vinylLyricText_"+line.index
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width-18
                        text: line.modelData.text
                        textFormat: Text.PlainText
                        color: line.isPlayingCue?theme.ink:theme.inkDimmer
                        font.family: root.lyricFontFamily
                        font.pixelSize: root.width<300?16:18
                        font.weight: Font.Medium
                        font.underline: line.hasAnnotation
                        lineHeight: 1.25
                        Behavior on color { ColorAnimation { duration: 280; easing.type: Easing.InOutQuad } }
                        wrapMode: Text.WordWrap
                    }
                    KeyboardAction {
                        id: lyricAction
                        objectName: "vinylLyricAction_"+line.index
                        showFocusFrame: false
                        anchors.fill: parent
                        pointerEnabled: false
                        enabled: root.pageData.timedLyrics || line.hasAnnotation
                        contextEnabled: line.hasAnnotation
                        accessibleName: line.modelData.text
                        accessibleDescription: root.pageData.timedLyrics
                            ? "Enter to seek and update annotations; Menu to pin this note" : "Enter to read annotations; Menu to pin this note"
                        onTriggered: root.activateLine(line.index)
                        onContextRequested: root.pageData.showNotes(line.index)
                        onActiveFocusChanged: if(activeFocus) {
                            root.pauseFollowing();
                            var top=line.mapToItem(scroll,0,0).y;
                            if(top<0 || line.height>=scroll.height)root.scrollTo(scroll.contentY+top);
                            else if(top+line.height>scroll.height)root.scrollTo(scroll.contentY+top+line.height-scroll.height);
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: root.pageData.timedLyrics || line.hasAnnotation?Qt.PointingHandCursor:Qt.ArrowCursor
                        onClicked: mouse => {
                            if(mouse.button===Qt.RightButton) {
                                root.pageData.showNotes(line.index);
                            }else root.activateLine(line.index);
                        }
                        onDoubleClicked: mouse => root.pageData.showNotes(line.index)
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2;height: 18;radius: 1
                        color: theme.inkDimmer
                        visible: lyricAction.activeFocus
                    }
                }
            }
        }
    }
    KeyboardAction {
        id: resume
        objectName: "vinylLyricsResume"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 34;height: 34
        visible: root.pageData.timedLyrics && !root.following
        accessibleName: "Return to current line and resume following"
        focusRadius: 17
        onTriggered: {
            scroll.cancelFlick();glide.cancelGlide("resume-following");
            if(root.pageData.resumeLyrics)root.pageData.resumeLyrics();root.followCurrentLine();
        }
        Rectangle {anchors.fill: parent;radius: 17;color: theme.glassTint}
        VinylControlIcon {anchors.centerIn: parent;symbol: "nowPlaying";color: theme.ink}
    }
    KeyboardAction {
        id: sourceCredit
        objectName: "vinylLyricsSourceCredit"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: creditText.implicitWidth;height: 28
        visible: root.showSourceCredit && root.pageData.lyricsSource === "lrclib"
        accessibleName: "Lyrics from LRCLIB"
        onTriggered: Qt.openUrlExternally("https://lrclib.net")
        Text {id: creditText;anchors.verticalCenter: parent.verticalCenter;text: "Source: LRCLIB";font.family: root.lyricFontFamily;font.pixelSize: 11;color: theme.inkDimmer}
    }
}
