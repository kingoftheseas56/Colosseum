import QtQuick
import "../.."
import "Player2Browser.js" as Browser
import "../../PlayerFocusContainment.js" as FocusContainment

// SourceDrawer — Feature 8's in-player browser: a glass side panel that slides in over the playing
// video (which keeps playing beside it) with two tabs, Episodes and Sources. This is the ORCHESTRATING
// container: it asks `hostServices` (typed requests), renders whatever it resolves, and emits typed
// intent up to the shell. It never fetches/ranks/persists itself — that is the whole point of the
// Player 2 seam. Episodes render inside EpisodeBrowser; sources render here. Structured identity
// (root id, current episode, season) is provided by the shell, so nothing here parses ids.
Item {
    id: drawer
    anchors.fill: parent
    visible: drawer.open || slide.x < panel.width - 0.5
    z: 55   // above the transport chrome

    property QtObject theme
    property var hostServices
    property bool open: false

    // structured identity from the shell (production wires from its playbackContext; the lab sets it)
    property string rootMediaId: ""
    property string currentEpisodeId: ""
    property string mediaTitle: ""
    property bool isSeries: true
    property int activeSeason: 1

    // host-resolved state
    property int seasonCount: 0
    property var episodes: []
    property var sources: []
    property string episodesState: "idle"   // idle | loading | ready
    property string sourcesState: "idle"

    property string tab: "episodes"
    readonly property bool hasEpisodes: drawer.isSeries

    signal episodePicked(string episodeId)
    signal sourcePicked(int index, string sourceId)
    signal dismissed()

    focusPolicy: drawer.open ? Qt.TabFocus : Qt.NoFocus
    function moveFocus(forward) {
        var w = drawer.Window.window; var item = w ? w.activeFocusItem : null
        if (!item || !item.nextItemInFocusChain) return false
        var next = item.nextItemInFocusChain(forward); if (!next || next === item) return false
        next.forceActiveFocus(Qt.TabFocusReason); return true
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { drawer.dismissed(); event.accepted = true }
        else if ((event.key === Qt.Key_Down || event.key === Qt.Key_Up) && !sourceList.activeFocus) event.accepted = drawer.moveFocus(event.key === Qt.Key_Down)
    }
    Keys.onTabPressed: function(event) { event.accepted = FocusContainment.move(drawer.Window.window, panel, true) }
    Keys.onBacktabPressed: function(event) { event.accepted = FocusContainment.move(drawer.Window.window, panel, false) }

    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"
    readonly property color panelColor: theme ? theme.panel : Qt.rgba(0.04, 0.05, 0.07, 0.94)
    readonly property color edge: theme ? theme.edge : Qt.rgba(1, 1, 1, 0.18)
    readonly property color goldTint: Qt.rgba(0.94, 0.77, 0.29, 0.12)

    // ---- host wiring: request on demand, render as it resolves (ignore stale/other-media answers) ----
    Connections {
        target: drawer.hostServices
        ignoreUnknownSignals: true
        function onMetadataResolved(mediaId, meta) {
            if (mediaId !== drawer.rootMediaId) return
            drawer.seasonCount = Number(meta.seasons) || 0
        }
        function onSeasonEpisodesResolved(mediaId, season, eps) {
            if (mediaId !== drawer.rootMediaId || season !== drawer.activeSeason) return
            drawer.episodes = eps
            drawer.episodesState = "ready"
        }
        function onAlternateSourcesResolved(mediaId, srcs) {
            if (mediaId !== drawer.rootMediaId) return
            drawer.sources = srcs
            drawer.sourcesState = "ready"
        }
    }

    function ensureEpisodes() {
        if (!drawer.hostServices || !drawer.rootMediaId.length || drawer.episodesState !== "idle")
            return
        drawer.episodesState = "loading"
        drawer.hostServices.requestMetadata(drawer.rootMediaId)
        drawer.hostServices.requestSeasonEpisodes(drawer.rootMediaId, drawer.activeSeason)
    }
    function ensureSources() {
        if (!drawer.hostServices || !drawer.rootMediaId.length || drawer.sourcesState !== "idle")
            return
        drawer.sourcesState = "loading"
        drawer.hostServices.requestAlternateSources(drawer.rootMediaId)
    }
    function selectSeason(season) {
        if (season === drawer.activeSeason && drawer.episodesState === "ready")
            return
        drawer.activeSeason = season
        drawer.episodes = []
        drawer.episodesState = "loading"
        drawer.hostServices.requestSeasonEpisodes(drawer.rootMediaId, season)
    }

    onOpenChanged: {
        if (!drawer.open)
            return
        drawer.tab = drawer.hasEpisodes ? "episodes" : "sources"
        if (drawer.tab === "episodes")
            ensureEpisodes()
        else
            ensureSources()
        Qt.callLater(function() { tabStrip.forceActiveFocus(Qt.PopupFocusReason) })
    }
    onTabChanged: {
        if (!drawer.open)
            return
        if (drawer.tab === "episodes")
            ensureEpisodes()
        else
            ensureSources()
    }

    // ---- tap-outside (the strip left of the panel) dismisses ----
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
        color: drawer.panelColor
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        // house motion: slide in from the right edge, animate the transition
        transform: Translate {
            id: slide
            x: drawer.open ? 0 : panel.width
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }

        // click-swallower: taps on the panel never fall through to the video
        MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.ArrowCursor }

        Column {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            spacing: 12

            Text {
                width: parent.width
                visible: drawer.mediaTitle.length > 0
                text: drawer.mediaTitle
                elide: Text.ElideRight
                color: drawer.ink; font.family: "Segoe UI Variable Display"
                font.pixelSize: 18; font.weight: Font.DemiBold
            }

            // tabs â€” one composite Tab stop.
            Row {
                id: tabStrip
                readonly property var options: drawer.hasEpisodes ? ["episodes", "sources"] : ["sources"]
                property int keyboardIndex: Math.max(0, options.indexOf(drawer.tab))
                width: parent.width
                spacing: 8
                focusPolicy: drawer.open ? Qt.TabFocus : Qt.NoFocus
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        var next = keyboardIndex + (event.key === Qt.Key_Left ? -1 : 1)
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
                        width: (header.width - (drawer.hasEpisodes ? 8 : 0)) / (drawer.hasEpisodes ? 2 : 1)
                        height: 34
                        radius: 6
                        color: drawer.tab === tabPill.modelData ? drawer.goldTint : Qt.rgba(1, 1, 1, 0.05)
                        border.width: tabStrip.activeFocus && tabStrip.keyboardIndex === tabPill.index ? 2 : 0
                        border.color: drawer.gold
                        Text { anchors.centerIn: parent; text: tabPill.modelData === "episodes" ? "EPISODES" : "SOURCES"; color: drawer.tab === tabPill.modelData ? drawer.gold : drawer.inkDim; font.family: "Segoe UI"; font.pixelSize: 11; font.letterSpacing: 1 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: drawer.tab = tabPill.modelData }
                    }
                }
            }
        }

        // ---- EPISODES tab ----
        EpisodeBrowser {
            id: episodeBrowser
            visible: drawer.tab === "episodes"
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            theme: drawer.theme
            seasonCount: drawer.seasonCount
            activeSeason: drawer.activeSeason
            episodes: drawer.episodes
            currentEpisodeId: drawer.currentEpisodeId
            state: drawer.episodesState
            onSeasonPicked: function(season) { drawer.selectSeason(season) }
            onEpisodePicked: function(episodeId) {
                drawer.episodePicked(episodeId)
                drawer.dismissed()
            }
        }

        // ---- SOURCES tab ----
        ListView {
            id: sourceList
            visible: drawer.tab === "sources"
            anchors.top: header.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: drawer.sources
            focusPolicy: visible && count > 0 ? Qt.TabFocus : Qt.NoFocus
            Keys.onPressed: function(event) { sourceKeyboard.handle(event) }
            delegate: Item {
                id: srcRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 64
                readonly property string st: Browser.sourceRowState(
                    srcRow.modelData.current === true, srcRow.modelData.dead === true)

                Rectangle {
                    anchors.fill: parent
                    color: srcRow.st === "now" ? drawer.goldTint
                         : (srcMa.containsMouse && srcRow.st === "playable") ? Qt.rgba(1, 1, 1, 0.05)
                         : "transparent"
                }
                Rectangle {
                    visible: srcRow.st === "now"
                    anchors.left: parent.left; width: 2
                    anchors.top: parent.top; anchors.bottom: parent.bottom
                    color: drawer.gold
                }
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Row {
                        spacing: 8
                        Text {
                            text: srcRow.modelData.title || "Source"
                            color: srcRow.st === "dead" ? drawer.inkDimmer : drawer.ink
                            font.family: "Segoe UI"; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        Text {
                            text: srcRow.modelData.quality || ""
                            color: drawer.gold; font.family: "Segoe UI"; font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: srcRow.st === "now"
                            text: "PLAYING"
                            color: drawer.gold; font.family: "Segoe UI"; font.pixelSize: 9; font.letterSpacing: 1
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: srcRow.st === "dead"
                            text: "didn't start"
                            color: drawer.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        width: parent.width
                        visible: Number(srcRow.modelData.seeders) > 0
                        text: srcRow.modelData.seeders + " seeders"
                        elide: Text.ElideRight
                        color: srcRow.st === "dead" ? drawer.inkDimmer : drawer.inkDim
                        font.family: "Segoe UI"; font.pixelSize: 11
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(1, 1, 1, 0.05) }
                MouseArea {
                    id: srcMa
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: srcRow.st === "playable" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (srcRow.st === "playable")
                                   drawer.sourcePicked(srcRow.index, srcRow.modelData.id)
                }
            }
            KeyboardCollectionController {
                id: sourceKeyboard
                view: sourceList
                orientation: "vertical"
                count: sourceList.count
                onActivated: function(index) {
                    if (index < 0 || index >= drawer.sources.length) return
                    var row = drawer.sources[index] || ({})
                    if (Browser.sourceRowState(row.current === true, row.dead === true) === "playable") drawer.sourcePicked(index, row.id)
                }
            }
            footer: Text {
                visible: drawer.sourcesState === "ready" && drawer.sources.length === 0
                width: ListView.view ? ListView.view.width : 0
                horizontalAlignment: Text.AlignHCenter
                topPadding: 30
                text: "This is a downloaded file — no live sources."
                color: drawer.inkDimmer
                font.family: "Segoe UI"; font.pixelSize: 12
            }
        }
    }
}
