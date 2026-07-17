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

    // ---- signals up to ReaderChrome / ReaderShell ----
    signal backRequested()
    signal searchRequested()
    signal contentsRequested()
    signal appearanceRequested()
    signal bookmarkRequested()

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
        signal clicked()
        implicitWidth: box + 10
        implicitHeight: box + 10
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
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ib.clicked()
        }
    }

    // back arrow (left, 22px, inkDim → ink on hover)
    IconButton {
        id: backBtn
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 22
        box: 22
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
            color: Theme.inkDim
            font.family: Theme.ui
            font.pixelSize: 13
            rightPadding: 4
        }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            source: Qt.resolvedUrl("../../assets/icons/reader2/search.svg"); onClicked: root.searchRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            source: Qt.resolvedUrl("../../assets/icons/reader2/contents.svg"); onClicked: root.contentsRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            source: Qt.resolvedUrl("../../assets/icons/reader2/appearance.svg"); onClicked: root.appearanceRequested() }
        IconButton { anchors.verticalCenter: parent.verticalCenter
            source: Qt.resolvedUrl("../../assets/icons/reader2/bookmark.svg"); onClicked: root.bookmarkRequested() }
    }
}
