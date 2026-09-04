pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "UniverseExtApi.js" as UniverseApi

Item {
    id: root
    anchors.fill: parent
    focus: true
    activeFocusOnTab: true

    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    property var installedExtensions: []
    property var payload: null
    property bool catalogueOpen: false
    property var catalogueArc: ({})
    property string catalogueLane: "anime"

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)
    signal seriesRequested(var entry)
    signal bookRequested(var payload)
    signal comicsArchiveRequested(var payload)
    signal onePaceRequested(var arc)
    signal paradiseRequested()
    signal continueResumeRequested(var entry)
    signal continueDetailRequested(var entry)
    Keys.onPressed: onePieceKeys.handle(event)

    Theme { id: theme }

    onExtensionIdChanged: root.reload()
    function reload() {
        page.contentY = 0
        if (!root.extensionId) { root.payload = null; return }
        UniverseApi.load(root.extensionId, function(p) { root.payload = p })
    }

    function section(id) {
        var sections = root.payload ? root.payload.sections : []
        for (var i = 0; i < sections.length; ++i)
            if (sections[i].id === id) return sections[i]
        return null
    }

    function entry(sectionId, id) {
        var s = root.section(sectionId)
        var entries = s ? s.entries : []
        for (var i = 0; i < entries.length; ++i)
            if (String(entries[i].id) === String(id)) return entries[i]
        return null
    }

    function colorMangaEntry() {
        return root.entry("manga", "one-piece-color") ||
               root.entry("manga", "01J76XYAQSGEJPXCSCVPQ3MHZM")
    }

    function hasOnePaceExtension() {
        for (var i = 0; i < (root.installedExtensions || []).length; ++i) {
            var ext = root.installedExtensions[i]
            if (ext && ext.id === "com.onepace.fedew" && ext.enabled === true) return true
        }
        return false
    }

    function openArcCatalogue(lane) {
        if (lane === "pace" && !root.hasOnePaceExtension()) {
            root.onePaceRequested(eastBlueMap.selectedArc)
            return
        }
        root.catalogueArc = eastBlueMap.selectedArc
        root.catalogueLane = lane || "anime"
        root.catalogueOpen = true
    }

    function openCatalogueEpisode(item, lane) {
        var isLive = lane === "live"
        var series = root.entry("tv", isLive ? "tt11737520" : "tt0388629")
        if (!series || !item) return
        root.watchRequested({ id: isLive ? series.id : "kitsu:12", type: "series",
                              title: series.title, cover: series.poster || "",
                              requestedArc: root.catalogueArc,
                              requestedSeason: Number(item.season || 1),
                              requestedEpisode: Number(item.episode || 1),
                              requestedEpisodeRange: String(item.episode || ""),
                              requestedProvider: item.provider || "" })
    }

    function openAnime(arc) {
        var anime = root.entry("tv", "tt0388629")
        if (!anime) return
        root.watchRequested({ id: anime.id, type: anime.type,
                              title: anime.title, cover: anime.poster || "",
                              requestedArc: arc })
    }

    function openManga(arc, colorEdition) {
        var manga = colorEdition
                ? root.colorMangaEntry()
                : root.entry("manga", "30013")
        if (!manga) return
        var routed = ({})
        for (var key in manga) routed[key] = manga[key]
        routed.requestedArc = arc
        root.seriesRequested(routed)
    }

    function openLiveAction(arc) {
        var live = root.entry("tv", "tt11737520")
        if (!live) return
        var episodeRange = String(arc.liveActionEpisodes || "1")
        var firstEpisode = Number(episodeRange.split("-")[0] || 1)
        root.watchRequested({ id: live.id, type: live.type,
                              title: live.title, cover: live.poster || "",
                              requestedArc: arc,
                              requestedSeason: Number(arc.liveActionSeason || 1),
                              requestedEpisode: firstEpisode,
                              requestedEpisodeRange: arc.liveActionEpisodes })
    }

    function openCatalogueVolume(volumeNumber, colorEdition) {
        var manga = colorEdition
                ? root.colorMangaEntry()
                : root.entry("manga", "30013")
        if (!manga) return
        var routed = ({})
        for (var key in manga) routed[key] = manga[key]
        routed.requestedArc = root.catalogueArc
        routed.requestedVolumeNumber = String(volumeNumber || "")
        routed.colorEdition = colorEdition === true
        root.seriesRequested(routed)
    }

    function openSpecial(id, arc) {
        var special = root.entry("specials", id)
        if (!special) return
        root.watchRequested({ id: special.id, type: special.type,
                              title: special.title, cover: special.poster || "",
                              requestedArc: arc })
    }

    function progressBelongs(entry) {
        if (!entry || !root.payload) return false
        var kind = String(entry.kind || "")
        var id = String(entry.id || "")
        if (kind === "video") {
            var rootId = id.split(":")[0]
            var tv = root.section("tv")
            var vids = tv ? tv.entries : []
            for (var i = 0; i < vids.length; ++i)
                if (String(vids[i].id) === rootId) return true
            return false
        }
        if (kind === "book") return root.entry("novels", id) !== null
        if (kind === "manga" || kind === "tankoban") {
            if (root.entry("manga", id)) return true
            var title = String(entry.title || entry.caption || "").toLowerCase()
            var manga = root.section("manga")
            var rows = manga ? manga.entries : []
            for (var j = 0; j < rows.length; ++j) {
                var candidate = String(rows[j].title || "").toLowerCase()
                if (candidate.length && (title.indexOf(candidate) === 0 || candidate.indexOf(title) === 0))
                    return true
            }
        }
        return false
    }

    function universeContinue() {
        if (typeof Progress === "undefined" || !root.payload) return []
        var recent = Progress.recent("", 100)
        var out = []
        for (var i = 0; i < recent.length && out.length < 12; ++i)
            if (recent[i].kind !== "audiobook" && root.progressBelongs(recent[i])) out.push(recent[i])
        return out
    }

    Rectangle { id: pageBackdrop; anchors.fill: parent; color: "#0c0e11" }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 54
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { id: onePieceGlide; flick: page }

        Column {
            id: contentCol
            width: page.width
            spacing: 0

            Item {
                width: parent.width
                height: 154

                Column {
                    x: theme.margin
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 20
                    spacing: 6
                    Text {
                        text: "UNIVERSE · ONE PIECE"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 4
                    }
                    Text {
                        text: "Explore East Blue"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 50
                    }
                    Text {
                        text: "SEA 01 · EAST BLUE SAGA"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.letterSpacing: 1.5
                    }
                }
            }

            Column {
                id: continueCol
                x: theme.margin
                width: page.width - theme.margin * 2
                spacing: 12
                property int progressRevision: typeof Progress !== "undefined" ? Progress.revision : 0
                property var items: {
                    continueCol.progressRevision
                    return root.universeContinue()
                }
                visible: items.length > 0

                WidgetHeader {
                    width: parent.width
                    title: "Continue"
                    moreLabel: ""
                }
                Flickable {
                    id: continueFlick
                    width: parent.width
                    height: 148
                    contentWidth: continueRow.width
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: continueRow
                        spacing: 18
                        Repeater {
                            model: continueCol.items
                            delegate: ContinueTile {
                                required property var modelData
                                variant: "home"
                                entry: modelData
                                backdrop: pageBackdrop
                                track: page.contentY + continueFlick.contentX
                                onResumeRequested: root.continueResumeRequested(modelData)
                                onDetailRequested: root.continueDetailRequested(modelData)
                                onRemoveRequested: if (typeof Progress !== "undefined")
                                                       Progress.forget(modelData.kind, modelData.id)
                            }
                        }
                    }
                }
                Item { width: 1; height: 10 }
            }

            OnePieceEastBlueMap {
                id: eastBlueMap
                x: theme.margin
                width: page.width - theme.margin * 2
                height: Math.max(620, width * 11 / 27)
                reducedMotion: root.reducedMotion
                onParadiseRequested: root.paradiseRequested()
            }

            Item { width: 1; height: 26 }

            OnePieceArcDock {
                id: eastBlueDock
                x: theme.margin
                width: page.width - theme.margin * 2
                height: 520
                arc: eastBlueMap.selectedArc
                backdrop: pageBackdrop
                onAnimeRequested: root.openArcCatalogue("anime")
                onOnePaceRequested: root.openArcCatalogue("pace")
                onMangaRequested: function(colorEdition) {
                    root.openArcCatalogue(colorEdition ? "color" : "manga")
                }
                onLiveActionRequested: root.openArcCatalogue("live")
                onSpecialRequested: function(id) {
                    root.openSpecial(id, eastBlueMap.selectedArc)
                }
            }

            Item { width: 1; height: 110 }

            Text {
                visible: root.payload === null && root.extensionId !== ""
                x: theme.margin
                topPadding: 18
                text: "One Piece catalogue data is unavailable."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
            }
        }
    }


    OnePieceArcCatalogue {
        id: arcCatalogue
        anchors.fill: parent
        z: 40
        visible: root.catalogueOpen
        enabled: visible
        arc: root.catalogueArc
        backdrop: pageBackdrop
        focusLane: root.catalogueLane
        installedExtensions: root.installedExtensions
        reducedMotion: root.reducedMotion
        onBackRequested: root.catalogueOpen = false
        onEpisodeRequested: function(entry) { root.watchRequested(entry) }
        onMangaVolumeRequested: function(colorEdition, volumeNumber) {
            root.openCatalogueVolume(volumeNumber, colorEdition)
        }
    }


    ChromeScrim { z: 16 }
    BackAction {
        x: theme.margin
        y: 28
        z: 20
        onTriggered: root.backRequested()
    }

    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20

        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/minimize.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: minMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: minMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.minimizeRequested()
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Minimize window"
                onTriggered: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg"
                        : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: fsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.fullscreenRequested()
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Toggle fullscreen"
                onTriggered: root.fullscreenRequested() }
        }
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: "../assets/icons/power.svg"
                sourceSize.width: 22; sourceSize.height: 22
                fillMode: Image.PreserveAspectFit
                opacity: closeMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea {
                id: closeMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.closeRequested()
            }
            KeyboardAction { anchors.fill: parent; pointerEnabled: false; accessibleName: "Close universe"
                onTriggered: root.closeRequested() }
        }
    }

    KeyboardScrollController {
        id: onePieceKeys
        flick: page
        glide: onePieceGlide
    }
}
