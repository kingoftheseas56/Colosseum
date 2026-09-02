// TopBar.qml — the reader's ICON-ONLY top chrome (Hemanth's ratified amendment: no
// pills, no text buttons). Left: a back arrow. Center: title (Fraunces) + author
// (Inter, quiet). Right: the current chapter label + four line icons — search,
// contents, appearance, bookmark. Glass over the paper; it fades/slides in with the
// reveal (ReaderChrome drives `shown`). Pixel contract: the chrome mock's `.topbar`.
//
// Icons are white-stroke SVGs recolored purely by opacity (the ink ramp IS white at
// alpha), so `inkDim` = the icon at 0.62, hover = 1.0. No GraphicalEffects needed.
//
// [Agent 2 (Claude), biblio]
import QtQuick

Item {
    id: root
    height: 64

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
        implicitWidth: box + 10
        implicitHeight: box + 10
        function focusKeyboard() { ma.forceActiveFocus(Qt.OtherFocusReason) }
        Image {
            id: img
            anchors.centerIn: parent
            width: ib.box
            height: ib.box
            sourceSize.width: ib.box * 2
            sourceSize.height: ib.box * 2
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: ma.containsMouse ? 1.0 : 0.62      // ink → inkDim
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        ReaderKeyboardArea {
            id: ma
            anchors.fill: parent
            keyboardLabel: ib.label
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ib.clicked()
        }
    }

    // back arrow (left, 22px, inkDim → ink on hover)
    IconButton {
        id: backBtn
        objectName: "bookReaderBack"   // test-only seam: Lanista ui-click closes the reader session (goBack -> closed)
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 22
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
        anchors.centerIn: parent
        height: parent.height
        readonly property real authorW: authorText.visible ? authorText.implicitWidth + 10 : 0
        readonly property real sideReserve: Math.max(rightRow.width, backBtn.width) + 22 + 18
        readonly property real availW: Math.max(80, root.width - 2 * sideReserve)
        width: Math.min(titleText.implicitWidth + authorW, availW)

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - titleBlock.authorW)
            text: root.title
            elide: Text.ElideRight
            color: Theme.inkTitle
            font.family: Theme.display
            font.weight: Font.Medium
            font.pixelSize: 17
        }
        Text {
            id: authorText
            anchors.left: titleText.right
            anchors.leftMargin: 10
            anchors.baseline: titleText.baseline
            text: root.author
            visible: root.author !== ""
            // elide is INERT without an explicit width — unconstrained, a squeezed block
            // let the author overflow at natural width into the right cluster (the same
            // 2026-07-20 double-print). Clamp to the room the block actually has.
            width: Math.min(implicitWidth, Math.max(0, titleBlock.width - titleText.width - 10))
            elide: Text.ElideRight
            color: Theme.inkFaint
            font.family: Theme.ui
            font.pixelSize: 13
        }
    }

    // right cluster: chapter label + four icons
    Row {
        id: rightRow
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 22
        spacing: 12

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.chapterLabel
            visible: root.chapterLabel !== ""
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
        IconButton { anchors.verticalCenter: parent.verticalCenter
            label: "Minimize"; source: Qt.resolvedUrl("../../assets/icons/reader2/minimize.svg"); onClicked: root.minimizeRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            label: root.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
            source: Qt.resolvedUrl(root.shellWindowed
                ? "../../assets/icons/reader2/fullscreen.svg"
                : "../../assets/icons/reader2/fullscreen-exit.svg")
            onClicked: root.fullscreenRequested() }
        // close (window verb, player-parity — the X ends the session; Back does the same
        // from the keyboard side. Rightmost, matching the player/comic chrome order.)
        IconButton { anchors.verticalCenter: parent.verticalCenter
            label: "Close"; source: Qt.resolvedUrl("../../assets/icons/reader2/close.svg"); onClicked: root.closeRequested() }
    }
}
