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
    property real headingExpansion: 0
    property real aspect: 0.71
    property real leftMotionRoom: 0
    signal openRequested(var entry)
    signal verticalSwipeRequested(real distance)

    readonly property int count: entries.length
    readonly property bool canBrowse: count > 1
    readonly property var activeEntry: count ? entries[current] : null
    readonly property string title: activeEntry ? String(activeEntry.title) : ""
    readonly property var facts: factsFor(activeEntry)
    readonly property string detailLine: facts.length > 1 ? qsTr("%1 · %2").arg(facts[0]).arg(facts[1])
                                                         : facts[0] || ""
    readonly property real turnAngle: 7
    readonly property real fanAngle: 1.5
    readonly property real turnScale: .84
    // The rotated card extends by at most half the perpendicular edge times sin(angle).
    readonly property real turnAllowance: Math.sin(turnAngle * Math.PI / 180) / 2
    readonly property real fanAllowance: Math.sin(2 * fanAngle * Math.PI / 180) / 2
    // Reuse the old caption budget for the taller heading, retaining one line
    // each for the medium and current title even in compact windows.
    readonly property real captionHeight: Math.min(height, Math.max(
        5 + mediumLabel.height + labels.spacing + titleMetrics.height,
        Math.min(132, (height + headingExpansion) * .37) - headingExpansion))
    readonly property real artHeight: Math.max(0, height - captionHeight)
    readonly property real coverHeight: Math.max(0, Math.min(
        (artHeight - 8) / (1 + 2 * aspect * fanAllowance),
        (width - offset(2) - 4) / (aspect + fanAllowance),
        (width - 4) / (aspect + 2 * fanAllowance)))
    readonly property real coverWidth: coverHeight * aspect
    readonly property int backDepth: Math.min(Math.max(count - 1, 0), 2)
    readonly property real backRotationOverhang: coverHeight * Math.sin(backDepth * fanAngle * Math.PI / 180) / 2
    readonly property real leftStackOverhang: Math.max(0, backRotationOverhang - offset(backDepth))
    readonly property real rightStackOverhang: offset(backDepth) + backRotationOverhang
    readonly property real coverLeft: (width - coverWidth + leftStackOverhang - rightStackOverhang) / 2
    readonly property real coverTop: (artHeight - coverHeight) / 2
    readonly property real turnOverhang: Math.max(0, coverHeight * turnAllowance - coverWidth * (1 - turnScale) / 2)
    readonly property real leftTravel: Math.max(0, Math.min(54, coverLeft + leftMotionRoom - turnOverhang - 1))
    readonly property real rightTravel: Math.max(0, Math.min(54, width - coverLeft - coverWidth
        + leftMotionRoom - turnOverhang - 1))
    property int current: 0
    property string selectionKey: ""
    property int direction: 1
    property int moving: -1
    property bool behind: false
    property real shift: 0
    property real tilt: 0
    property real incoming: 0
    readonly property int duration: reducedMotion ? 0 : 155
    z: moving >= 0 ? 1 : 0

    Theme { id: theme }

    function factsFor(entry) {
        if (!entry)
            return []
        const raw = entry.raw || {}
        const year = entry.year || ""
        let values
        switch (mediumKey) {
        case "anime":
        case "tv":
        case "movies":
            values = [entry.releaseInfo, Array.isArray(entry.genres) ? entry.genres[0] : entry.genres]
            break
        case "manga":
        case "manhwa":
            values = [raw.authors?.[0]?.name, year]
            break
        case "comics":
            values = [entry.publisher, year]
            break
        case "books":
            values = [entry.author, year]
            break
        case "audiobooks":
            values = [raw.author]
            break
        default:
            values = []
        }
        return values.map(value => String(value || "").trim()).filter(value => value.length > 0)
    }

    function cardDepth(index) {
        return count ? ((index - current) * direction + count) % count : 0
    }
    function offset(layerNumber) { return Math.min(layerNumber, 2) * 4 }
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
    function focusCover() {
        stage.forceActiveFocus(Qt.TabFocusReason)
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

    Item {
        id: stage
        objectName: "dockPickStage_" + deck.mediumKey
        anchors.fill: parent
        activeFocusOnTab: deck.count > 0
        Accessible.role: deck.count ? Accessible.Button : Accessible.StaticText
        Accessible.name: deck.count ? qsTr("Open %1 (%2)").arg(deck.title).arg(deck.medium) : deck.medium
        Accessible.description: deck.count ? deck.detailLine : qsTr("No picks available")
        Accessible.onPressAction: deck.openCurrent()
        Keys.onLeftPressed: deck.advance(-1)
        Keys.onRightPressed: deck.advance(1)
        Keys.onReturnPressed: deck.openCurrent()
        Keys.onEnterPressed: deck.openCurrent()
        Keys.onSpacePressed: deck.openCurrent()

        Repeater {
            model: deck.entries
            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                objectName: "dockPickFace_" + index
                readonly property int depth: deck.cardDepth(index)
                readonly property bool inMotion: index === deck.moving
                readonly property bool front: depth === 0
                width: deck.coverWidth
                height: deck.coverHeight
                x: deck.coverLeft + (inMotion ? deck.shift : front ? deck.incoming : deck.offset(depth))
                y: deck.coverTop + (front ? 0 : Math.min(depth, 2))
                rotation: inMotion ? deck.tilt : front ? grab.hoverTilt : Math.min(depth, 2) * deck.fanAngle
                scale: inMotion && !deck.reducedMotion
                    ? 1 - (1 - deck.turnScale) * Math.min(1,
                          Math.abs(deck.shift - (deck.behind ? deck.offset(depth) : 0))
                          / Math.max(1, deck.shift < 0 ? deck.leftTravel : deck.rightTravel))
                    : 1
                transform: Rotation {
                    origin.x: deck.coverWidth / 2
                    origin.y: deck.coverHeight / 2
                    axis { x: 1; y: 0; z: 0 }
                    angle: card.front && !card.inMotion ? grab.hoverPitch : 0
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
                        objectName: "dockPosterArt"
                        radius: 4
                        fitArtwork: true
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
                            source: posterLoader.entry.cover || ""
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
            x: deck.coverLeft
            y: deck.coverTop
            width: deck.coverWidth
            height: deck.coverHeight
            enabled: deck.count > 0 && !turn.running
            preventStealing: true
            cursorShape: deck.canBrowse ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
            property point start
            hoverEnabled: true
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
                start = grab.mapToItem(null, event.x, event.y)
                dragAxis = 0
            }
            onPositionChanged: event => {
                if (!pressed) return
                const scenePosition = grab.mapToItem(null, event.x, event.y)
                const dx = scenePosition.x - start.x
                const dy = scenePosition.y - start.y
                if (dragAxis === 0 && Math.max(Math.abs(dx), Math.abs(dy)) >= 6)
                    dragAxis = Math.abs(dx) >= Math.abs(dy) ? Qt.Horizontal : Qt.Vertical
                if (dragAxis !== Qt.Horizontal || !deck.canBrowse) return
                deck.moving = deck.current
                deck.direction = dx < 0 ? 1 : -1
                deck.shift = Math.max(-deck.leftTravel, Math.min(deck.rightTravel, dx))
                const travel = dx < 0 ? deck.leftTravel : deck.rightTravel
                deck.tilt = deck.reducedMotion ? 0 : deck.shift / Math.max(1, travel) * deck.turnAngle
            }
            onReleased: event => {
                const scenePosition = grab.mapToItem(null, event.x, event.y)
                const dx = scenePosition.x - start.x
                const dy = scenePosition.y - start.y
                if (dragAxis === 0 && Math.hypot(dx, dy) < 6) deck.openCurrent()
                else if (dragAxis === Qt.Horizontal && deck.canBrowse
                        && Math.abs(dx) >= Math.max(12, Math.min(26, (dx < 0 ? deck.leftTravel : deck.rightTravel) * .62)))
                    deck.advance(dx < 0 ? 1 : -1)
                else if (dragAxis === Qt.Vertical)
                    deck.verticalSwipeRequested(dy)
                else if (deck.moving >= 0) settle.start()
            }
            onCanceled: deck.resetMotion()
        }

    }

    Column {
        id: labels
        x: Math.min(5, deck.width * .03)
        y: deck.count ? deck.artHeight + 5 : Math.max(0, (deck.height - height) / 2)
        width: Math.max(0, deck.width - 2 * x)
        spacing: 4

        Text {
            id: mediumLabel
            objectName: "dockPickMedium_" + deck.mediumKey
            width: labels.width
            text: deck.medium
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
            Accessible.ignored: true
        }

        Text {
            id: titleLabel
            objectName: "dockPickTitle_" + deck.mediumKey
            width: labels.width
            text: deck.count ? deck.title : qsTr("No picks available")
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: {
                const room = deck.captionHeight - 5 - mediumLabel.height - labels.spacing
                    - (factLabel.visible ? factLabel.height + labels.spacing : 0)
                return Math.max(1, Math.min(3, 1 + Math.floor((room - titleMetrics.height) / Math.max(1, titleMetrics.lineSpacing))))
            }
            elide: Text.ElideRight
            color: deck.count ? theme.ink : theme.inkDim
            font.family: deck.count ? theme.display : theme.ui
            font.pixelSize: theme.fontSize(deck.count ? 20 : 13)
            Accessible.ignored: true
        }

        Text {
            id: factLabel
            objectName: "dockPickFacts_" + deck.mediumKey
            width: labels.width
            visible: text.length > 0 && deck.captionHeight >= 5 + mediumLabel.height + titleMetrics.height
                + height + 2 * labels.spacing
            text: deck.detailLine
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: theme.fontSize(14)
            Accessible.ignored: true
        }
        FontMetrics { id: titleMetrics; font: titleLabel.font }
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
            NumberAnimation { target: deck; property: "shift"; to: deck.direction > 0 ? -deck.leftTravel : deck.rightTravel; duration: deck.duration; easing.type: Easing.OutCubic }
            NumberAnimation { target: deck; property: "tilt"; to: -deck.direction * deck.turnAngle; duration: deck.duration }
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
            NumberAnimation { target: deck; property: "tilt"; to: Math.min(deck.count - 1, 2) * deck.fanAngle; duration: deck.duration; easing.type: Easing.OutCubic }
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
