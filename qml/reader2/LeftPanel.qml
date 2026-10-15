// LeftPanel.qml — the reader's LEFT GLASS PANEL (TASK 8): a slide-in tabbed column
// with Contents / Bookmarks / Highlights (+ a DISABLED Audio tab whose real content is
// Task 13). 348px glass over the paper, sliding in from the left edge with the mock's
// ~.32s cubic. Pixel contract: the chrome mock's `.panel.left`, `.tabs`, `.pane`,
// `.chrow`, `.mark`, `.hl` (agents/colosseum-book-reader-chrome-mock.html).
//
// Like TopBar/BottomRail this overlay is BRIDGE-FREE: it takes its data via properties
// and reports back via signals only, so ReaderShell keeps sole ownership of the paper +
// the native stores (bookmarks.json / annotations.json through Reader2Bridge). Row
// SHAPING is pure (Reader2Logic.tocRowState/bookmarkRow/highlightRow) so it renders BOTH
// reader2's write shape AND the old reader's records with zero migration.
//
// Dismissal: a transparent click-catcher over the paper to the RIGHT of the column
// (below the top bar, so the TopBar's right icons stay live) emits closeRequested — the
// familiar "tap the page to close the drawer". The Contents icon toggles it too, and Esc
// closes it (both wired in ReaderChrome / ReaderShell).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import QtQuick.Window
import "Reader2Logic.js" as L

Item {
    id: panel

    // ---- inputs (bound by ReaderChrome from ReaderShell's view-model) ----
    property bool open: false
    property string activeTab: "contents"       // contents | bookmarks | highlights | audio
    property var tocModel: []                    // [{ index, label, href, fraction? }]
    property int currentTocIndex: -1             // from relocated.tocIndex
    property var bookmarks: []                   // raw records from bookmarksGet
    property var highlights: []                  // raw records from annotationsGet

    // ---- Audio pane (Task 13 — read-along), all bound from ReaderShell ----
    // Bridge-free like the rest of the panel: it renders these values and reports the
    // user's intent via signals; ReaderShell owns the pairing lookup + the AudiobookSession.
    property bool audioAttached: false           // a pairing exists for shell.bookId
    property string audioTitle: ""               // the audiobook's title
    property url audioCover: ""                   // cover art if any (else a placeholder glyph)
    property string audioMetaLine: ""            // "21 h 04 m · 135 chapters" (or "135 chapters")
    property bool followOn: false                // "Follow my reading" switch state
    property bool audioPlaying: false            // session playing (drives the playing marker)
    property string audioTimeLine: ""            // (transport moved to the HUD pill; kept for compat)
    property real audioProgress: 0               // (kept for compat)
    property string audioSpeedLabel: "1.0×"       // (kept for compat)
    property var audioPlaylist: []               // chapter/file labels — the PLAYLIST rows
    property int audioCurrentIndex: -1           // playing row (-1 = stream not live yet)

    // ---- read-along (Task 6) — the Text Sync mode controls, all bound from ReaderShell ----
    // DORMANT by default: when the native ReadAlong/AudioTextAlignment context props are
    // absent, ReaderShell leaves readAlongAvailable false and this whole block stays hidden,
    // so the Audio pane reads byte-for-byte as it does today.
    property bool readAlongAvailable: false      // native read-along engine present for this book
    property string readAlongMode: "sentenceWord" // "sentence" | "word" | "sentenceWord"
    property real readAlongWordScale: 1.0        // word enlargement (1.0..2.0)

    // ---- Text Sync status (Task 7) — the honest per-chapter alignment status + controls ----
    // Bridge-free in the SAME sense as the rest of the panel: the native
    // AudioTextAlignmentService arrives as an INJECTED object (`textSync`), not reached as a
    // context property — so a harness feeds a fake and ReaderShell binds the real one. The
    // panel reads status/chapters STRAIGHT off the service on each refresh (never a second
    // copy of truth, never re-derives stage/progress in QML) and re-fetches on `jobChanged`.
    // DORMANT until the service is registered (Task 12): textSync stays null, textSyncOn is
    // false, and the whole block is absent — the Audio pane reads byte-for-byte as today.
    property var textSync: null                  // AudioTextAlignmentService (or a fake); null = dormant
    property string bookId: ""                   // the book whose alignment job we show
    property var textSyncStatus: ({})            // last statusFor(bookId): {stage, ready, total, paused}
    property var textSyncChapters: []            // last chaptersFor(bookId): [{index, stage, failureCode, ...}]
    property bool restartArmed: false            // protected Restart: true = the confirm step is showing

    // Shown only when the native service AND a book id are present (and read-along is on).
    readonly property bool textSyncOn: panel.readAlongAvailable && !!panel.textSync && panel.bookId !== ""
    // Thin renders of the service's own status — never a re-derivation.
    readonly property string textSyncSummaryText: L.textSyncSummary(panel.textSyncStatus)
    readonly property string textSyncReadyText: L.readyCountText(panel.textSyncStatus)
    readonly property bool textSyncAllReady: L.textSyncAllReady(panel.textSyncStatus)
    readonly property var textSyncChapterLabels: {
        var out = []
        var cs = panel.textSyncChapters || []
        for (var i = 0; i < cs.length; i++) out.push(L.chapterStateText(cs[i]))
        return out
    }

    // Re-fetch the service's own status/chapters. Called at init, when the service or book
    // changes, and on every jobChanged — the ONLY way the panel's view of the job updates.
    function refreshTextSync() {
        if (panel.textSync && panel.bookId !== "") {
            panel.textSyncStatus = panel.textSync.statusFor(panel.bookId) || ({})
            panel.textSyncChapters = panel.textSync.chaptersFor(panel.bookId) || []
        } else {
            panel.textSyncStatus = ({})
            panel.textSyncChapters = []
        }
    }
    // Control intents call the service DIRECTLY (it owns the job); the refresh rides jobChanged.
    function pauseTextSync() { if (panel.textSync) panel.textSync.pause(panel.bookId) }
    function resumeTextSync() { if (panel.textSync) panel.textSync.resume(panel.bookId) }
    function retryChapter(index) { if (panel.textSync) panel.textSync.retry(panel.bookId, index) }
    function requestRestart() { panel.restartArmed = true }           // arm the confirm step
    function cancelRestart() { panel.restartArmed = false }
    function confirmRestart() { panel.restartArmed = false; if (panel.textSync) panel.textSync.restart(panel.bookId) }

    onTextSyncChanged: panel.refreshTextSync()
    onBookIdChanged: panel.refreshTextSync()
    Component.onCompleted: panel.refreshTextSync()
    Connections {
        target: panel.textSync
        ignoreUnknownSignals: true
        function onJobChanged(changedBookId) { if (changedBookId === panel.bookId) panel.refreshTextSync() }
    }

    // ---- signals up ----
    signal closeRequested()
    signal tabSelected(string tab)
    signal tocActivated(string href)
    signal bookmarkActivated(string cfi)
    signal bookmarkDeleted(string id)
    signal highlightActivated(string cfi)
    // Audio pane actions (Task 13) — ReaderShell drives the AudiobookSession on these.
    signal followToggled(bool on)
    signal audioPlayToggled()
    signal audioSpeedCycled()
    signal audioSeekRequested(real fraction)     // (compat; the pill owns transport now)
    signal audioChapterPicked(int index)         // playlist row tap → play that chapter/file
    // read-along (Task 6): ReaderShell persists the mode/enlargement + pushes them to the paper.
    signal readAlongModePicked(string mode)      // "sentence" | "word" | "sentenceWord"
    signal readAlongScaleChanged(real scale)     // word enlargement 1.0..2.0

    readonly property int colWidth: 348
    readonly property int topBarPx: 64

    function focusActiveTab() {
        if (activeTab === "bookmarks") bookmarksTab.focusKeyboard()
        else if (activeTab === "highlights") highlightsTab.focusKeyboard()
        else if (activeTab === "audio") audioTab.focusKeyboard()
        else contentsTab.focusKeyboard()
    }
    function focusBelongsHere(item) {
        var p = item
        while (p) { if (p === panel) return true; p = p.parent }
        return false
    }
    function trapTab(event) {
        var tab = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
        if (!open || !tab) return false
        var backwards = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier)
        var w = panel.Window.window
        var current = w ? w.activeFocusItem : null
        if (!current || !focusBelongsHere(current)) { focusActiveTab(); event.accepted = true; return true }
        var next = current
        for (var i = 0; i < 512; ++i) {
            next = next.nextItemInFocusChain(!backwards)
            if (!next || next === current) break
            if (focusBelongsHere(next) && next.activeFocusOnTab && next.visible && next.enabled) {
                next.forceActiveFocus(Qt.OtherFocusReason); event.accepted = true; return true
            }
        }
        event.accepted = true; return true
    }
    onOpenChanged: if (open) Qt.callLater(panel.focusActiveTab)
    onActiveTabChanged: if (open) Qt.callLater(panel.focusActiveTab)
    Keys.onPressed: function(event) {
        if (panel.trapTab(event)) return
        if (open && event.key === Qt.Key_Escape) { panel.closeRequested(); event.accepted = true }
    }
           // keep the top bar's right icons clickable

    // ---------- click-outside-to-dismiss (transparent; paper area right of the column) ----------
    // Only armed while open, and inset below the top bar so the TopBar icons stay live.
    ReaderKeyboardArea {
        anchors.left: column.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: panel.topBarPx
        anchors.bottom: parent.bottom
        enabled: panel.open
        onClicked: panel.closeRequested()
    }

    // ---------- the glass column ----------
    Rectangle {
        id: column
        width: panel.colWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        color: Theme.panelBg

        // slide in/out from the left edge (mock: transform .32s cubic-bezier(.2,.8,.2,1)).
        transform: Translate {
            x: panel.open ? 0 : -column.width
            Behavior on x {
                NumberAnimation {
                    duration: 320
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1]
                }
            }
        }

        // right hairline border (mock border-right: 1px var(--bar-border)).
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.barBorder
        }

        // OWN click-swallow (house doctrine): taps/scrolls inside the column never fall
        // through to the paper or the chrome's double-click toggle beneath.
        ReaderKeyboardArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // ---------- tab strip ----------
        Row {
            id: tabStrip
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 18
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 2

            readonly property real tabW: (width - spacing * 3) / 4

            // ICON tabs (Hemanth 2026-07-18: "change every name in toc to an SVG icon placed at
            // equal distance") — equal-width cells, icon centered in each; the name lives on in
            // the hover tooltip. Same SVG set as the rest of the reader chrome.
            Tab { id: contentsTab; width: tabStrip.tabW; icon: "contents.svg";  tip: "Contents";   active: panel.activeTab === "contents";   onPicked: panel.tabSelected("contents") }
            Tab { id: bookmarksTab; width: tabStrip.tabW; icon: "bookmark.svg";  tip: "Bookmarks";  active: panel.activeTab === "bookmarks";  onPicked: panel.tabSelected("bookmarks") }
            Tab { id: highlightsTab; width: tabStrip.tabW; icon: "highlight.svg"; tip: "Highlights"; active: panel.activeTab === "highlights"; onPicked: panel.tabSelected("highlights") }
            // Audio (Task 13) — the read-along pane: attached audiobook + Follow switch +
            // mini transport. Live now (the placeholder/disabled state is gone).
            Tab { id: audioTab; width: tabStrip.tabW; icon: "headphones.svg"; tip: "Audio"; active: panel.activeTab === "audio"; onPicked: panel.tabSelected("audio") }
        }

        // ---------- pane body ----------
        Item {
            id: body
            anchors.top: tabStrip.bottom
            anchors.topMargin: 9
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            // ===== Contents =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "contents"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.tocModel || panel.tocModel.length === 0
                    text: "No chapters"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: tocList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.tocModel && panel.tocModel.length > 0
                    clip: true
                    model: panel.tocModel
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: panel.open && panel.activeTab === "contents" && count > 0
                    Accessible.role: Accessible.List
                    Accessible.name: "Contents"
                    Keys.onPressed: function(event) { tocKeys.handle(event) }

                    delegate: Item {
                        id: chrow
                        required property var modelData
                        required property int index
                        readonly property string rowState: L.tocRowState(chrow.index, panel.currentTocIndex)
                        width: tocList.width - 12
                        height: chLabel.implicitHeight + 20

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: (tocList.activeFocus && tocList.currentIndex === chrow.index) ? Theme.rowHover
                                 : (chrow.rowState === "current" ? Theme.goldWash
                                 : (chMa.containsMouse ? Theme.rowHover : "transparent"))
                        }
                        Row {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            Text {
                                width: 20
                                text: String(chrow.index + 1)
                                horizontalAlignment: Text.AlignLeft
                                color: chrow.rowState === "current" ? Theme.gold : Theme.inkGhost
                                font.family: Theme.ui
                                font.pixelSize: 11
                            }
                            Text {
                                id: chLabel
                                width: parent.width - 20 - parent.spacing
                                text: (chrow.modelData && chrow.modelData.label) ? String(chrow.modelData.label) : ""
                                wrapMode: Text.WordWrap
                                font.family: Theme.ui
                                font.pixelSize: 14
                                color: chrow.rowState === "current" ? Theme.gold
                                     : (chrow.rowState === "read" ? Theme.inkGhost : Theme.inkDim)
                            }
                        }
                        ReaderKeyboardArea {
                            id: chMa
                            anchors.fill: parent
                            keyboardTabStop: false
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { tocList.currentIndex = chrow.index; panel.tocActivated((chrow.modelData && chrow.modelData.href) ? String(chrow.modelData.href) : "") }
                        }
                    }
                }
            }

            // ===== Bookmarks =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "bookmarks"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.bookmarks || panel.bookmarks.length === 0
                    text: "No bookmarks yet"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: bmList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.bookmarks && panel.bookmarks.length > 0
                    clip: true
                    model: panel.bookmarks
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: panel.open && panel.activeTab === "bookmarks" && count > 0
                    Accessible.role: Accessible.List
                    Accessible.name: "Bookmarks"
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Delete && currentIndex >= 0 && currentIndex < count) {
                            var row = L.bookmarkRow(panel.bookmarks[currentIndex])
                            panel.bookmarkDeleted(row.id); event.accepted = true; return
                        }
                        bookmarkKeys.handle(event)
                    }
                    spacing: 2

                    delegate: Item {
                        id: bmRow
                        required property var modelData
                        required property int index
                        readonly property var r: L.bookmarkRow(bmRow.modelData)
                        width: bmList.width - 12
                        height: bmCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: (bmList.activeFocus && bmList.currentIndex === bmRow.index) ? Theme.rowHover
                                 : (bmMa.containsMouse ? Theme.rowHover : "transparent")
                        }
                        Column {
                            id: bmCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                width: parent.width
                                text: bmRow.r.where
                                visible: bmRow.r.where !== ""
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.6
                                font.capitalization: Font.AllUppercase
                                color: Theme.inkGhost
                            }
                            Text {
                                width: parent.width
                                text: bmRow.r.snippet
                                visible: bmRow.r.snippet !== ""
                                wrapMode: Text.WordWrap
                                font.family: Theme.display
                                font.pixelSize: 14
                                color: Theme.inkDim
                            }
                        }
                        // main click → jump to the bookmark (declared FIRST so the × sits above it)
                        ReaderKeyboardArea {
                            id: bmMa
                            anchors.fill: parent
                            keyboardTabStop: false
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { bmList.currentIndex = bmRow.index; panel.bookmarkActivated(bmRow.r.cfi) }
                        }
                        // hover delete
                        Text {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: 10
                            anchors.topMargin: 10
                            visible: bmMa.containsMouse || bmDelMa.containsMouse
                            text: "×"
                            font.family: Theme.ui
                            font.pixelSize: 15
                            color: bmDelMa.containsMouse ? Theme.ink : Theme.inkFaint
                            ReaderKeyboardArea {
                                id: bmDelMa
                                anchors.fill: parent
                                keyboardTabStop: false
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.bookmarkDeleted(bmRow.r.id)
                            }
                        }
                    }
                }
            }

            // ===== Highlights =====
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "highlights"

                Text {
                    anchors.centerIn: parent
                    visible: !panel.highlights || panel.highlights.length === 0
                    text: "No highlights yet"
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                }

                ListView {
                    id: hlList
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 6
                    anchors.topMargin: 14
                    anchors.bottomMargin: 20
                    visible: panel.highlights && panel.highlights.length > 0
                    clip: true
                    model: panel.highlights
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: panel.open && panel.activeTab === "highlights" && count > 0
                    Accessible.role: Accessible.List
                    Accessible.name: "Highlights"
                    Keys.onPressed: function(event) { highlightKeys.handle(event) }
                    spacing: 2

                    delegate: Item {
                        id: hlRow
                        required property var modelData
                        required property int index
                        readonly property var r: L.highlightRow(hlRow.modelData)
                        width: hlList.width - 12
                        height: hlCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: (hlList.activeFocus && hlList.currentIndex === hlRow.index) ? Theme.rowHover
                                 : (hlMa.containsMouse ? Theme.rowHover : "transparent")
                        }
                        Column {
                            id: hlCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                width: parent.width
                                text: hlRow.r.where
                                visible: hlRow.r.where !== ""
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.letterSpacing: 1.6
                                font.capitalization: Font.AllUppercase
                                color: Theme.inkGhost
                            }
                            // colored left edge-rule + serif quote (mock .hl .snippet)
                            Item {
                                width: parent.width
                                height: hlText.implicitHeight
                                Rectangle {
                                    id: hlEdge
                                    width: 3
                                    radius: 1
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    color: hlRow.r.color !== "" ? hlRow.r.color : Theme.gold
                                }
                                Text {
                                    id: hlText
                                    anchors.left: hlEdge.right
                                    anchors.leftMargin: 10
                                    anchors.right: parent.right
                                    text: hlRow.r.text
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.display
                                    font.pixelSize: 14
                                    color: Theme.inkDim
                                }
                            }
                            // optional indented note (mock .mark .note)
                            Text {
                                width: parent.width
                                visible: hlRow.r.note !== ""
                                leftPadding: 10
                                text: hlRow.r.note
                                wrapMode: Text.WordWrap
                                font.family: Theme.ui
                                font.pixelSize: 13
                                color: Theme.inkFaint
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 2
                                    color: Theme.noteRule
                                }
                            }
                        }
                        ReaderKeyboardArea {
                            id: hlMa
                            anchors.fill: parent
                            keyboardTabStop: false
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { hlList.currentIndex = hlRow.index; panel.highlightActivated(hlRow.r.cfi) }
                        }
                    }
                }
            }

            // ===== Audio (Task 13 — read-along) =====
            // Attached: the mock's audiobook card + "Follow my reading" switch + a mini
            // transport (play, chapter/time, progress rail, speed). Unattached: a quiet
            // message — downloading the audiobook is Biblio's job, not the reader's.
            Item {
                anchors.fill: parent
                visible: panel.activeTab === "audio"

                // ---- unattached state ----
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    anchors.topMargin: 30
                    visible: !panel.audioAttached
                    text: "Download the audiobook from this book's page to read along."
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.inkGhost
                    font.family: Theme.ui
                    font.pixelSize: 13
                    lineHeight: 1.4
                }

                // ---- attached state (scrollable — the playlist can outgrow the pane) ----
                Flickable {
                    id: audioFlick
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    anchors.topMargin: 14
                    visible: panel.audioAttached
                    contentWidth: width
                    contentHeight: abPaneCol.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: abPaneCol
                    width: parent.width
                    spacing: 14

                    // --- attached-audiobook card (mock .audiohead) ---
                    Rectangle {
                        width: parent.width
                        // grows if a long title wraps to two lines; never shorter than the cover.
                        height: Math.max(56, abCardText.implicitHeight) + 28
                        radius: 11
                        color: Theme.cardBg
                        border.color: Theme.barBorder
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 14

                            // cover (art if we have it, else a headphones glyph on a dark tile)
                            Rectangle {
                                width: 56; height: 56; radius: 8
                                color: "#171b26"
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    visible: String(panel.audioCover) !== ""
                                    source: panel.audioCover
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                Image {
                                    anchors.centerIn: parent
                                    visible: String(panel.audioCover) === ""
                                    source: Qt.resolvedUrl("../../assets/icons/reader2/headphones.svg")
                                    width: 22; height: 22
                                    sourceSize.width: 44; sourceSize.height: 44
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                            }

                            Column {
                                id: abCardText
                                width: parent.width - 56 - parent.spacing
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Text {
                                    width: parent.width
                                    text: panel.audioTitle
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    font.family: Theme.ui
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.inkTitle
                                }
                                Text {
                                    width: parent.width
                                    text: panel.audioMetaLine
                                    elide: Text.ElideRight
                                    font.family: Theme.ui
                                    font.pixelSize: 12
                                    color: Theme.inkFaint
                                }
                                // gold pill: "Downloaded for this book" (mock .paired)
                                Row {
                                    spacing: 5
                                    Rectangle {
                                        width: 5; height: 5; radius: 2.5
                                        color: Theme.gold
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: "Downloaded for this book"
                                        font.family: Theme.ui
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1.4
                                        font.capitalization: Font.AllUppercase
                                        color: Theme.gold
                                    }
                                }
                            }
                        }
                    }

                    // --- "Follow my reading" row (mock .followrow) ---
                    Rectangle {
                        width: parent.width
                        height: 60
                        radius: 11
                        color: Theme.cardBg
                        border.color: Theme.barBorder
                        border.width: 1

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.right: followSwitch.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: "Follow my reading"
                                font.family: Theme.ui
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: Theme.inkDim
                            }
                            Text {
                                width: parent.width
                                text: "Audio keeps pace with your page turns"
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 11
                                color: Theme.inkGhost
                            }
                        }

                        // the pill switch (mock .switch): 40×22 track + 16px knob.
                        Rectangle {
                            id: followSwitch
                            width: 40; height: 22; radius: 11
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            color: panel.followOn ? Theme.gold : Theme.switchTrackOff
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Rectangle {
                                width: 16; height: 16; radius: 8
                                color: Theme.switchKnob
                                anchors.verticalCenter: parent.verticalCenter
                                x: panel.followOn ? parent.width - width - 3 : 3
                                Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                            }
                            ReaderKeyboardArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.followToggled(!panel.followOn)
                            }
                        }
                    }

                    // --- SYNC STATUS (Task 7 — honest read-along alignment progress) ---
                    // DORMANT-GATED on textSyncOn (native service present + a book id). A pure
                    // renderer of the service's OWN status: a summary line, a ready count, every
                    // chapter's state, per-chapter Retry on a failure (the chapter stays playable
                    // as ordinary audio), pause/resume, and a confirmation-gated Restart. The
                    // controls call the service directly; the view refreshes on jobChanged.
                    Rectangle {
                        width: parent.width
                        visible: panel.textSyncOn
                        height: visible ? (tsStatusCol.implicitHeight + 28) : 0
                        radius: 11
                        color: Theme.cardBg
                        border.color: Theme.barBorder
                        border.width: 1

                        Column {
                            id: tsStatusCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 10

                            // header + pause/resume (hidden once every chapter is ready)
                            Item {
                                width: parent.width
                                height: tsHeader.implicitHeight
                                Text {
                                    id: tsHeader
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Sync status"
                                    font.family: Theme.ui
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    font.letterSpacing: 1.4
                                    font.capitalization: Font.AllUppercase
                                    color: Theme.inkFaint
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !panel.textSyncAllReady
                                    text: panel.textSyncStatus.paused ? "Resume" : "Pause"
                                    font.family: Theme.ui
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: tsPauseMa.containsMouse ? Theme.ink : Theme.inkDim
                                    ReaderKeyboardArea {
                                        id: tsPauseMa
                                        anchors.fill: parent
                                        anchors.margins: -8
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: panel.textSyncStatus.paused ? panel.resumeTextSync() : panel.pauseTextSync()
                                    }
                                }
                            }

                            // the one-line summary (Syncing chapter K of N · <stage> / All N ready)
                            Text {
                                width: parent.width
                                text: panel.textSyncSummaryText
                                wrapMode: Text.WordWrap
                                font.family: Theme.ui
                                font.pixelSize: 13
                                color: Theme.inkDim
                            }
                            // K chapters ready (redundant once the summary says "All N ready")
                            Text {
                                width: parent.width
                                visible: !panel.textSyncAllReady && panel.textSyncReadyText !== ""
                                text: panel.textSyncReadyText
                                font.family: Theme.ui
                                font.pixelSize: 11
                                color: Theme.inkGhost
                            }

                            // per-chapter states — one row each; a failed row shows its plain
                            // failure line (gold) + a Retry that re-attempts just that chapter.
                            Column {
                                width: parent.width
                                spacing: 0
                                Repeater {
                                    model: panel.textSyncChapters
                                    delegate: Item {
                                        id: tsRow
                                        required property var modelData
                                        required property int index
                                        readonly property bool failed: L.chapterFailed(tsRow.modelData)
                                        readonly property int chIndex: (tsRow.modelData && Number.isFinite(tsRow.modelData.index))
                                                                       ? tsRow.modelData.index : tsRow.index
                                        width: parent.width
                                        height: 30
                                        Text {
                                            id: tsRowNum
                                            anchors.left: parent.left
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 24
                                            text: String(tsRow.chIndex + 1)
                                            font.family: Theme.ui
                                            font.pixelSize: 11
                                            color: Theme.inkGhost
                                        }
                                        Text {
                                            anchors.left: tsRowNum.right
                                            anchors.leftMargin: 6
                                            anchors.right: tsRetry.left
                                            anchors.rightMargin: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: L.chapterStateText(tsRow.modelData)
                                            elide: Text.ElideRight
                                            font.family: Theme.ui
                                            font.pixelSize: 12
                                            color: tsRow.failed ? Theme.gold : Theme.inkDim
                                        }
                                        Text {
                                            id: tsRetry
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: tsRow.failed
                                            text: "Retry"
                                            font.family: Theme.ui
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: tsRetryMa.containsMouse ? Theme.ink : Theme.inkDim
                                            ReaderKeyboardArea {
                                                id: tsRetryMa
                                                anchors.fill: parent
                                                anchors.margins: -8
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: panel.retryChapter(tsRow.chIndex)
                                            }
                                        }
                                    }
                                }
                            }

                            // protected Restart — a confirm step gates it (re-runs everything).
                            Item {
                                width: parent.width
                                height: 24
                                Text {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !panel.restartArmed
                                    text: "Restart sync"
                                    font.family: Theme.ui
                                    font.pixelSize: 12
                                    color: tsRestartMa.containsMouse ? Theme.ink : Theme.inkGhost
                                    ReaderKeyboardArea {
                                        id: tsRestartMa
                                        anchors.fill: parent
                                        anchors.margins: -8
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: panel.requestRestart()
                                    }
                                }
                                Row {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: panel.restartArmed
                                    spacing: 12
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "Re-run all chapters?"
                                        font.family: Theme.ui
                                        font.pixelSize: 12
                                        color: Theme.inkDim
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "Confirm"
                                        font.family: Theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: tsConfirmMa.containsMouse ? Theme.gold : Theme.inkDim
                                        ReaderKeyboardArea {
                                            id: tsConfirmMa
                                            anchors.fill: parent
                                            anchors.margins: -8
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: panel.confirmRestart()
                                        }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "Cancel"
                                        font.family: Theme.ui
                                        font.pixelSize: 12
                                        color: tsCancelMa.containsMouse ? Theme.ink : Theme.inkGhost
                                        ReaderKeyboardArea {
                                            id: tsCancelMa
                                            anchors.fill: parent
                                            anchors.margins: -8
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: panel.cancelRestart()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- TEXT SYNC controls (Task 6 — read-along) --- DORMANT-GATED: only
                    // shows when the native read-along engine is present for this book. Mode
                    // (Sentence / Word / Sentence + Word) + a word-enlargement stepper. Bridge-
                    // free: it reports the pick up; ReaderShell persists it + styles the paper.
                    Rectangle {
                        width: parent.width
                        visible: panel.readAlongAvailable
                        height: visible ? (tsCol.implicitHeight + 28) : 0
                        radius: 11
                        color: Theme.cardBg
                        border.color: Theme.barBorder
                        border.width: 1

                        Column {
                            id: tsCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 12

                            Text {
                                text: "Text sync"
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                font.letterSpacing: 1.4
                                font.capitalization: Font.AllUppercase
                                color: Theme.inkFaint
                            }

                            // segmented mode control — three equal cells, the active one gold.
                            Row {
                                width: parent.width
                                spacing: 0
                                readonly property real segW: (width - 2) / 3
                                RaMode { width: parent.segW; label: "Sentence";      mode: "sentence" }
                                RaMode { width: parent.segW; label: "Word";          mode: "word" }
                                RaMode { width: parent.segW; label: "Sentence + Word"; mode: "sentenceWord" }
                            }

                            // word enlargement stepper (− value +), disabled when Word emphasis is off.
                            Row {
                                width: parent.width
                                spacing: 10
                                readonly property bool wordShown: panel.readAlongMode !== "sentence"
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 3 * 34 - 2 * parent.spacing
                                    text: "Word size"
                                    font.family: Theme.ui
                                    font.pixelSize: 13
                                    color: parent.wordShown ? Theme.inkDim : Theme.inkGhost
                                }
                                RaStep { symbol: "−"; enabledStep: parent.wordShown && panel.readAlongWordScale > 1.0
                                         onStepped: panel.readAlongScaleChanged(Math.max(1.0, panel.readAlongWordScale - 0.1)) }
                                Rectangle {
                                    width: 34; height: 30; radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: (Math.round(panel.readAlongWordScale * 10) / 10).toFixed(1) + "×"
                                        font.family: Theme.ui
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: parent.parent.wordShown ? Theme.inkTitle : Theme.inkGhost
                                    }
                                }
                                RaStep { symbol: "+"; enabledStep: parent.wordShown && panel.readAlongWordScale < 2.0
                                         onStepped: panel.readAlongScaleChanged(Math.min(2.0, panel.readAlongWordScale + 0.1)) }
                            }
                        }
                    }

                    // --- THE PLAYLIST (Hemanth 2026-07-18: every chapter/file of the audiobook,
                    // listed the way Contents lists the book's chapters — filenames as labels;
                    // tap a row to play it; the playing row is marked gold. The transport
                    // itself lives on the reader's HUD pill now.) ---
                    Column {
                        id: playlistRegion
                        width: parent.width
                        spacing: 0
                        property int keyboardIndex: panel.audioPlaylist && panel.audioPlaylist.length > 0
                                                    ? Math.max(0, Math.min(panel.audioPlaylist.length - 1, panel.audioCurrentIndex >= 0 ? panel.audioCurrentIndex : 0)) : -1
                        activeFocusOnTab: panel.open && panel.activeTab === "audio" && panel.audioAttached
                                          && panel.audioPlaylist && panel.audioPlaylist.length > 0
                        Accessible.role: Accessible.List
                        Accessible.name: "Audiobook playlist"
                        function ensureKeyboardVisible() {
                            var row = playlistRepeater.itemAt(keyboardIndex)
                            if (!row) return
                            var pt = row.mapToItem(abPaneCol, 0, 0)
                            audioFlick.contentY = Math.max(0, Math.min(audioFlick.contentHeight - audioFlick.height,
                                pt.y - Math.max(0, (audioFlick.height - row.height) / 2)))
                        }
                        Keys.onPressed: function(event) {
                            var n = panel.audioPlaylist ? panel.audioPlaylist.length : 0
                            if (n <= 0) return
                            var next = keyboardIndex
                            if (event.key === Qt.Key_Up) next = Math.max(0, keyboardIndex - 1)
                            else if (event.key === Qt.Key_Down) next = Math.min(n - 1, keyboardIndex + 1)
                            else if (event.key === Qt.Key_Home) next = 0
                            else if (event.key === Qt.Key_End) next = n - 1
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                panel.audioChapterPicked(keyboardIndex); event.accepted = true; return
                            } else return
                            if (next !== keyboardIndex) { keyboardIndex = next; ensureKeyboardVisible(); event.accepted = true }
                        }
                        Text {
                            text: "Playlist"
                            leftPadding: 4
                            bottomPadding: 8
                            font.family: Theme.ui
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 1.4
                            font.capitalization: Font.AllUppercase
                            color: Theme.inkFaint
                        }
                        Repeater {
                            id: playlistRepeater
                            model: panel.audioPlaylist
                            delegate: Item {
                                id: plRow
                                required property var modelData
                                required property int index
                                readonly property bool current: index === panel.audioCurrentIndex
                                width: parent.width; height: 40
                                Rectangle {
                                    anchors.fill: parent; radius: 8
                                    color: (playlistRegion.activeFocus && playlistRegion.keyboardIndex === plRow.index) ? Theme.rowHover
                                         : (plMa.containsMouse ? Qt.rgba(1, 1, 1, 0.06)
                                         : plRow.current ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                                }
                                // playing marker — a slim gold bar, same vocabulary as the
                                // Contents current-chapter marker.
                                Rectangle {
                                    visible: plRow.current
                                    width: 3; height: 18; radius: 1.5
                                    color: Theme.gold
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 14
                                    anchors.right: plNum.left; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: String(plRow.modelData)
                                    elide: Text.ElideRight
                                    font.family: Theme.ui
                                    font.pixelSize: 13
                                    color: plRow.current ? Theme.inkTitle : Theme.inkDim
                                }
                                Text {
                                    id: plNum
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    // the playing row shows a state glyph; others their track number
                                    text: plRow.current ? (panel.audioPlaying ? "playing" : "paused")
                                                        : (plRow.index + 1)
                                    font.family: Theme.ui
                                    font.pixelSize: 11
                                    color: plRow.current ? Theme.gold : Theme.inkGhost
                                }
                                ReaderKeyboardArea {
                                    id: plMa
                                    anchors.fill: parent
                                    keyboardTabStop: false
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { playlistRegion.keyboardIndex = plRow.index; panel.audioChapterPicked(plRow.index) }
                                }
                            }
                        }
                    }
                }
                }
            }
        }
    }

    ReaderKeyboardCollectionController {
        id: tocKeys
        view: tocList
        orientation: "vertical"
        count: tocList.count
        onActivated: function(index) {
            var row = panel.tocModel && index >= 0 && index < panel.tocModel.length ? panel.tocModel[index] : null
            panel.tocActivated(row && row.href ? String(row.href) : "")
        }
    }
    ReaderKeyboardCollectionController {
        id: bookmarkKeys
        view: bmList
        orientation: "vertical"
        count: bmList.count
        onActivated: function(index) {
            if (!panel.bookmarks || index < 0 || index >= panel.bookmarks.length) return
            panel.bookmarkActivated(L.bookmarkRow(panel.bookmarks[index]).cfi)
        }
    }
    ReaderKeyboardCollectionController {
        id: highlightKeys
        view: hlList
        orientation: "vertical"
        count: hlList.count
        onActivated: function(index) {
            if (!panel.highlights || index < 0 || index >= panel.highlights.length) return
            panel.highlightActivated(L.highlightRow(panel.highlights[index]).cfi)
        }
    }

    // ---------- read-along (Task 6) mode segment: an equal-width cell, gold when it's the
    // active mode; a tap reports the pick up (ReaderShell persists + styles the paper) ----------
    component RaMode: Item {
        id: raSeg
        property string label: ""
        property string mode: ""
        readonly property bool active: panel.readAlongMode === raSeg.mode
        height: 34
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 8
            color: raSeg.active ? Theme.goldWash : (raSegMa.containsMouse ? Theme.rowHover : "transparent")
            border.color: raSeg.active ? Theme.gold : Theme.barBorder
            border.width: 1
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 8
            horizontalAlignment: Text.AlignHCenter
            text: raSeg.label
            elide: Text.ElideRight
            font.family: Theme.ui
            font.pixelSize: 11
            font.weight: raSeg.active ? Font.DemiBold : Font.Normal
            color: raSeg.active ? Theme.gold : Theme.inkDim
        }
        ReaderKeyboardArea {
            id: raSegMa
            anchors.fill: parent
            keyboardLabel: raSeg.label
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.readAlongModePicked(raSeg.mode)
        }
    }

    // ---------- read-along (Task 6) enlargement stepper button (− / +) ----------
    component RaStep: Rectangle {
        id: raStep
        property string symbol: ""
        property bool enabledStep: true
        signal stepped()
        width: 34; height: 30; radius: 8
        anchors.verticalCenter: parent.verticalCenter
        color: raStepMa.containsMouse && raStep.enabledStep ? Theme.rowHover : "transparent"
        border.color: Theme.barBorder
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: raStep.symbol
            font.family: Theme.ui
            font.pixelSize: 16
            color: raStep.enabledStep ? Theme.inkDim : Theme.inkGhost
        }
        ReaderKeyboardArea {
            id: raStepMa
            anchors.fill: parent
            keyboardLabel: raStep.symbol === "+" ? "Increase word size" : "Decrease word size"
            hoverEnabled: true
            cursorShape: raStep.enabledStep ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (raStep.enabledStep) raStep.stepped()
        }
    }

    // ---------- a tab: SVG icon centered in an equal-width cell + gold underline when
    // active; the tab NAME lives in the hover tooltip (icon tabs, Hemanth 2026-07-18) ----------
    component Tab: Item {
        id: tab
        property string icon: ""            // filename under assets/icons/reader2/
        property bool active: false
        property bool tabEnabled: true
        property string tip: ""
        signal picked()
        height: 36
        function focusKeyboard() { tabMa.forceActiveFocus(Qt.OtherFocusReason) }

        Image {
            id: tabIcon
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 7
            width: 17; height: 17
            source: tab.icon !== "" ? Qt.resolvedUrl("../../assets/icons/reader2/" + tab.icon) : ""
            sourceSize: Qt.size(34, 34)     // 2x for crisp scaling
            // icons ship white; dim inactive/disabled states via opacity (no recolor pass needed)
            opacity: tab.active ? 1.0 : (tab.tabEnabled ? 0.45 : 0.22)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: Theme.gold
            visible: tab.active
        }
        ReaderKeyboardArea {
            id: tabMa
            anchors.fill: parent
            keyboardLabel: tab.tip
            hoverEnabled: true
            cursorShape: tab.tabEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (tab.tabEnabled) tab.picked()
        }
        // hover tooltip — now carries the tab NAME for every tab (icons have no text).
        Rectangle {
            visible: tabMa.containsMouse && tab.tip !== ""
            anchors.top: parent.bottom
            anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            z: 50
            radius: 6
            color: Theme.bar
            border.color: Theme.barBorder
            border.width: 1
            width: tipText.implicitWidth + 16
            height: tipText.implicitHeight + 10
            Text {
                id: tipText
                anchors.centerIn: parent
                text: tab.tip
                color: Theme.inkDim
                font.family: Theme.ui
                font.pixelSize: 12
            }
        }
    }
}
