// ReaderChrome.qml — the native glass chrome that floats OVER the paper (the web
// view). It owns the reveal (comic-reader doctrine, MangaReader.qml): the chrome stays
// hidden while you read and returns ONLY when you deliberately reach for it — the
// cursor enters the top/bottom edge band — or on the book-open beat / a double-click.
// A ~300ms Timer ticks it toward sleep after 3s idle; `awake` (= revealState.shown)
// fades the top scrim + TopBar and the bottom scrim + BottomRail in/out. Body movement,
// scroll, and keys NEVER wake it — there is NO "move" path into the reducer (that was
// the bug this doctrine fixes). Left/right edge zones turn pages. Keys are handled by
// ReaderShell and never routed here. Pixel contract: the chrome mock
// (.scrim/.topbar/.bottombar/.turn).
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

    // ---- left-panel data (Task 8), bound through from ReaderShell ----
    property var tocModel: []
    property int currentTocIndex: -1
    property var bookmarks: []
    property var highlights: []

    // ---- left-panel state (owned here; the panel is a pure view over these) ----
    property bool panelOpen: false
    property string activeTab: "contents"

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
    // left-panel actions forwarded to ReaderShell (which owns the paper + stores)
    signal tocActivated(string href)
    signal bookmarkActivated(string cfi)
    signal bookmarkDeleted(string id)
    signal highlightActivated(string cfi)
    signal tabSelected(string tab)

    // ---- reveal state (pure reducer in Reader2Logic; `awake` mirrors revealState.shown) ----
    property bool awake: false
    property var revealState: ({ shown: false, lastActive: 0, pinned: false, frozen: false })

    // book-open orientation beat / toggle-from-hidden — show, then let idle reclaim it.
    function wake() { revealState = L.revealReducer(revealState, "wake", Date.now()); awake = revealState.shown }
    // cursor reached the top/bottom edge band → reveal AND freeze while it stays inside.
    function enterBar() { revealState = L.revealReducer(revealState, "enterBar", Date.now()); awake = revealState.shown }
    // cursor left the band → drop the freeze and restart the 3s idle countdown.
    function exitBar() { revealState = L.revealReducer(revealState, "exitBar", Date.now()); awake = revealState.shown }
    function tick() { revealState = L.revealReducer(revealState, "tick", Date.now()); awake = revealState.shown }
    // future panels (Task 8+) pin the chrome shown while open.
    function setPanelOpen(open) {
        revealState = L.revealReducer(revealState, open ? "panelOpen" : "panelClose", Date.now())
        awake = revealState.shown
    }
    // double-click in the reading body toggles the chrome (hide if shown, else show).
    function toggle() { revealState = L.revealReducer(revealState, "toggle", Date.now()); awake = revealState.shown }

    // ---- left panel (Task 8) open/close, and the Contents-icon toggle ----
    function openPanelTo(tab) { activeTab = tab; panelOpen = true }
    function closePanel() { panelOpen = false }
    // Contents icon: open to Contents; if already open ON Contents, close; if open on
    // another tab, switch to Contents (don't close).
    function handleContents() {
        contentsRequested()
        if (panelOpen && activeTab === "contents") closePanel()
        else openPanelTo("contents")
    }
    // While the panel is open the chrome is PINNED shown (can't idle-hide); closing
    // unpins and restarts the idle countdown.
    onPanelOpenChanged: setPanelOpen(panelOpen)

    Timer { interval: 300; running: true; repeat: true; onTriggered: chrome.tick() }

    // ---------- 1. center body = double-click toggles chrome (edges override at sides) ----------
    // Body MOVEMENT feeds nothing — there is deliberately NO hover tracker here. A single
    // click is harmlessly swallowed for now (Task 9 reworks this area for text selection);
    // a double-click toggles the chrome. The top/bottom reveal bands (§5, top-most,
    // hover-only) never consume this click.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: chrome.toggle()
    }

    // ---------- 2. edge page-turn zones (~11% each side) ----------
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

    // ---------- 3. reveal scrims (paint-only, never block input) ----------
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

    // ---------- 4. the bars ----------
    TopBar {
        id: topBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        shown: chrome.awake
        title: chrome.title
        author: chrome.author
        chapterLabel: chrome.chapterLabel
        onBackRequested: chrome.backRequested()
        onSearchRequested: chrome.searchRequested()
        onContentsRequested: chrome.handleContents()      // toggles the left panel (Task 8)
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

    // ---------- 5. reveal bands (top + bottom) — the ONLY hover waker ----------
    // Reaching the top or bottom edge reveals the chrome AND freezes it shown while the
    // cursor stays in the band. The band is tall enough to contain the whole shown bar,
    // so "hovering the bar" == "inside the band" == frozen; moving back into the reading
    // body leaves the band → the 3s idle countdown resumes. HoverHandler (NOT a
    // hoverEnabled MouseArea) is deliberate and mirrors MangaReader.qml's HUD hover-
    // freeze: it is geometry-based and passive, so its hover can't be stolen by the bars'
    // child icon-button MouseAreas (which would drop the freeze), it never blocks their
    // clicks or hover-brighten, and it never shadows a lower cursorShape. Declared last
    // (top-most) so hover is delivered reliably; carries no MouseArea, so every click
    // falls straight through to the bars / page-turn zones beneath.
    readonly property int revealBandPx: 96
    Item {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: chrome.revealBandPx
        HoverHandler { onHoveredChanged: hovered ? chrome.enterBar() : chrome.exitBar() }
    }
    Item {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: chrome.revealBandPx
        HoverHandler { onHoveredChanged: hovered ? chrome.enterBar() : chrome.exitBar() }
    }

    // ---------- 6. LEFT PANEL (Task 8) — top-most so it overlays the bars (mock z8 > z5) ----------
    // Off-screen (x = -width) while closed, so it intercepts nothing and the reveal bands
    // above keep working; it fully owns its column + the click-outside catcher when open.
    LeftPanel {
        id: leftPanel
        anchors.fill: parent
        open: chrome.panelOpen
        activeTab: chrome.activeTab
        tocModel: chrome.tocModel
        currentTocIndex: chrome.currentTocIndex
        bookmarks: chrome.bookmarks
        highlights: chrome.highlights

        onCloseRequested: chrome.closePanel()
        onTabSelected: (tab) => { chrome.activeTab = tab; chrome.tabSelected(tab) }
        onTocActivated: (href) => chrome.tocActivated(href)
        onBookmarkActivated: (cfi) => chrome.bookmarkActivated(cfi)
        onBookmarkDeleted: (id) => chrome.bookmarkDeleted(id)
        onHighlightActivated: (cfi) => chrome.highlightActivated(cfi)
    }
}
