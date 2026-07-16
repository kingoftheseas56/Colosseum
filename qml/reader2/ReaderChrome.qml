// ReaderChrome.qml — the native glass chrome that floats OVER the paper (the web
// view). It owns the reveal: a hover tracker feeds Reader2Logic.revealReducer, a
// ~300ms Timer ticks it toward sleep, and `awake` fades the top scrim + TopBar and
// the bottom scrim + BottomRail in/out. Edge zones turn pages; a center tap toggles
// the chrome. Keys are handled by ReaderShell and NEVER routed here, so they can't
// wake the chrome. Pixel contract: the chrome mock (.scrim/.topbar/.bottombar/.turn).
//
// This overlay is BRIDGE-FREE: it only emits signals up to ReaderShell, which owns
// the paper + the native stores. That keeps it instantiable headless (chrome smoke).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "Reader2Logic.js" as L

Item {
    id: chrome

    // ---- inputs from the shell (book metadata + the paper's relocated position) ----
    property string title: ""
    property string author: ""
    property string chapterLabel: ""
    property int percent: 0
    property int pageInChapter: 0
    property int pagesInChapter: 0
    property var ticks: []
    property bool returnVisible: false
    property string returnPageLabel: ""

    // ---- signals up ----
    signal backRequested()
    signal searchRequested()
    signal contentsRequested()
    signal appearanceRequested()
    signal bookmarkRequested()
    signal prevRequested()
    signal nextRequested()
    signal scrubbed(real fraction)
    signal returnRequested()

    // ---- reveal state (pure reducer in Reader2Logic) ----
    property bool awake: false
    property var revealState: ({ awake: false, lastMove: 0, pinned: false })

    function bump() { revealState = L.revealReducer(revealState, "move", Date.now()); awake = revealState.awake }
    function tick() { revealState = L.revealReducer(revealState, "tick", Date.now()); awake = revealState.awake }
    // future panels (Task 8+) pin the chrome awake while open.
    function setPanelOpen(open) {
        revealState = L.revealReducer(revealState, open ? "panelOpen" : "panelClose", Date.now())
        awake = revealState.awake
    }
    function toggle() {
        if (awake && !revealState.pinned) { revealState = { awake: false, lastMove: 0, pinned: false }; awake = false }
        else bump()
    }

    Timer { interval: 300; running: true; repeat: true; onTriggered: chrome.tick() }

    // ---------- 1. reveal hover tracker (never consumes clicks) ----------
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: chrome.bump()
        onEntered: chrome.bump()
    }

    // ---------- 2. center tap = toggle chrome (full width; edges override at sides) ----------
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: chrome.toggle()
    }

    // ---------- 3. edge page-turn zones (~11% each side) ----------
    MouseArea {
        id: leftEdge
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width * 0.11
        cursorShape: Qt.PointingHandCursor
        onClicked: chrome.prevRequested()
        Image {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 22
            source: Qt.resolvedUrl("../../assets/icons/reader2/chevron-left.svg")
            width: 26; height: 26; sourceSize.width: 52; sourceSize.height: 52
            fillMode: Image.PreserveAspectFit; smooth: true
            opacity: chrome.awake ? 0.26 : 0
            Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        }
    }
    MouseArea {
        id: rightEdge
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width * 0.11
        cursorShape: Qt.PointingHandCursor
        onClicked: chrome.nextRequested()
        Image {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 22
            source: Qt.resolvedUrl("../../assets/icons/reader2/chevron-right.svg")
            width: 26; height: 26; sourceSize.width: 52; sourceSize.height: 52
            fillMode: Image.PreserveAspectFit; smooth: true
            opacity: chrome.awake ? 0.26 : 0
            Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        }
    }

    // ---------- 4. reveal scrims (paint-only, never block input) ----------
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 130
        opacity: chrome.awake ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 0.72) }
            GradientStop { position: 1.0; color: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 0.0) }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 130
        opacity: chrome.awake ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 0.0) }
            GradientStop { position: 1.0; color: Qt.rgba(8 / 255, 8 / 255, 10 / 255, 0.78) }
        }
    }

    // ---------- 5. the bars ----------
    TopBar {
        id: topBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        shown: chrome.awake
        title: chrome.title
        author: chrome.author
        chapterLabel: chrome.chapterLabel
        onBackRequested: chrome.backRequested()
        onSearchRequested: chrome.searchRequested()
        onContentsRequested: chrome.contentsRequested()
        onAppearanceRequested: chrome.appearanceRequested()
        onBookmarkRequested: chrome.bookmarkRequested()
    }

    BottomRail {
        id: bottomRail
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        shown: chrome.awake
        fraction: chrome.percent / 100
        pageInChapter: chrome.pageInChapter
        pagesInChapter: chrome.pagesInChapter
        percentOfBook: chrome.percent
        ticks: chrome.ticks
        returnVisible: chrome.returnVisible
        returnPageLabel: chrome.returnPageLabel
        onScrubbed: (f) => chrome.scrubbed(f)
        onReturnRequested: chrome.returnRequested()
    }
}
