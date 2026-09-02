// BrowserDrawer — Feature 8: the in-player episode/source browser (spec 2026-07-08,
// mock-ratified: side drawer, season pills, auto-play best source).
// Self-contained like ShortcutsSheet: props in, signals out, its own Theme. The video
// keeps playing beside it. All derivations live in EpisodeBrowser.js (harness-tested);
// this file only renders and forwards taps.
import QtQuick
import QtQuick.Controls
import "EpisodeBrowser.js" as EpisodeBrowser
import "TheatreApi.js" as TheatreApi
import "Magnet.js" as Magnet
import "PlayerFocusContainment.js" as FocusContainment

Item {
    id: drawer
    anchors.fill: parent
    visible: open
    z: 55   // above chrome, below the ? shortcuts sheet (60)

    property bool open: false
    // inputs from the player
    property var queue: []                // traveling episodeQueue (instant floor)
    property int queueIndex: -1
    property string nowId: ""             // root.mediaId
    property string mediaTitle: ""
    property string mediaYear: ""
    property string backdropUrl: ""
    property string subType: "series"
    property var candidates: []           // root.streamCandidates (normalized)
    property int currentStreamIndex: -1
    property var isDead: function(i) { return false }   // root.isStreamDead

    signal episodePicked(var target)
    signal sourcePicked(int index)
    signal dismissed()

    focusPolicy: open ? Qt.TabFocus : Qt.NoFocus
    property var focusReturnItem: null
    function movePanelFocus(forward) {
        var w = drawer.Window.window
        var item = w ? w.activeFocusItem : null
        if (!item || !item.nextItemInFocusChain) return false
        var next = item.nextItemInFocusChain(forward)
        if (!next || next === item) return false
        next.forceActiveFocus(Qt.TabFocusReason)
        return true
    }
    function copySource(index) {
        if (index < 0 || index >= drawer.candidates.length) return false
        var row = drawer.candidates[index] || ({})
        var link = Magnet.linkFor({ "infoHash": row.infoHash, "filename": row.title })
        if (!link.length) return false
        Clipboard.copy(link)
        if (sourceList.currentItem && sourceList.currentItem.showCopied) sourceList.currentItem.showCopied()
        return true
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { drawer.dismissed(); event.accepted = true }
        else if (event.key === Qt.Key_Down && !episodeList.activeFocus && !sourceList.activeFocus) event.accepted = drawer.movePanelFocus(true)
        else if (event.key === Qt.Key_Up && !episodeList.activeFocus && !sourceList.activeFocus) event.accepted = drawer.movePanelFocus(false)
    }
    Keys.onTabPressed: function(event) { event.accepted = FocusContainment.move(drawer.Window.window, panel, true) }
    Keys.onBacktabPressed: function(event) { event.accepted = FocusContainment.move(drawer.Window.window, panel, false) }

    Theme { id: theme }

    readonly property bool hasEpisodes: subType === "series" && (queue.length > 0 || nowId.split(":").length >= 3)
    property string tab: "episodes"

    // ---- season metadata (ONE fetch per playback session, lazy on first Episodes view) ----
    property var videos: []
    property var seasonList: []
    property int activeSeason: -1
    property string metaState: "idle"     // idle | loading | ready | error
    property string loadedRootId: ""      // the series the cached meta belongs to (cache key)

    onOpenChanged: {
        if (!open) {
            if (drawer.focusReturnItem) {
                var target = drawer.focusReturnItem; drawer.focusReturnItem = null
                Qt.callLater(function() { if (target && target.visible && target.enabled && target.forceActiveFocus) target.forceActiveFocus(Qt.TabFocusReason) })
            }
            return
        }
        var w = drawer.Window.window; drawer.focusReturnItem = w ? w.activeFocusItem : null
        drawer.tab = hasEpisodes ? "episodes" : "sources"
        if (drawer.tab === "episodes") drawer.ensureMeta()
        Qt.callLater(function() { tabStrip.forceActiveFocus(Qt.PopupFocusReason) })
    }
    onTabChanged: if (open && tab === "episodes") drawer.ensureMeta()

    // A NEW series invalidates the cached season metadata. This drawer instance persists
    // across shows and the fetch is "once per series", so without this a new show keeps the
    // previous one's seasons + episode list (One Piece's 23 seasons over Batman's 2). A same-
    // series episode change keeps the cache (root id unchanged) so browsing state survives.
    onNowIdChanged: {
        if (EpisodeBrowser.seriesRootId(nowId) === drawer.loadedRootId)
            return
        drawer.metaState = "idle"
        drawer.videos = []
        drawer.seasonList = []
        drawer.activeSeason = -1
        drawer.loadedRootId = ""
        if (drawer.open && drawer.tab === "episodes")
            drawer.ensureMeta()
    }

    function ensureMeta() {
        if (metaState === "loading" || metaState === "ready")
            return
        var rootId = EpisodeBrowser.seriesRootId(nowId)
        if (!rootId.length || rootId === nowId) {   // no series shape in the id
            metaState = "error"
            return
        }
        metaState = "loading"
        if (typeof Extensions !== "undefined")
            TheatreApi.setExtensions(Extensions.installed())
        TheatreApi.loadMeta("series", rootId, function(meta) {
            var vids = (meta && meta.videos && meta.videos.length) ? meta.videos : []
            if (vids.length) {
                drawer.videos = vids
                drawer.seasonList = EpisodeBrowser.seasonsFrom(vids)
                var nowSeason = EpisodeBrowser.seasonOf(drawer.nowId)
                drawer.activeSeason = (nowSeason >= 0 && drawer.seasonList.indexOf(nowSeason) >= 0)
                                      ? nowSeason
                                      : (drawer.seasonList.length ? drawer.seasonList[0] : -1)
                drawer.metaState = "ready"
                drawer.loadedRootId = rootId
            } else {
                drawer.metaState = "error"
            }
        })
    }

    // rows for the list: fetched season when ready, else the traveling floor
    readonly property var episodeRows: {
        if (metaState === "ready" && activeSeason >= 0)
            return EpisodeBrowser.episodesFor(videos, activeSeason, EpisodeBrowser.seriesRootId(nowId))
        return EpisodeBrowser.floorRows(queue)
    }

    // Progress store revision poke so watched/progress states re-evaluate live.
    readonly property int progressRev: (typeof Progress !== "undefined") ? Progress.revision : 0

    function pickEpisode(row) {
        if (row.id === drawer.nowId) { drawer.dismissed(); return }
        var q, idx
        if (drawer.metaState === "ready" && drawer.activeSeason >= 0) {
            q = EpisodeBrowser.queueFrom(drawer.episodeRows,
                                         EpisodeBrowser.showTitleFrom(drawer.mediaTitle),
                                         drawer.backdropUrl)
            idx = row.queueIdx
        } else {
            q = drawer.queue
            idx = row.queueIdx
        }
        var target = Object.assign({}, q[idx])
        target.context = { "year": drawer.mediaYear, "episodeQueue": q, "episodeIndex": idx }
        drawer.episodePicked(target)
        drawer.dismissed()
    }

    // ---- tap-outside dismisses (the strip left of the panel) ----
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: panel.left
        onClicked: drawer.dismissed()
    }

    Rectangle {
        id: panel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(380, Math.round(parent.width * 0.32))
        color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        // slide-in (house motion): bind the offset to open-state, animate the transition.
        transform: Translate {
            id: slide
            x: drawer.open ? 0 : panel.width
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }

        // click-swallower: taps on the panel body must never fall through to the video
        MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.ArrowCursor }

        Column {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            spacing: 12

            // tabs â€” one composite Tab stop; arrows move inside the strip.
            Row {
                id: tabStrip
                readonly property var options: drawer.hasEpisodes ? ["episodes", "sources"] : ["sources"]
                property int keyboardIndex: Math.max(0, options.indexOf(drawer.tab))
                width: parent.width
                spacing: 8
                focusPolicy: drawer.open ? Qt.TabFocus : Qt.NoFocus
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        var delta = event.key === Qt.Key_Left ? -1 : 1
                        var next = keyboardIndex + delta
                        if (next >= 0 && next < options.length) { keyboardIndex = next; event.accepted = true }
                    } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
                    else if (event.key === Qt.Key_End) { keyboardIndex = options.length - 1; event.accepted = true }
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { drawer.tab = options[keyboardIndex]; event.accepted = true }
                }
                Repeater {
                    model: tabStrip.options
                    delegate: Rectangle {
                        id: tabPill
                        required property int index
                        required property string modelData
                        width: (header.width - 8) / (drawer.hasEpisodes ? 2 : 1)
                        height: 34
                        radius: 6
                        color: drawer.tab === tabPill.modelData ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.12) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: tabStrip.activeFocus && tabStrip.keyboardIndex === tabPill.index ? 2 : 0
                        border.color: theme.gold
                        Text {
                            anchors.centerIn: parent
                            text: tabPill.modelData === "episodes" ? "EPISODES" : "SOURCES"
                            color: drawer.tab === tabPill.modelData ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: drawer.tab = tabPill.modelData }
                    }
                }
            }


            // season pills â€” one composite Tab stop when multiple seasons are present.
            Flow {
                id: seasonStrip
                property int keyboardIndex: Math.max(0, drawer.seasonList.indexOf(drawer.activeSeason))
                width: parent.width
                spacing: 6
                visible: drawer.tab === "episodes" && drawer.metaState === "ready" && drawer.seasonList.length > 1
                focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        var next = keyboardIndex + (event.key === Qt.Key_Left ? -1 : 1)
                        if (next >= 0 && next < drawer.seasonList.length) { keyboardIndex = next; event.accepted = true }
                    } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
                    else if (event.key === Qt.Key_End) { keyboardIndex = drawer.seasonList.length - 1; event.accepted = true }
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { drawer.activeSeason = drawer.seasonList[keyboardIndex]; event.accepted = true }
                }
                Repeater {
                    model: drawer.seasonList
                    delegate: Rectangle {
                        id: seasonPill
                        required property int index
                        required property int modelData
                        width: pillText.implicitWidth + 20
                        height: 24
                        radius: 12
                        color: "transparent"
                        border.width: seasonStrip.activeFocus && seasonStrip.keyboardIndex === seasonPill.index ? 2 : 1
                        border.color: (seasonStrip.activeFocus && seasonStrip.keyboardIndex === seasonPill.index) || drawer.activeSeason === seasonPill.modelData ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.7) : theme.edge
                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: seasonPill.modelData === 0 ? "Specials" : "S" + seasonPill.modelData
                            color: drawer.activeSeason === seasonPill.modelData ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: drawer.activeSeason = seasonPill.modelData }
                    }
                }
            }


            // honest fetch states
            Text {
                visible: drawer.tab === "episodes" && drawer.metaState === "loading"
                text: "Loading seasons…"
                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
            }
            Row {
                visible: drawer.tab === "episodes" && drawer.metaState === "error" && drawer.queue.length > 0
                spacing: 8
                Text {
                    text: "Couldn't load other seasons."
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                }
                Text {
                    text: "Retry"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { drawer.metaState = "idle"; drawer.ensureMeta() } }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Retry seasons"; onTriggered: { drawer.metaState = "idle"; drawer.ensureMeta() } }
                }
            }
        }

        // ---- EPISODES list ----
        ListView {
            id: episodeList
            visible: drawer.tab === "episodes"
            anchors.top: header.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: episodeList }
            model: drawer.episodeRows
            focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) { episodeKeyboard.handle(event) }
            delegate: Item {
                id: epRow
                required property var modelData
                width: ListView.view.width
                height: 46
                readonly property var st: {
                    var _poke = drawer.progressRev
                    return EpisodeBrowser.rowState(
                        (typeof Progress !== "undefined") ? Progress.get("video", epRow.modelData.id) : ({}),
                        epRow.modelData.id, drawer.nowId)
                }
                Rectangle {
                    anchors.fill: parent
                    color: epRow.st.state === "now" ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.07)
                         : epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                }
                Rectangle {
                    visible: epRow.st.state === "now"
                    anchors.left: parent.left; width: 2
                    anchors.top: parent.top; anchors.bottom: parent.bottom
                    color: theme.gold
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 12
                    spacing: 10
                    Text {
                        width: 30
                        anchors.verticalCenter: parent.verticalCenter
                        text: "E" + epRow.modelData.num
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    }
                    Text {
                        width: parent.width - 30 - 56 - 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: epRow.modelData.title
                        elide: Text.ElideRight
                        color: epRow.st.state === "now" ? "#f0e6c8"
                             : epRow.st.state === "watched" ? theme.inkDimmer : theme.ink
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    Rectangle {
                        width: 46; height: 3; radius: 2
                        anchors.verticalCenter: parent.verticalCenter
                        color: Qt.rgba(1, 1, 1, 0.10)
                        Rectangle {
                            width: parent.width * epRow.st.frac
                            anchors.left: parent.left
                            anchors.top: parent.top; anchors.bottom: parent.bottom
                            radius: 2; color: theme.gold
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(1, 1, 1, 0.05) }
                MouseArea {
                    id: epMa
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: drawer.pickEpisode(epRow.modelData)
                }
            }
        }

        KeyboardCollectionController {
            id: episodeKeyboard
            view: episodeList
            orientation: "vertical"
            count: episodeList.count
            onActivated: function(index) { if (index >= 0 && index < drawer.episodeRows.length) drawer.pickEpisode(drawer.episodeRows[index]) }
        }

        // ---- SOURCES list ----
        ListView {
            id: sourceList
            visible: drawer.tab === "sources"
            anchors.top: header.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: sourceList }
            model: drawer.candidates
            currentIndex: drawer.currentStreamIndex >= 0 ? drawer.currentStreamIndex : (count > 0 ? 0 : -1)
            focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) {
                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) { event.accepted = drawer.copySource(sourceList.currentIndex); return }
                sourceKeyboard.handle(event)
            }
            delegate: Item {
                id: srcRow
                required property var modelData
                required property int index
                property bool copiedTick: false
                function showCopied() { copiedTick = true; srcTickTimer.restart() }
                width: ListView.view.width
                height: 64
                readonly property string st: EpisodeBrowser.sourceRowState(
                    srcRow.index, drawer.currentStreamIndex, drawer.isDead(srcRow.index))
                Rectangle {
                    anchors.fill: parent
                    color: srcRow.st === "now" ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.07)
                         : srcMa.containsMouse && srcRow.st === "playable" ? Qt.rgba(1, 1, 1, 0.05)
                         : "transparent"
                }
                Rectangle {
                    visible: srcRow.st === "now"
                    anchors.left: parent.left; width: 2
                    anchors.top: parent.top; anchors.bottom: parent.bottom
                    color: theme.gold
                }
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.right: copySrc.left; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Row {
                        spacing: 8
                        Text {
                            text: srcRow.modelData.sourceName || "Source"
                            color: srcRow.st === "dead" ? theme.inkDimmer : theme.ink
                            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        Text {
                            text: srcRow.modelData.quality || ""
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 11
                        }
                        Text {
                            visible: srcRow.st === "now"
                            text: "PLAYING"
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1
                        }
                        Text {
                            visible: srcRow.st === "dead"
                            text: "didn't start"
                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 10
                        }
                    }
                    Text {
                        width: parent.width
                        text: (srcRow.modelData.title || "") +
                              (Number(srcRow.modelData.seeders) > 0 ? "  ·  " + srcRow.modelData.seeders + " seeders" : "")
                        elide: Text.ElideRight
                        color: srcRow.st === "dead" ? theme.inkDimmer : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 11
                    }
                }
                // copy link — same affordance the sources sheet ships (2026-07-08)
                Rectangle {
                    id: copySrc
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32; height: 32; radius: 16
                    color: copySrcMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                    border.width: 1; border.color: theme.edge
                    Text {
                        anchors.centerIn: parent
                        text: srcRow.copiedTick ? "✓" : "⧉"
                        color: srcRow.copiedTick ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                    }
                    MouseArea {
                        id: copySrcMa
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!drawer.copySource(srcRow.index)) return
                            srcRow.showCopied()
                        }
                    }
                }
                Timer { id: srcTickTimer; interval: 1200; onTriggered: srcRow.copiedTick = false }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(1, 1, 1, 0.05) }
                MouseArea {
                    id: srcMa
                    z: -1   // under the copy button
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: srcRow.st === "playable" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (srcRow.st === "playable") drawer.sourcePicked(srcRow.index)
                }
            }
            KeyboardCollectionController {
                id: sourceKeyboard
                view: sourceList
                orientation: "vertical"
                count: sourceList.count
                onActivated: function(index) {
                    if (index < 0 || index >= drawer.candidates.length) return
                    if (EpisodeBrowser.sourceRowState(index, drawer.currentStreamIndex, drawer.isDead(index)) === "playable") drawer.sourcePicked(index)
                }
            }
            footer: Text {
                visible: drawer.candidates.length === 0
                width: ListView.view ? ListView.view.width : 0
                horizontalAlignment: Text.AlignHCenter
                topPadding: 30
                text: "This is a downloaded file — no live sources."
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 12
            }
        }

        ScrollGlide { flick: episodeList }
        ScrollGlide { flick: sourceList }
    }
}
