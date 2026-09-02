// UniverseHallPage — the universe collection's "see all": THE HALL OF WORLDS, second form.
// Born a horizontal spine shelf (0662ffe); at 21 worlds the sideways walk failed Hemanth's
// hand ("it's just one long row") and on 2026-07-12 he ratified the LEDGER STACK from the
// mock: the books lie flat. Every universe is a slim full-width bar — gold index numeral,
// name set LEVEL in Fraunces (no rotated type), banner art washed dark behind. Hover
// breathes a bar taller to reveal the kicker, blurb, media ledger and "Enter →"; a straight
// click enters the world untouched (hover stays an enhancement). The pile scrolls DOWN like
// every other page — HouseScrollBar gold sliver + ScrollGlide — and scales forever.
// Not a tile grid (standing constraint). Data: the INSTALLED ROSTER, handed down by the
// shell ({extensionId, name, banner, logo} per universe) — the baked Universes.js list is
// gone, so installing or removing a universe is the only thing that changes this pile.
import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    // shell contract (mirrors the genre-page layers)
    property Item backdrop: null
    // handed down from Main.qml's installedUniverses — never read from a baked list here
    property var universes: []
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal exploreRequested(string extensionId, string name)   // a bar → win.openUniverse

    Theme { id: theme }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            root.backRequested()
            event.accepted = true
        }
    }

    property int hovered: -1              // which bar breathes (-1 = the pile at rest)
    Component.onCompleted: root.forceActiveFocus(Qt.TabFocusReason)

    // the breathe goes STILL while the pile moves (Hemanth 2026-07-13: "scrolling is a
    // nightmare" — bars ballooning under a stationary cursor shoved the content around
    // mid-scroll). Any contentY motion closes the open bar and holds the pile flat until
    // the walk settles.
    property bool walking: false
    Timer { id: walkSettle; interval: 260; onTriggered: root.walking = false }
    Connections {
        target: stackWalk
        function onContentYChanged() {
            root.walking = true
            root.hovered = -1
            walkSettle.restart()
        }
    }

    // ---- the wall the hall stands in ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }
        Image { anchors.fill: parent; visible: root.backdrop === null
                source: "../assets/wallpaper/captured-motion.jpg"
                fillMode: Image.PreserveAspectCrop; cache: true }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.025, 0.045, 0.9) }
    }

    // ---- header strip ----
    Column {
        id: head
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: 30
        spacing: 6
        z: 20
        Text { text: "THE COLLECTION"; color: theme.gold
               font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 3 }
        Row {
            spacing: 14
            Text { text: "Hall of Worlds"; color: theme.ink
                   font.family: theme.display; font.pixelSize: 34 }
            Text { text: root.universes.length + " universes"
                   color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                   anchors.baseline: parent.children[0].baseline }
        }
    }
    BackAction {
        x: theme.margin; y: 96; z: 20
        onTriggered: root.backRequested()
    }
    Row {
        z: 20
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 34
        spacing: 20
        UniverseChromeAction { accessibleName: "Minimize"; source: "../assets/icons/minimize.svg"; onTriggered: root.minimizeRequested() }
        UniverseChromeAction {
            accessibleName: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "Enter fullscreen" : "Exit fullscreen"
            source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
            onTriggered: root.fullscreenRequested()
        }
        UniverseChromeAction { accessibleName: "Close Colosseum"; source: "../assets/icons/power.svg"; onTriggered: root.closeRequested() }
    }

    // ---- THE STACK — the books lie flat, one world under another ----
    Flickable {
        id: stackWalk
        property int currentIndex: root.universes.length > 0 ? 0 : -1
        focusPolicy: root.universes.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => hallNav.handle(event)
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.topMargin: 140
        anchors.bottom: parent.bottom; anchors.bottomMargin: 34
        anchors.leftMargin: theme.margin; anchors.rightMargin: theme.margin
        contentWidth: width
        contentHeight: pile.implicitHeight + 12
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: stackWalk }
        ScrollGlide { flick: stackWalk }

    Column {
        id: pile
        width: stackWalk.width
        spacing: 8

        // geometry: a bar rests slim; the hovered one breathes open (heights, not widths —
        // the whole collection stays one flick away, nothing ever walks sideways)
        readonly property int restH: 64
        // was 176, sized for the baked list's blurb + media ledger. The roster carries
        // identity and art, not prose or counts, so the breathe now reveals the kicker and
        // the "Enter the universe →" affordance and is sized to exactly that — a taller bar
        // would be an empty hole where copy we do not have used to sit.
        readonly property int openH: 98

        Repeater {
            id: hallRepeater
            model: root.universes
            delegate: Item {
                id: bar
                required property var modelData
                required property int index
                readonly property bool open: root.hovered === bar.index || (stackWalk.activeFocus && stackWalk.currentIndex === bar.index)
                width: pile.width
                height: open ? pile.openH : pile.restH
                Behavior on height { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    radius: 14; clip: true
                    color: "#14161d"   // the per-IP accent went with the baked list
                    border.width: 1
                    border.color: bar.open ? Qt.rgba(0.94, 0.77, 0.29, 0.65)
                                           : Qt.rgba(0.97, 0.97, 0.96, 0.10)
                    Behavior on border.color { ColorAnimation { duration: 240 } }

                    // the world's art, washed dark so the type carries the bar
                    Image {
                        anchors.fill: parent
                        source: bar.modelData.banner
                        asynchronous: true; cache: true
                        fillMode: Image.PreserveAspectCrop
                        opacity: status === Image.Ready ? (bar.open ? 0.55 : 0.30) : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.74) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, bar.open ? 0.45 : 0.28) }
                        }
                    }

                    // ---- title row (always level, always present) ----
                    Item {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top
                        height: pile.restH
                        Text {   // the index numeral — library plate
                            id: plate
                            anchors.left: parent.left; anchors.leftMargin: 22
                            anchors.verticalCenter: parent.verticalCenter
                            text: (bar.index + 1 < 10 ? "0" : "") + (bar.index + 1)
                            color: Qt.rgba(0.94, 0.77, 0.29, 0.8)
                            font.family: theme.display; font.pixelSize: 14; font.italic: true
                        }
                        Text {   // the name reads level — no tilted head
                            anchors.left: plate.right; anchors.leftMargin: 18
                            anchors.right: enterRow.left; anchors.rightMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            text: bar.modelData.name
                            color: theme.ink
                            font.family: theme.display; font.pixelSize: 21
                            elide: Text.ElideRight
                        }
                        Row {
                            id: enterRow
                            anchors.right: parent.right; anchors.rightMargin: 22
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            opacity: bar.open ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 250 } }
                            Text { text: "Enter the universe"; color: theme.ink
                                   font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Text { text: "→"; color: theme.gold; font.pixelSize: 14 }
                        }
                    }

                    // ---- the breathe: the world introduces itself under its own title ----
                    Column {
                        anchors.left: parent.left; anchors.leftMargin: 68
                        anchors.right: parent.right; anchors.rightMargin: 24
                        anchors.top: parent.top; anchors.topMargin: pile.restH
                        spacing: 8
                        opacity: bar.open ? 1 : 0
                        visible: opacity > 0.01
                        Behavior on opacity { NumberAnimation { duration: 260 } }
                        // the kicker is derived from the pile's own order, not from the row —
                        // the blurb and the media ledger below it are GONE with the baked
                        // list, because the roster supplies neither and faking either would
                        // put words in a universe's mouth.
                        Text { text: "UNIVERSE  ·  " + ((bar.index + 1 < 10 ? "0" : "") + (bar.index + 1))
                               color: theme.gold; font.family: theme.ui
                               font.pixelSize: 10; font.letterSpacing: 3 }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: if (!root.walking && !hallNav.keyboardRecentlyMoved) root.hovered = bar.index
                    onExited: if (root.hovered === bar.index) root.hovered = -1
                    onClicked: {
                        stackWalk.currentIndex = bar.index
                        root.exploreRequested(bar.modelData.extensionId, bar.modelData.name)
                    }
                }
            }
        }
    }
        KeyboardCollectionController {
            id: hallNav
            view: stackWalk
            count: root.universes.length
            orientation: "vertical"
            positionIndexFn: function(index) {
                const item = hallRepeater.itemAt(index)
                if (!item) return
                const top = item.y
                const bottom = item.y + item.height
                const maxY = Math.max(0, stackWalk.contentHeight - stackWalk.height)
                if (top < stackWalk.contentY) stackWalk.contentY = Math.max(0, top)
                else if (bottom > stackWalk.contentY + stackWalk.height)
                    stackWalk.contentY = Math.min(maxY, bottom - stackWalk.height)
            }
            onActivated: (index) => {
                if (index >= 0 && index < root.universes.length) {
                    const u = root.universes[index]
                    root.exploreRequested(u.extensionId, u.name)
                }
            }
        }
    }   // stackWalk (the pile scrolls down — never sideways)
}
