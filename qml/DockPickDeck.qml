pragma ComponentBehavior: Bound
import QtQuick
import "PosterSourcePolicy.js" as PosterPolicy

FocusScope {
    id: deck
    required property string medium
    required property string mediumKey
    property var entries: []
    property bool logoArt: false
    property bool reducedMotion: false
    property real aspect: 0.71
    signal openRequested(var entry)
    signal verticalSwipeRequested(int direction)

    readonly property int count: entries.length
    readonly property bool canBrowse: count > 1
    readonly property real coverHeight: Math.max(1, Math.min(stage.height - 5, (stage.width - 32) / aspect))
    readonly property real coverWidth: coverHeight * aspect
    readonly property real travel: Math.max(0, Math.min(78, (stage.width - coverWidth) / 2 - 9))
    property int current: 0
    property string selectionKey: ""
    property int direction: 1
    property int moving: -1
    property bool behind: false
    property real shift: 0
    property real tilt: 0
    property real incoming: 0
    readonly property int duration: reducedMotion ? 0 : 155

    Theme { id: theme }

    function cardDepth(index) {
        return count ? ((index - current) * direction + count) % count : 0
    }
    function offset(layerNumber) { return Math.min(layerNumber, 2) * 6 }
    function identity(entry) { return entry ? String(entry.id || entry.title) : "" }
    function resetMotion() {
        turn.stop()
        settle.stop()
        moving = -1
        shift = 0
        tilt = 0
        incoming = 0
        behind = false
    }
    function advance(step) {
        if (!canBrowse || turn.running) return
        settle.stop()
        direction = step
        moving = current
        turn.start()
    }
    function openCurrent() {
        if (count && !turn.running) openRequested(entries[current])
    }

    onEntriesChanged: {
        resetMotion()
        const previous = selectionKey
        current = Math.max(0, entries.findIndex(entry => identity(entry) === previous))
        selectionKey = identity(entries[current])
    }
    onCurrentChanged: selectionKey = identity(entries[current])
    onVisibleChanged: if (!visible) resetMotion()
    onReducedMotionChanged: resetMotion()

    Text {
        id: mediumLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        text: deck.medium
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 12
    }

    Item {
        id: stage
        anchors.top: mediumLabel.bottom
        anchors.topMargin: 2
        anchors.bottom: caption.top
        anchors.left: parent.left
        anchors.right: parent.right
        activeFocusOnTab: deck.count > 0
        Accessible.role: Accessible.Button
        Accessible.name: deck.count ? qsTr("Open %1").arg(deck.entries[deck.current].title) : deck.medium
        Accessible.onPressAction: deck.openCurrent()
        Keys.onLeftPressed: deck.advance(-1)
        Keys.onRightPressed: deck.advance(1)
        Keys.onReturnPressed: deck.openCurrent()
        Keys.onEnterPressed: deck.openCurrent()
        Keys.onSpacePressed: deck.openCurrent()

        Repeater {
            model: deck.entries
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property int depth: deck.cardDepth(index)
                readonly property bool inMotion: index === deck.moving
                readonly property bool front: depth === 0
                width: deck.coverWidth
                height: deck.coverHeight
                x: (stage.width - width) / 2 + (inMotion ? deck.shift : front ? deck.incoming : deck.offset(depth))
                y: (stage.height - height) / 2 + (front ? 0 : Math.min(depth, 2))
                rotation: inMotion ? deck.tilt : front ? grab.hoverTilt : Math.min(depth, 2) * 2
                transform: Rotation {
                    origin.x: deck.coverWidth / 2
                    origin.y: deck.coverHeight / 2
                    axis { x: 1; y: 0; z: 0 }
                    angle: parent.front && !parent.inMotion ? grab.hoverPitch : 0
                }
                z: inMotion ? (deck.behind ? 0 : 20) : 10 - Math.min(depth, 5)
                visible: depth < 3 || inMotion
                radius: 4
                color: "#24272e"
                border.width: 1
                border.color: front && stage.activeFocus ? theme.gold : "#45ffffff"
                Accessible.ignored: true

                Loader {
                    id: posterLoader
                    anchors.fill: parent
                    anchors.margins: 1
                    active: parent.visible
                    readonly property var entry: parent.modelData
                    sourceComponent: deck.logoArt ? logoFace : posterFace
                }

                Component {
                    id: posterFace
                    RoundedPosterImage {
                        radius: 4
                        revealDuration: deck.reducedMotion ? 0 : 150
                        sources: PosterPolicy.candidates(posterLoader.entry.cover || "", posterLoader.entry.coverCandidates || [])
                    }
                }

                Component {
                    id: logoFace
                    Item {
                        Image {
                            objectName: "dockUniverseLogo"
                            anchors.fill: parent
                            anchors.margins: Math.min(parent.width, parent.height) * .12
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            source: posterLoader.entry.cover
                            sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
                        }
                    }
                }
            }
        }

        MouseArea {
            id: grab
            objectName: "dockPickCard_" + deck.mediumKey
            z: 25
            x: (stage.width - width) / 2
            y: (stage.height - height) / 2
            width: deck.coverWidth
            height: deck.coverHeight
            enabled: deck.count > 0 && !turn.running
            preventStealing: true
            cursorShape: deck.canBrowse ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
            property point start
            hoverEnabled: !deck.reducedMotion
            property int dragAxis: 0
            readonly property bool hoverActive: containsMouse && !pressed && deck.moving < 0 && !deck.reducedMotion
            property real hoverTilt: hoverActive ? (mouseX / Math.max(1, width) - .5) * 5 : 0
            property real hoverPitch: hoverActive ? (.5 - mouseY / Math.max(1, height)) * 8 : 0
            Behavior on hoverTilt {
                enabled: !grab.pressed
                NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
            Behavior on hoverPitch {
                enabled: !grab.pressed
                NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
            }

            onPressed: event => {
                deck.resetMotion()
                start = Qt.point(event.x, event.y)
                dragAxis = 0
            }
            onPositionChanged: event => {
                if (!pressed) return
                const dx = event.x - start.x
                const dy = event.y - start.y
                if (dragAxis === 0 && Math.max(Math.abs(dx), Math.abs(dy)) >= 6)
                    dragAxis = Math.abs(dx) >= Math.abs(dy) ? Qt.Horizontal : Qt.Vertical
                if (dragAxis !== Qt.Horizontal || !deck.canBrowse) return
                deck.moving = deck.current
                deck.direction = dx < 0 ? 1 : -1
                deck.shift = Math.max(-deck.travel, Math.min(deck.travel, dx))
                deck.tilt = deck.reducedMotion ? 0 : deck.shift / Math.max(1, deck.travel) * 7
            }
            onReleased: event => {
                const dx = event.x - start.x
                const dy = event.y - start.y
                if (dragAxis === 0 && Math.hypot(dx, dy) < 6) deck.openCurrent()
                else if (dragAxis === Qt.Horizontal && deck.canBrowse
                        && Math.abs(dx) >= Math.max(12, Math.min(26, deck.travel * .62)))
                    deck.advance(dx < 0 ? 1 : -1)
                else if (dragAxis === Qt.Vertical && Math.abs(dy) >= 48)
                    deck.verticalSwipeRequested(dy < 0 ? 1 : -1)
                else if (deck.moving >= 0) settle.start()
            }
            onCanceled: deck.resetMotion()
        }

    }

    Text {
        id: caption
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 17
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 11
        text: deck.count ? deck.entries[deck.current].title : ""
    }

    SequentialAnimation {
        id: settle
        ParallelAnimation {
            NumberAnimation { target: deck; property: "shift"; to: 0; duration: deck.duration; easing.type: Easing.OutCubic }
            NumberAnimation { target: deck; property: "tilt"; to: 0; duration: deck.duration; easing.type: Easing.OutCubic }
        }
        ScriptAction { script: deck.moving = -1 }
    }
    SequentialAnimation {
        id: turn
        ParallelAnimation {
            NumberAnimation { target: deck; property: "shift"; to: -deck.direction * deck.travel; duration: deck.duration; easing.type: Easing.OutCubic }
            NumberAnimation { target: deck; property: "tilt"; to: -deck.direction * 7; duration: deck.duration }
        }
        ScriptAction {
            script: {
                deck.behind = true
                deck.incoming = deck.offset(1)
                deck.current = (deck.current + deck.direction + deck.count) % deck.count
            }
        }
        ParallelAnimation {
            NumberAnimation { target: deck; property: "shift"; to: deck.offset(deck.count - 1); duration: deck.duration + (deck.reducedMotion ? 0 : 85); easing.type: Easing.OutCubic }
            NumberAnimation { target: deck; property: "tilt"; to: Math.min(deck.count - 1, 2) * 2; duration: deck.duration; easing.type: Easing.OutCubic }
            NumberAnimation { target: deck; property: "incoming"; to: 0; duration: deck.duration; easing.type: Easing.OutCubic }
        }
        ScriptAction {
            script: {
                deck.moving = -1
                deck.behind = false
                deck.shift = 0
                deck.tilt = 0
            }
        }
    }
}
