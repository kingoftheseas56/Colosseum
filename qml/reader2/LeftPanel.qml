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
    property bool audioPlaying: false            // session playing (drives play/pause glyph)
    property string audioTimeLine: ""            // "Chapter 1 — Loomings · 04:12 / 22:30"
    property real audioProgress: 0               // 0..1, the mini rail fill
    property string audioSpeedLabel: "1.0×"       // the speed pill text

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
    signal audioSeekRequested(real fraction)     // scrub the mini rail (0..1)

    readonly property int colWidth: 348
    readonly property int topBarPx: 64           // keep the top bar's right icons clickable

    // ---------- click-outside-to-dismiss (transparent; paper area right of the column) ----------
    // Only armed while open, and inset below the top bar so the TopBar icons stay live.
    MouseArea {
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
        MouseArea {
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
            Tab { width: tabStrip.tabW; icon: "contents.svg";  tip: "Contents";   active: panel.activeTab === "contents";   onPicked: panel.tabSelected("contents") }
            Tab { width: tabStrip.tabW; icon: "bookmark.svg";  tip: "Bookmarks";  active: panel.activeTab === "bookmarks";  onPicked: panel.tabSelected("bookmarks") }
            Tab { width: tabStrip.tabW; icon: "highlight.svg"; tip: "Highlights"; active: panel.activeTab === "highlights"; onPicked: panel.tabSelected("highlights") }
            // Audio (Task 13) — the read-along pane: attached audiobook + Follow switch +
            // mini transport. Live now (the placeholder/disabled state is gone).
            Tab { width: tabStrip.tabW; icon: "headphones.svg"; tip: "Audio"; active: panel.activeTab === "audio"; onPicked: panel.tabSelected("audio") }
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
                            color: chrow.rowState === "current" ? Theme.goldWash
                                 : (chMa.containsMouse ? Theme.rowHover : "transparent")
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
                        MouseArea {
                            id: chMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.tocActivated((chrow.modelData && chrow.modelData.href) ? String(chrow.modelData.href) : "")
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
                    spacing: 2

                    delegate: Item {
                        id: bmRow
                        required property var modelData
                        readonly property var r: L.bookmarkRow(bmRow.modelData)
                        width: bmList.width - 12
                        height: bmCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: bmMa.containsMouse ? Theme.rowHover : "transparent"
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
                        MouseArea {
                            id: bmMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.bookmarkActivated(bmRow.r.cfi)
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
                            MouseArea {
                                id: bmDelMa
                                anchors.fill: parent
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
                    spacing: 2

                    delegate: Item {
                        id: hlRow
                        required property var modelData
                        readonly property var r: L.highlightRow(hlRow.modelData)
                        width: hlList.width - 12
                        height: hlCol.implicitHeight + 24

                        Rectangle {
                            anchors.fill: parent
                            radius: 9
                            color: hlMa.containsMouse ? Theme.rowHover : "transparent"
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
                        MouseArea {
                            id: hlMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.highlightActivated(hlRow.r.cfi)
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

                // ---- attached state ----
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    anchors.topMargin: 14
                    spacing: 14
                    visible: panel.audioAttached

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
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.followToggled(!panel.followOn)
                            }
                        }
                    }

                    // --- mini transport (mock .transport) ---
                    Rectangle {
                        width: parent.width
                        height: 68
                        radius: 11
                        color: Theme.cardBg
                        border.color: Theme.barBorder
                        border.width: 1

                        // play / pause — white circle, dark glyph (mock .transport .play)
                        Rectangle {
                            id: playBtn
                            width: 40; height: 40; radius: 20
                            color: Theme.ink
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            Image {
                                anchors.centerIn: parent
                                // nudge the play triangle right for optical centering (mock margin-left:2)
                                anchors.horizontalCenterOffset: panel.audioPlaying ? 0 : 2
                                source: panel.audioPlaying
                                        ? Qt.resolvedUrl("../../assets/icons/reader2/pause-dark.svg")
                                        : Qt.resolvedUrl("../../assets/icons/reader2/play-dark.svg")
                                width: 16; height: 16
                                sourceSize.width: 32; sourceSize.height: 32
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.audioPlayToggled()
                            }
                        }

                        // chapter · time + the progress rail (mock .tinfo)
                        Column {
                            anchors.left: playBtn.right
                            anchors.leftMargin: 14
                            anchors.right: speedBtn.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Text {
                                width: parent.width
                                text: panel.audioTimeLine
                                elide: Text.ElideRight
                                font.family: Theme.ui
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: Theme.inkDim
                            }
                            // scrubbable rail (mock .rail2 — display + click-to-seek)
                            Rectangle {
                                id: miniRail
                                width: parent.width
                                height: 3
                                radius: 2
                                color: Theme.track
                                Rectangle {
                                    height: parent.height
                                    radius: 2
                                    color: Theme.gold
                                    width: parent.width * Math.max(0, Math.min(1, panel.audioProgress))
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.topMargin: -8
                                    anchors.bottomMargin: -8
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: (m) => panel.audioSeekRequested(
                                                   Math.max(0, Math.min(1, m.x / miniRail.width)))
                                }
                            }
                        }

                        // speed pill (mock .spd)
                        Rectangle {
                            id: speedBtn
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: speedTxt.implicitWidth + 14
                            height: 26
                            radius: 6
                            color: "transparent"
                            border.color: Theme.barBorder
                            border.width: 1
                            Text {
                                id: speedTxt
                                anchors.centerIn: parent
                                text: panel.audioSpeedLabel
                                font.family: Theme.ui
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                font.letterSpacing: 0.6
                                color: Theme.inkFaint
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.audioSpeedCycled()
                            }
                        }
                    }
                }
            }
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
        MouseArea {
            id: tabMa
            anchors.fill: parent
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
