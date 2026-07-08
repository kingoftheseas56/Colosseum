// BrowserDrawer — Feature 8: the in-player episode/source browser (spec 2026-07-08,
// mock-ratified: side drawer, season pills, auto-play best source).
// Self-contained like ShortcutsSheet: props in, signals out, its own Theme. The video
// keeps playing beside it. All derivations live in EpisodeBrowser.js (harness-tested);
// this file only renders and forwards taps.
import QtQuick
import "EpisodeBrowser.js" as EpisodeBrowser
import "TheatreApi.js" as TheatreApi
import "Magnet.js" as Magnet

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

    Theme { id: theme }

    readonly property bool hasEpisodes: subType === "series" && (queue.length > 0 || nowId.split(":").length >= 3)
    property string tab: "episodes"

    // ---- season metadata (ONE fetch per playback session, lazy on first Episodes view) ----
    property var videos: []
    property var seasonList: []
    property int activeSeason: -1
    property string metaState: "idle"     // idle | loading | ready | error

    onOpenChanged: {
        if (!open)
            return
        drawer.tab = hasEpisodes ? "episodes" : "sources"
        if (drawer.tab === "episodes")
            drawer.ensureMeta()
    }
    onTabChanged: if (open && tab === "episodes") drawer.ensureMeta()

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

            // tabs
            Row {
                width: parent.width
                spacing: 8
                Repeater {
                    model: drawer.hasEpisodes ? ["episodes", "sources"] : ["sources"]
                    delegate: Rectangle {
                        id: tabPill
                        required property string modelData
                        width: (header.width - 8) / (drawer.hasEpisodes ? 2 : 1)
                        height: 34
                        radius: 6
                        color: drawer.tab === tabPill.modelData ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.12)
                                                                : Qt.rgba(1, 1, 1, 0.05)
                        Text {
                            anchors.centerIn: parent
                            text: tabPill.modelData === "episodes" ? "EPISODES" : "SOURCES"
                            color: drawer.tab === tabPill.modelData ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: drawer.tab = tabPill.modelData }
                    }
                }
            }

            // season pills (episodes tab, ready state)
            Flow {
                width: parent.width
                spacing: 6
                visible: drawer.tab === "episodes" && drawer.metaState === "ready" && drawer.seasonList.length > 1
                Repeater {
                    model: drawer.seasonList
                    delegate: Rectangle {
                        id: seasonPill
                        required property int modelData
                        width: pillText.implicitWidth + 20
                        height: 24
                        radius: 12
                        color: "transparent"
                        border.width: 1
                        border.color: drawer.activeSeason === seasonPill.modelData
                                      ? Qt.rgba(212 / 255, 175 / 255, 55 / 255, 0.5) : theme.edge
                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: seasonPill.modelData === 0 ? "Specials" : "S" + seasonPill.modelData
                            color: drawer.activeSeason === seasonPill.modelData ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: drawer.activeSeason = seasonPill.modelData }
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
                }
            }
        }

        // ---- EPISODES list ----
        ListView {
            visible: drawer.tab === "episodes"
            anchors.top: header.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: drawer.episodeRows
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

        // ---- SOURCES list ----
        ListView {
            visible: drawer.tab === "sources"
            anchors.top: header.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: drawer.candidates
            delegate: Item {
                id: srcRow
                required property var modelData
                required property int index
                property bool copiedTick: false
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
                            var link = Magnet.linkFor({ "infoHash": srcRow.modelData.infoHash,
                                                        "filename": srcRow.modelData.title })
                            if (!link.length)
                                return
                            Clipboard.copy(link)
                            srcRow.copiedTick = true
                            srcTickTimer.restart()
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
    }
}
