import QtQuick
import QtTest

// Exercises the actual PlayerPage seek MouseArea and cancellation functions,
// extracted unchanged because the full page requires native-registered player
// and activity types. The backend seek seam is a recording signal. This is not
// full-page, Media3 or device qualification. Run with QML_XHR_ALLOW_FILE_READ=1.
TestCase {
    id: testCase
    name: "PlayerSeekTouch"
    when: windowShown
    visible: true
    width: 792
    height: 354
    property string playerSource: ""
    property var host: null
    property var gesture: null
    property var pendingTouch: null
    SignalSpy { id: seeks; target: host; signalName: "seekCommitted" }
    SignalSpy { id: cancellations; target: gesture; signalName: "canceled" }

    function blockAt(source,start) {
        verify(start>=0,"Required production source block exists")
        const opening=source.indexOf("{",start)
        verify(opening>=0)
        let depth=1
        for (let end=opening+1;end<source.length;++end) {
            if (source[end]==="{") ++depth
            else if (source[end]==="}" && --depth===0)
                return source.slice(start,end+1)
        }
        fail("Unterminated production source block")
        return ""
    }
    function productionFunction(name) {
        return blockAt(playerSource,playerSource.indexOf("function "+name+"("))
    }
    function initTestCase() {
        const request=new XMLHttpRequest()
        request.open("GET",Qt.resolvedUrl("../../qml/PlayerPage.qml"),false)
        request.send()
        playerSource=request.responseText
        verify(playerSource.length>0,"Read PlayerPage.qml; set QML_XHR_ALLOW_FILE_READ=1")
    }
    function init() {
        const marker=playerSource.indexOf("id: seekHover")
        verify(marker>=0)
        const mouseArea=blockAt(playerSource,playerSource.lastIndexOf("MouseArea {",marker))
        const mediaChange=blockAt(playerSource,playerSource.indexOf("onMediaIdChanged:"))
        const chromeEnabled=playerSource.match(/id: chrome\s*\n\s*(enabled:[^\n]*)/)
        verify(chromeEnabled!==null,"Extract the production chrome enabled gate")
        const source=[
            'import QtQuick',
            'Item { id: root; width: 792; height: 354',
            'property bool seeking: false; property real seekPreview: 20',
            'property bool touchLocked: false; property bool controlsShown: true',
            'property alias controlsEnabled: chrome.enabled',
            'property string mediaId: ""; property bool traktPlaying: false; property string traktVideoId: ""',
            'signal seekCommitted(real seconds)',
            'QtObject { id: mpv; property real duration: 100; property real position: 20 }',
            'QtObject { id: playerTouchSurface; function cancelGesture() {} }',
            'Timer { id: hideTimer; interval: 1000 }',
            'function requestSeekThumb() {}',
            'function sendTraktPlayback(playing) {}',
            'function requestUserSeekTo(seconds) { root.seekCommitted(seconds) }',
            productionFunction("clamp"),
            productionFunction("previewAt"),
            productionFunction("cancelSeekGesture"),
            productionFunction("cancelTouchInteraction"),
            productionFunction("wakeChrome"),
            mediaChange,
            'Item { id: chrome; anchors.fill: parent',
            chromeEnabled[1],
            'Item { id: seekBar; width: 600; height: 32; anchors.centerIn: parent; property bool hovered: false',
            mouseArea,
            '}',
            '}',
            '}'
        ].join("\n")
        host=Qt.createQmlObject(source,testCase,"PlayerSeekProductionExtract")
        verify(host!==null)
        gesture=findChild(host,"playerSeekGestureArea")
        verify(gesture!==null)
        seeks.clear()
        cancellations.clear()
        waitForRendering(host)
    }
    function cleanup() {
        releaseTouch(-50,-50)
        gesture=null
        if (host) host.destroy()
        host=null
    }
    function releaseTouch(x,y) {
        if (!pendingTouch) return
        pendingTouch.release(0,gesture,x,y).commit()
        pendingTouch=null
    }
    function beginDrag() {
        pendingTouch=touchEvent(gesture)
        pendingTouch.press(0,gesture,80,16).commit()
        wait(20)
        compare(host.seeking,true)
        pendingTouch.move(0,gesture,300,16).commit()
        wait(20)
        verify(host.seekPreview>45 && host.seekPreview<55)
    }
    function test_touch_release_commits_preview_once() {
        beginDrag()
        releaseTouch(300,16)
        tryCompare(seeks,"count",1)
        compare(host.seeking,false)
        verify(seeks.signalArguments[0][0]>45 && seeks.signalArguments[0][0]<55)
    }
    function test_disabled_area_resets_without_seek() {
        beginDrag()
        gesture.enabled=false
        // MouseArea disabling does not guarantee canceled. The production
        // enabled-change hook must revoke the seek before a late release.
        const canceledBeforeRelease=!host.seeking
        releaseTouch(300,16)
        verify(canceledBeforeRelease)
        compare(host.seeking,false)
        compare(host.seekPreview,20)
        wait(20)
        compare(seeks.count,0)
    }
    function test_pointer_ungrab_resets_without_seek() {
        beginDrag()
        gesture.visible=false
        const canceledBeforeRelease=!host.seeking
        releaseTouch(300,16)
        tryCompare(cancellations,"count",1)
        verify(canceledBeforeRelease)
        compare(host.seeking,false)
        compare(host.seekPreview,20)
        compare(seeks.count,0)
    }
    function test_lock_disables_controls_before_release_and_unlock_reenables() {
        beginDrag()
        host.touchLocked=true
        const disabledBeforeRelease=!host.controlsEnabled
        releaseTouch(300,16)
        verify(disabledBeforeRelease,"The production chrome gate takes effect before its visual fade")
        compare(host.seeking,false)
        compare(seeks.count,0)
        host.touchLocked=false
        compare(host.controlsEnabled,true)
        beginDrag()
        releaseTouch(300,16)
        tryCompare(seeks,"count",1)
    }
    function test_media_change_cancels_held_seek() {
        beginDrag()
        host.mediaId="replacement-item"
        const canceledBeforeRelease=!host.seeking
        releaseTouch(300,16)
        verify(canceledBeforeRelease)
        compare(host.seeking,false)
        compare(host.seekPreview,20)
        wait(20)
        compare(seeks.count,0)
    }
    function test_lifecycle_cancel_does_not_commit_on_late_move_or_release() {
        beginDrag()
        host.cancelTouchInteraction()
        pendingTouch.move(0,gesture,500,16).commit()
        wait(20)
        const canceledBeforeRelease=!host.seeking
        releaseTouch(500,16)
        verify(canceledBeforeRelease)
        compare(host.seeking,false)
        compare(host.seekPreview,20)
        wait(20)
        compare(seeks.count,0)
    }
    function test_desktop_mouse_seek_preserved() {
        mouseDrag(gesture,80,16,220,0,Qt.LeftButton)
        compare(seeks.count,1)
        compare(host.seeking,false)
        verify(seeks.signalArguments[0][0]>45 && seeks.signalArguments[0][0]<55)
    }
}
