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

    // ---- Audio pane data (Task 13), bound through from ReaderShell ----
    property bool audioAttached: false
    property string audioTitle: ""
    property url audioCover: ""
    property string audioMetaLine: ""
    property bool followOn: false
    property bool audioPlaying: false
    property string audioTimeLine: ""
    property real audioProgress: 0
    property string audioSpeedLabel: "1.0×"

    // ---- left-panel state (owned here; the panel is a pure view over these) ----
    property bool panelOpen: false
    property string activeTab: "contents"

    // ---- appearance panel (Task 10) state + current settings (bound from ReaderShell) ----
    property bool appearanceOpen: false
    property var appearance: ({})

    // ---- search sheet (Task 11) state + data (bound from ReaderShell) ----
    property bool searchOpen: false
    property var searchResults: []
    property int searchCount: 0
    property bool searchCapped: false
    property string searchLastQuery: ""

    // Any of the three overlays open pins the chrome shown; Esc closes whichever is open
    // (ReaderShell's cascade). Search counts too, so Esc dismisses it via closeAnyPanel.
    readonly property bool anyPanelOpen: panelOpen || appearanceOpen || searchOpen

    // ---- signals up ----
    signal backRequested()
    signal searchRequested()
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
    // Audio pane actions (Task 13) forwarded to ReaderShell (which owns the AudiobookSession)
    signal followToggled(bool on)
    signal audioPlayToggled()
    signal audioSpeedCycled()
    signal audioSeekRequested(real fraction)
    signal audioSkipRequested(real seconds)   // HUD transport pill: relative ±seek
    // appearance edits forwarded to ReaderShell (which merges + persists + live-applies)
    signal appearanceEdited(string key, var value)
    // search actions forwarded to ReaderShell (which owns paper.search / goTo / clearSearch)
    signal searchSubmitted(string query)
    signal searchResultActivated(string cfi)

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
    // The left (Contents), right (Appearance), and search overlays are MUTUALLY EXCLUSIVE —
    // opening one closes the others (keep it simple, per the task).
    function openPanelTo(tab) { appearanceOpen = false; searchOpen = false; activeTab = tab; panelOpen = true }
    function closePanel() { panelOpen = false }
    // Contents icon: open to Contents; if already open ON Contents, close; if open on
    // another tab, switch to Contents (don't close).
    function handleContents() {
        if (panelOpen && activeTab === "contents") closePanel()
        else openPanelTo("contents")
    }

    // ---- appearance panel (Task 10) open/close, and the Appearance-icon toggle ----
    function closeAppearance() { appearanceOpen = false }
    // Appearance icon: toggle the right panel; opening it closes the left panel + search.
    function handleAppearance() {
        if (appearanceOpen) appearanceOpen = false
        else { panelOpen = false; searchOpen = false; appearanceOpen = true }
    }

    // ---- search sheet (Task 11) open/close, and the search-icon toggle ----
    function openSearch() { panelOpen = false; appearanceOpen = false; searchOpen = true }
    function closeSearch() { searchOpen = false }
    // Search icon: toggle the sheet; opening it closes both panels. searchRequested() lets
    // ReaderShell reset the model (and clear any prior search) as the sheet opens.
    function handleSearch() {
        searchRequested()
        if (searchOpen) closeSearch()
        else openSearch()
    }

    // Esc / a shared close: drop whichever overlay is open.
    function closeAnyPanel() { panelOpen = false; appearanceOpen = false; searchOpen = false }

    // While EITHER panel is open the chrome is PINNED shown (can't idle-hide); closing the
    // last one unpins and restarts the idle countdown. Both open-state changes route here.
    // Read the RAW open flags (not the derived anyPanelOpen binding, which can lag one beat
    // behind a change signal) so the pin reflects the just-applied state.
    function updatePin() { setPanelOpen(chrome.panelOpen || chrome.appearanceOpen || chrome.searchOpen) }
    onPanelOpenChanged: updatePin()
    onAppearanceOpenChanged: updatePin()
    onSearchOpenChanged: updatePin()

    Timer { interval: 300; running: true; repeat: true; onTriggered: chrome.tick() }

    // ---------- 1. center body: NO QML overlay — the paper owns pointer input ----------
    // THE POINTER REWORK (Task 9): there is deliberately NO full-fill MouseArea over the
    // center anymore. The old center-tap `onDoubleClicked: chrome.toggle()` sat OVER the
    // WebEngineView and ate every press/drag, so in-page text selection could never fire.
    // Removing it lets drags reach the paper (the glue's `selection` event flows). The
    // double-click-to-toggle affordance now lives in the glue (paper_glue.js `dblclick`):
    // a double-click on EMPTY space emits `toggleChrome` → ReaderShell → chrome.toggle();
    // a double-click on a word selects it (and opens the menu). The edge page-turn zones
    // (§2, outer ~11%) and the reveal bands (§5, hover-only, no MouseArea) stay — neither
    // covers the central text column where selection happens.

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
    // The near-black RGB comes from Theme.scrim (the shared token); each gradient stop just
    // varies its alpha. (Qt.rgba wants 0..1 channels — Theme.scrim.r/g/b already are.)
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 130
        opacity: chrome.awake ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.72) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.0) }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 130
        opacity: chrome.awake ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.0) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.scrim.r, Theme.scrim.g, Theme.scrim.b, 0.78) }
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
        onSearchRequested: chrome.handleSearch()          // toggles the search sheet (Task 11)
        onContentsRequested: chrome.handleContents()      // toggles the left panel (Task 8)
        onAppearanceRequested: chrome.handleAppearance()  // toggles the right panel (Task 10)
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

    // ---------- audiobook HUD transport (Hemanth 2026-07-18) — rides the chrome reveal ----------
    // A centered pill above the bottom rail: skip back · play/pause · skip forward · time.
    // Appears ONLY when this book has an attached audiobook, and fades on the same `awake`
    // beat as the bars ("a HUD controller that lives on top of the reader and disappears
    // along with the HUD"). Full controls — scrub rail, speed, Follow — stay in the left
    // panel's Audio tab; this pill is the quick transport. Its backing MouseArea is the
    // house click-swallower, so taps here never fall through to the page-turn zones.
    Rectangle {
        id: audioHud
        visible: opacity > 0.01 && chrome.audioAttached
        opacity: (chrome.awake && chrome.audioAttached) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bottomRail.top
        anchors.bottomMargin: 10
        width: hudRow.implicitWidth + 40
        height: 46
        radius: 23
        color: Theme.bar
        border.color: Theme.barBorder
        border.width: 1
        MouseArea { anchors.fill: parent }        // swallow — nothing falls through to the page

        Row {
            id: hudRow
            anchors.centerIn: parent
            spacing: 18

            Image {                               // skip back 15s (arc-arrow SVG, house stroke)
                anchors.verticalCenter: parent.verticalCenter
                width: 19; height: 19
                source: Qt.resolvedUrl("../../assets/icons/reader2/skip-back-15.svg")
                sourceSize: Qt.size(38, 38)
                opacity: skipBackMa.containsMouse ? 1.0 : 0.55
                MouseArea { id: skipBackMa; anchors.fill: parent; anchors.margins: -8
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: chrome.audioSkipRequested(-15) }
            }

            Rectangle {                           // play / pause — white circle, dark glyph
                anchors.verticalCenter: parent.verticalCenter
                width: 32; height: 32; radius: 16
                color: Theme.ink
                Image {
                    anchors.centerIn: parent
                    // nudge the play triangle right for optical centering (panel transport parity)
                    anchors.horizontalCenterOffset: chrome.audioPlaying ? 0 : 1
                    width: 13; height: 13
                    source: chrome.audioPlaying
                        ? Qt.resolvedUrl("../../assets/icons/reader2/pause-dark.svg")
                        : Qt.resolvedUrl("../../assets/icons/reader2/play-dark.svg")
                    sourceSize: Qt.size(26, 26)
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: chrome.audioPlayToggled() }
            }

            Image {                               // skip forward 15s (arc-arrow SVG, house stroke)
                anchors.verticalCenter: parent.verticalCenter
                width: 19; height: 19
                source: Qt.resolvedUrl("../../assets/icons/reader2/skip-forward-15.svg")
                sourceSize: Qt.size(38, 38)
                opacity: skipFwdMa.containsMouse ? 1.0 : 0.55
                MouseArea { id: skipFwdMa; anchors.fill: parent; anchors.margins: -8
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: chrome.audioSkipRequested(15) }
            }

            Rectangle {                           // speed chip — cycles the ladder (panel parity)
                anchors.verticalCenter: parent.verticalCenter
                width: speedText.implicitWidth + 16
                height: 24; radius: 12
                color: "transparent"
                border.width: 1
                border.color: speedMa.containsMouse ? Theme.ink : Theme.barBorder
                Text {
                    id: speedText
                    anchors.centerIn: parent
                    text: chrome.audioSpeedLabel
                    color: speedMa.containsMouse ? Theme.ink : Theme.inkDim
                    font.family: Theme.ui
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                MouseArea { id: speedMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chrome.audioSpeedCycled() }
            }

            Text {                                // "12:34 / 1:02:03" once live; quiet before play
                anchors.verticalCenter: parent.verticalCenter
                visible: chrome.audioTimeLine !== ""
                text: chrome.audioTimeLine
                color: Theme.inkDim
                font.family: Theme.ui
                font.pixelSize: 12
            }
        }
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

        // Audio pane (Task 13) — data down, intent up.
        audioAttached: chrome.audioAttached
        audioTitle: chrome.audioTitle
        audioCover: chrome.audioCover
        audioMetaLine: chrome.audioMetaLine
        followOn: chrome.followOn
        audioPlaying: chrome.audioPlaying
        audioTimeLine: chrome.audioTimeLine
        audioProgress: chrome.audioProgress
        audioSpeedLabel: chrome.audioSpeedLabel

        onCloseRequested: chrome.closePanel()
        onTabSelected: (tab) => { chrome.activeTab = tab; chrome.tabSelected(tab) }
        onTocActivated: (href) => chrome.tocActivated(href)
        onBookmarkActivated: (cfi) => chrome.bookmarkActivated(cfi)
        onBookmarkDeleted: (id) => chrome.bookmarkDeleted(id)
        onHighlightActivated: (cfi) => chrome.highlightActivated(cfi)
        onFollowToggled: (on) => chrome.followToggled(on)
        onAudioPlayToggled: chrome.audioPlayToggled()
        onAudioSpeedCycled: chrome.audioSpeedCycled()
        onAudioSeekRequested: (f) => chrome.audioSeekRequested(f)
    }

    // ---------- 7. RIGHT PANEL (Task 10) — Appearance; same z as the left panel ----------
    // Off-screen (x = +width) while closed, so it intercepts nothing; owns its column + the
    // click-outside catcher when open. Bridge-free: it emits appearanceEdited up to ReaderShell.
    AppearancePanel {
        id: appearancePanel
        anchors.fill: parent
        open: chrome.appearanceOpen
        appearance: chrome.appearance
        onCloseRequested: chrome.closeAppearance()
        onChanged: (key, value) => chrome.appearanceEdited(key, value)
    }

    // ---------- 8. SEARCH SHEET (Task 11) — top-most so it floats above the bars/panels ----------
    // A small centered card under the top bar (mock z9 > panels z8). Bridge-free: it emits
    // submit/activate/close up to ReaderShell, which owns paper.search / goTo / clearSearch.
    SearchSheet {
        id: searchSheet
        anchors.fill: parent
        open: chrome.searchOpen
        results: chrome.searchResults
        resultCount: chrome.searchCount
        capped: chrome.searchCapped
        lastQuery: chrome.searchLastQuery
        onSubmitted: (q) => chrome.searchSubmitted(q)
        onResultActivated: (cfi) => chrome.searchResultActivated(cfi)
        onCloseRequested: chrome.closeSearch()
    }
}
