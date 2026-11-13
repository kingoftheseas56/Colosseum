// TopBar.qml — the reader's icon-only desktop chrome with labelled Android reader
// actions. Left: a back arrow. Center: title (Fraunces) + author
// (Inter, quiet). Right: the current chapter label + four line icons — search,
// contents, appearance, bookmark. Glass over the paper; it fades/slides in with the
// reveal (ReaderChrome drives `shown`). Pixel contract: the chrome mock's `.topbar`.
//
// Icons are white-stroke SVGs recolored purely by opacity (the ink ramp IS white at
// alpha), so `inkDim` = the icon at 0.62, hover = 1.0. No GraphicalEffects needed.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import ".." as Shared

ReaderOverlay {
    id: root
    overlayActive: shown
    height: twoRow ? 158 : (onAndroid ? 72 : 64)

    // ---- inputs (the shell feeds these from book metadata + the paper's relocated) ----
    property string title: ""
    property string author: ""
    property string chapterLabel: ""
    property bool shown: false           // reveal drives this; controls interactivity
    property bool shellWindowed: false

    // ---- signals up to ReaderChrome / ReaderShell ----
    signal backRequested()
    signal searchRequested()
    signal contentsRequested()
    signal appearanceRequested()
    signal bookmarkRequested()
    signal minimizeRequested()   // window-verb parity with the player/comic readers (2026-07-18)
    signal fullscreenRequested()
    signal closeRequested()

    function focusSearch() { searchBtn.focusKeyboard() }
    function focusContents() { contentsBtn.focusKeyboard() }
    function focusAppearance() { appearanceBtn.focusKeyboard() }

    enabled: shown                        // when asleep, clicks fall through to the turn/tap zones

    // Android phone width. This bar lives in its own native overlay window there, so the app
    // window's androidHost is out of reach: gate on the platform and on the bar's own width.
    // Android places title, author and chapter above its labelled reader actions where
    // side-by-side controls would collide. Desktop remains its original icon-only bar.
    // Window verbs stay excluded on Android: Back already closes the reader session.
    readonly property bool onAndroid: Qt.platform.os === "android"
    // Keep a second row whenever the complete metadata and labelled action set
    // would collide, including portrait tablets. Never elide controls to force one row.
    readonly property bool compact: onAndroid && width < 600
    readonly property bool twoRow: onAndroid && width < 1180
    // Left-align Android metadata to retain space for the complete title and author.
    readonly property bool leftTitle: onAndroid

    Rectangle { anchors.fill: parent; color: Theme.scrim; visible: Qt.platform.os === "android"; opacity: 0.88; z: -1 }

    // reveal: fade + a small downward slide-in from the top edge (mock: translateY(-6)).
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: root.shown ? 0 : -6
        Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    }

    // an icon button: a white-stroke SVG dimmed by opacity, brightening on hover.
    component IconButton: Item {
        id: ib
        property alias source: img.source
        property int box: 19
        property string label: "Action"
        signal clicked()
        implicitWidth: root.onAndroid
            ? (root.twoRow ? Math.max(66, Math.min(84, Math.floor((root.width - 28) / 4))) : 84)
            : box + 10
        implicitHeight: root.onAndroid ? 72 : box + 10
        function focusKeyboard() { ma.forceActiveFocus(Qt.OtherFocusReason) }
        Image {
            id: img
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: root.onAndroid ? undefined : parent.verticalCenter
            anchors.top: root.onAndroid ? parent.top : undefined
            anchors.topMargin: root.onAndroid ? 8 : 0
            width: ib.box
            height: ib.box
            sourceSize.width: ib.box * 2
            sourceSize.height: ib.box * 2
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: ma.hovered ? 1.0 : 0.62      // ink → inkDim
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Shared.KeyboardAction {
            id: ma
            x: 0; y: 0; width: parent.width
            height: root.onAndroid ? 48 : parent.height
            accessibleName: ib.label
            tooltipText: ib.label
            spaceActivates: true
            androidTooltipPolicy: ib.label === "Contents" || ib.label === "Appearance" || ib.label === "Bookmark"
                ? "caption" : "drop"
            androidCaptionText: ib.label
            androidCaptionPlacement: "below"
            focusRadius: 8
            onTriggered: ib.clicked()
        }
    }

    // back arrow (left, 22px, inkDim → ink on hover)
    IconButton {
        id: backBtn
        objectName: "bookReaderBack"   // test-only seam: Lanista ui-click closes the reader session (goBack -> closed)
        anchors.verticalCenter: root.twoRow ? undefined : parent.verticalCenter
        anchors.top: root.twoRow ? parent.top : undefined
        anchors.topMargin: root.twoRow ? 12 : 0
        anchors.left: parent.left
        anchors.leftMargin: root.compact ? 6 : 22
        box: 22
        label: "Back"
        source: Qt.resolvedUrl("../../assets/icons/reader2/back.svg")
        onClicked: root.backRequested()
    }

    // centered title + author (title = Fraunces medium, author = Inter, quiet).
    // NOT a Row: baseline-aligning two different font sizes needs a sibling anchor,
    // which positioners disallow — so an anchored Item centers the pair cleanly.
    //
    // ELIDE CLAMP: the block is centered on the bar, so a very long title must not slide
    // under the back arrow (left) or the right icon cluster. We reserve the WIDER of the two
    // side clusters on BOTH sides (the block is symmetric about center) plus margins, cap the
    // block to what's left, and elide the title. The author keeps its natural width right
    // after the (possibly elided) title.
    Item {
        id: titleBlock
        // compact: left-aligned in the space between Back and the actions, never centred under them
        anchors.centerIn: root.leftTitle ? undefined : parent
        anchors.left: root.leftTitle ? backBtn.right : undefined
        anchors.leftMargin: root.leftTitle ? (root.compact ? 6 : 14) : 0
        anchors.verticalCenter: root.leftTitle && !root.twoRow ? parent.verticalCenter : undefined
        anchors.top: root.twoRow ? parent.top : undefined
        anchors.topMargin: root.twoRow ? 6 : 0
        height: root.twoRow ? 72 : parent.height
        readonly property real authorW: root.twoRow ? 0 : (authorText.visible ? authorText.implicitWidth + 10 : 0)
        readonly property real sideReserve: Math.max(rightRow.width, backBtn.width) + 22 + 18
        readonly property real availW: root.twoRow
            ? Math.max(40, root.width - (backBtn.x + backBtn.width) - 24)
            : (root.leftTitle
                ? Math.max(40, rightRow.x - (backBtn.x + backBtn.width) - (root.compact ? 14 : 28))
                : Math.max(80, root.width - 2 * sideReserve))
        width: root.twoRow ? availW : Math.min(titleText.implicitWidth + authorW, availW)

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: root.twoRow ? undefined : parent.verticalCenter
            anchors.top: root.twoRow ? parent.top : undefined
            anchors.topMargin: root.twoRow ? 3 : 0
            width: root.twoRow ? parent.width : Math.max(0, parent.width - titleBlock.authorW)
            text: root.title
            elide: Text.ElideRight
            color: Theme.inkTitle
            font.family: Theme.display
            font.weight: Font.Medium
            font.pixelSize: 17
        }
        Text {
            id: authorText
            anchors.left: root.twoRow ? parent.left : titleText.right
            anchors.leftMargin: root.twoRow ? 0 : 10
            anchors.baseline: root.twoRow ? undefined : titleText.baseline
            anchors.top: root.twoRow ? titleText.bottom : undefined
            anchors.topMargin: root.twoRow ? 2 : 0
            text: root.author
            visible: root.author !== ""
            // elide is INERT without an explicit width — unconstrained, a squeezed block
            // let the author overflow at natural width into the right cluster (the same
            // 2026-07-20 double-print). Clamp to the room the block actually has.
            width: root.twoRow ? parent.width : Math.min(implicitWidth, Math.max(0, titleBlock.width - titleText.width - 10))
            elide: Text.ElideRight
            color: Theme.inkFaint
            font.family: Theme.ui
            font.pixelSize: 13
        }
        Text {
            visible: root.twoRow && root.chapterLabel !== ""
            anchors.left: parent.left
            anchors.top: authorText.visible ? authorText.bottom : titleText.bottom
            anchors.topMargin: 2
            width: parent.width
            text: root.chapterLabel
            elide: Text.ElideRight
            color: Theme.inkDim
            font.family: Theme.ui
            font.pixelSize: 12
        }
    }

    // right cluster: chapter label + four icons
    Row {
        id: rightRow
        anchors.verticalCenter: root.twoRow ? undefined : parent.verticalCenter
        anchors.right: root.twoRow ? undefined : parent.right
        anchors.rightMargin: root.twoRow ? 0 : (root.compact ? 6 : 22)
        anchors.horizontalCenter: root.twoRow ? parent.horizontalCenter : undefined
        anchors.bottom: root.twoRow ? parent.bottom : undefined
        anchors.bottomMargin: root.twoRow ? 4 : 0
        spacing: root.twoRow ? 4 : 12

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.chapterLabel
            visible: root.chapterLabel !== "" && !root.twoRow
            // CAP (2026-07-20, the Wool 'Prologue 2110: Beneath the hills of…' finding):
            // uncapped, a long chapter name grows the right cluster until the centered
            // title block crushes to its 80px floor and the two print over each other.
            // The chapter is secondary context — it elides; the book's identity doesn't.
            width: Math.min(implicitWidth, 300)
            elide: Text.ElideRight
            color: Theme.inkDim
            font.family: Theme.ui
            font.pixelSize: 13
            rightPadding: 4
        }
        IconButton { id: searchBtn; anchors.verticalCenter: parent.verticalCenter
            label: "Search"; source: Qt.resolvedUrl("../../assets/icons/reader2/search.svg"); onClicked: root.searchRequested() }
        IconButton { id: contentsBtn; anchors.verticalCenter: parent.verticalCenter
            label: "Contents"; source: Qt.resolvedUrl("../../assets/icons/reader2/contents.svg"); onClicked: root.contentsRequested() }
        IconButton { id: appearanceBtn; anchors.verticalCenter: parent.verticalCenter
            label: "Appearance"; source: Qt.resolvedUrl("../../assets/icons/reader2/appearance.svg"); onClicked: root.appearanceRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            label: "Bookmark"; source: Qt.resolvedUrl("../../assets/icons/reader2/bookmark.svg"); onClicked: root.bookmarkRequested() }
        // minimize (window verb, comic-reader chrome parity 2026-07-18): park the book as a
        // taskbar tile instead of closing it. Back = close; this = keep the session warm.
        IconButton { anchors.verticalCenter: parent.verticalCenter; visible: !root.onAndroid
            label: "Minimize"; source: Qt.resolvedUrl("../../assets/icons/reader2/minimize.svg"); onClicked: root.minimizeRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter; visible: !root.onAndroid
            label: root.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
            source: Qt.resolvedUrl(root.shellWindowed
                ? "../../assets/icons/reader2/fullscreen.svg"
                : "../../assets/icons/reader2/fullscreen-exit.svg")
            onClicked: root.fullscreenRequested() }
        // close (window verb, player-parity — the X ends the session; Back does the same
        // from the keyboard side. Rightmost, matching the player/comic chrome order.)
        IconButton { anchors.verticalCenter: parent.verticalCenter; visible: !root.onAndroid
            label: "Close"; source: Qt.resolvedUrl("../../assets/icons/reader2/close.svg"); onClicked: root.closeRequested() }
    }
}
