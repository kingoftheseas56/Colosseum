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
    property int artRadius: -1
    signal openRequested(var entry)
    signal verticalSwipeRequested(real distance)

    readonly property int count: entries.length
    readonly property bool canBrowse: count > 1
    readonly property var activeEntry: count ? entries[current] : null
    readonly property string title: activeEntry ? String(activeEntry.title) : ""
    readonly property var facts: factsFor(activeEntry)
    readonly property string detailLine: facts.length > 1 ? qsTr("%1 · %2").arg(facts[0]).arg(facts[1])
                                                         : facts[0] || ""
    readonly property real turnAngle: 4
    readonly property real fanAngle: 1.5
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
    // At the depth change the traveling cover occupies the left lane. The
    // other covers form a stack in the right lane, with room for its fan.
    readonly property real singleLane: width * .24 - coverWidth / 2
    readonly property real stackLane: width * .74 - coverWidth / 2
    readonly property real crossingWidth: Math.max(0, width * .43 - coverHeight * Math.sin(turnAngle * Math.PI / 180))
    readonly property real crossingStretch: Math.min(1, crossingWidth / Math.max(1, coverWidth))
    readonly property real leftTravel: Math.max(0, coverLeft - singleLane)
    readonly property real rightTravel: Math.max(0, stackLane - coverLeft)
    property int current: 0
    property string selectionKey: ""
    property int direction: 1
    property int circulating: -1
    property real progress: 0
    readonly property bool turning: circulating >= 0
    // Forward and reverse use the same poses. Zero puts the circulating
    // cover on top, and one tucks it all the way behind the rest.
    readonly property real circulation: direction > 0 ? progress : 1 - progress
    readonly property real spread: 1 - Math.abs(2 * circulation - 1)
    readonly property int duration: reducedMotion ? 0 : 155
    property int revealTime: 0
    property var warmedEntries: null
    z: turning ? 1 : 0

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
        return count ? (index - current + count) % count : 0
    }
    function offset(layerNumber) { return Math.min(layerNumber, 2) * 6 }
    function between(before, after, amount) {
        return before + (after - before) * amount
    }
    function across(before, edge, after) {
        return circulation <= .5
            ? between(before, edge, spread)
            : between(after, edge, spread)
    }
    function prepare(step) {
        direction = step
        circulating = step > 0 ? current : (current + count - 1) % count
    }
    function pull(dx) {
        const step = dx < 0 ? 1 : -1
        if (!turning || step !== direction) {
            progress = 0
            prepare(step)
        }
        const travel = step > 0 ? leftTravel : rightTravel
        progress = Math.min(.5, Math.abs(dx) / (2 * Math.max(1, travel)))
    }
    function identity(entry) { return entry ? String(entry.id || entry.title) : "" }
    function selectEntry(key) {
        const index = entries.findIndex(entry => identity(entry) === key)
        if (index >= 0) current = index
    }
    function resetMotion() {
        turn.stop()
        settle.stop()
        circulating = -1
        progress = 0
    }
    function advance(step) {
        if (!canBrowse || turn.running || settle.running) return
        if (!turning || step !== direction) prepare(step)
        // Continue from the last pointer pose. A full pull is already at the edge.
        revealTime = Math.round(duration * (1 - 2 * progress))
        turn.start()
    }
    function openCurrent() {
        if (count && !turning) openRequested(entries[current])
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

    // A late catalogue response can populate several decks together. Submit
    // their front covers before adding the backing cards to the download queue.
    Timer {
        interval: 64
        running: deck.artRadius > 0 && deck.count > 1 && deck.warmedEntries !== deck.entries
        onTriggered: deck.warmedEntries = deck.entries
    }

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
                readonly property bool front: depth === 0
                readonly property int nearbyDepth: Math.min(depth, deck.count - depth)
                readonly property bool solo: index === deck.circulating
                readonly property int stackDepth: deck.count > 0
                    ? (index - deck.circulating - 1 + deck.count) % deck.count : 0
                readonly property int topDepth: solo ? 0 : Math.min(2, stackDepth + 1)
                readonly property int bottomDepth: solo ? deck.backDepth : Math.min(2, stackDepth)
                readonly property bool posing: deck.turning && !deck.reducedMotion
                width: deck.coverWidth
                height: deck.coverHeight
                x: posing ? deck.across(deck.coverLeft + deck.offset(topDepth),
                    solo ? deck.singleLane : deck.stackLane + deck.offset(stackDepth) * .32,
                    deck.coverLeft + deck.offset(bottomDepth))
                    : deck.coverLeft + deck.offset(depth)
                y: deck.coverTop + (posing ? deck.between(topDepth, bottomDepth, deck.circulation)
                    : Math.min(depth, 2))
                rotation: posing ? deck.across(topDepth * deck.fanAngle, solo ? -deck.turnAngle : deck.turnAngle,
                    bottomDepth * deck.fanAngle) : front ? grab.hoverTilt : Math.min(depth, 2) * deck.fanAngle
                transform: [
                    Scale {
                        origin.x: deck.coverWidth / 2
                        origin.y: deck.coverHeight / 2
                        xScale: card.posing ? deck.between(1, deck.crossingStretch, deck.spread) : 1
                    },
                    Rotation {
                        origin.x: deck.coverWidth / 2
                        origin.y: deck.coverHeight / 2
                        axis { x: 1; y: 0; z: 0 }
                        angle: card.front && !deck.turning ? grab.hoverPitch : 0
                    }
                ]
                // Their projected outlines are disjoint at the crossover,
                // so changing order cannot cover or uncover part of the solo card.
                z: posing ? (solo ? (deck.circulation <= .5 ? 20 : 0) : 10 - Math.min(stackDepth, 5))
                    : 10 - Math.min(depth, 5)
                visible: posing ? (solo || stackDepth < 3) : depth < 3
                radius: 4
                color: "#24272e"
                border.width: 1
                border.color: front && stage.activeFocus ? theme.gold : "#45ffffff"
                Accessible.ignored: true

                Loader {
                    id: posterLoader
                    anchors.fill: parent
                    anchors.margins: 1
                    active: (card.posing && card.visible)
                        || (card.front && deck.artRadius >= 0)
                        || (deck.warmedEntries === deck.entries && card.nearbyDepth <= deck.artRadius)
                    readonly property var entry: parent.modelData
                    sourceComponent: deck.logoArt ? logoFace : posterFace
                }

                Component {
                    id: posterFace
                    RoundedPosterImage {
                        objectName: "dockPosterArt"
                        radius: 4
                        fitArtwork: deck.mediumKey !== "tributes"
                        revealDuration: deck.turning || deck.reducedMotion || !card.visible ? 0 : 150
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
            enabled: deck.count > 0 && !turn.running && !settle.running
            preventStealing: true
            cursorShape: deck.canBrowse ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
            property point start
            hoverEnabled: true
            property int dragAxis: 0
            readonly property bool hoverActive: containsMouse && !pressed && !deck.turning && !deck.reducedMotion
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
                deck.pull(dx)
            }
            onReleased: event => {
                const scenePosition = grab.mapToItem(null, event.x, event.y)
                const dx = scenePosition.x - start.x
                const dy = scenePosition.y - start.y
                if (dragAxis === 0 && Math.hypot(dx, dy) < 6) deck.openCurrent()
                else if (dragAxis === Qt.Horizontal && deck.turning) {
                    const travel = deck.direction > 0 ? deck.leftTravel : deck.rightTravel
                    if (2 * deck.progress * travel >= Math.max(12, travel * .62))
                        deck.advance(deck.direction)
                    else settle.start()
                }
                else if (dragAxis === Qt.Vertical)
                    deck.verticalSwipeRequested(dy)
                else if (deck.turning) settle.start()
            }
            onCanceled: if (deck.turning && !turn.running) settle.start()
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
        NumberAnimation { target: deck; property: "progress"; to: 0; duration: deck.duration; easing.type: Easing.OutCubic }
        ScriptAction { script: deck.circulating = -1 }
    }
    SequentialAnimation {
        id: turn
        NumberAnimation { target: deck; property: "progress"; to: .5; duration: deck.revealTime; easing.type: Easing.OutCubic }
        ScriptAction { script: deck.current = (deck.current + deck.direction + deck.count) % deck.count }
        NumberAnimation {
            target: deck
            property: "progress"
            to: 1
            duration: deck.reducedMotion ? 0 : 240
            easing.type: Easing.InOutSine
        }
        ScriptAction { script: { deck.circulating = -1; deck.progress = 0 } }
    }
}
