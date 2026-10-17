pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import "UniverseExtApi.js" as UniverseApi
import "OnePieceParadiseAdapter.js" as ParadiseAdapter
import "OnePieceNewWorldAdapter.js" as NewWorldAdapter
import "OnePieceFinalSeaAdapter.js" as FinalSeaAdapter

Item {
    id: root
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    onUniverseContentGenerationChanged: { refreshArcContent(); reload() }
    objectName: "onePieceArcPage"
    anchors.fill: parent
    focus: true
    activeFocusOnTab: true

    required property var arc
    property string extensionId: ""
    property var installedExtensions: []
    property bool reducedMotion: false
    property var payload: null

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)
    signal seriesRequested(var entry)
    signal onePaceRequested(var arc)

    Theme { id: theme }

    onExtensionIdChanged: root.reload()
    onArcChanged: if (heroImage) heroImage.fallbackActive = false
    Component.onCompleted: root.reload()

    function reload() {
        if (!root.extensionId) { root.payload = null; return }
        UniverseApi.load(root.extensionId, function(p) { root.payload = p })
    }
    function refreshArcContent() {
        if (!root.arc || !root.arc.id || !root.arc.seaId) return
        var data = UniverseContent.document("tribute-one-piece-" + root.arc.seaId)
        var records = data.story && data.story.arcs
        if (!records) return
        for (var i = 0; i < records.length; ++i) {
            if (records[i].id !== root.arc.id) continue
            var record = UniverseContent.resolveAssets(records[i])
            var updated = root.arc.seaId === "paradise" ? ParadiseAdapter.cataloguePayload(record)
                        : root.arc.seaId === "new-world" ? NewWorldAdapter.cataloguePayload(record)
                        : root.arc.seaId === "final-sea" ? FinalSeaAdapter.cataloguePayload(record) : record
            if (!updated) return
            if (root.arc.seaId === "east-blue") {
                updated.seaId = "east-blue"; updated.seaLabel = "East Blue"; updated.sagaLabel = "East Blue"
                var art = data.artByArc && UniverseContent.resolveAssets(data.artByArc[record.id])
                if (art) { updated.heroSource = art.posterSource; updated.heroFallbackSource = art.fallbackSource }
            }
            root.arc = updated
            return
        }
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

    function openCatalogueVolume(volumeNumber, colorEdition) {
        var manga = colorEdition ? root.colorMangaEntry() : root.entry("manga", "30013")
        if (!manga) return
        var routed = ({})
        for (var key in manga) routed[key] = manga[key]
        routed.requestedArc = root.arc
        routed.requestedVolumeNumber = String(volumeNumber || "")
        routed.colorEdition = colorEdition === true
        if (!colorEdition) {
            // This arc catalogue is explicitly One Piece (MAL 13). Its universe
            // payload uses AniList 30013, which must never be treated as a MAL ID.
            routed.provider = "tankoban"
            routed.malId = "13"
            routed.seriesId = "mal:13"
        }
        root.seriesRequested(routed)
    }

    function relatedItemsForArc() {
        if (root.arc && root.arc.relatedMedia !== undefined)
            return root.arc.relatedMedia || []
        var rows = [{
            id: "tt11757066", type: "movie", title: "Episode of East Blue",
            episode: "", thumbnail: "https://live.metahub.space/background/medium/tt11757066/img"
        }]
        if (root.arc && root.arc.id === "arlong") {
            rows.push({
                id: "tt2598466", type: "movie", title: "Episode of Nami",
                episode: "", thumbnail: "https://live.metahub.space/background/medium/tt2598466/img"
            })
        }
        return rows
    }

    function heroSourceForArc() {
        if (root.arc && root.arc.heroSource) return String(root.arc.heroSource)
        var id = root.arc && root.arc.id ? String(root.arc.id) : "romance"
        root.universeContentGeneration
        return typeof UniverseContent !== "undefined" ? UniverseContent.asset("../assets/universes/one-piece/east-blue-markers/" + id + ".png") : "../assets/universes/one-piece/east-blue-markers/" + id + ".png"
    }

    Item {
        id: pageBackdrop
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: "#080b0f" }
        Image {
            id: heroImage
            objectName: "onePieceArcHero"
            property bool fallbackActive: false
            anchors.fill: parent
            source: fallbackActive && root.arc && root.arc.heroFallbackSource
                    ? root.arc.heroFallbackSource : root.heroSourceForArc()
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 1920
            opacity: 0.30
            onStatusChanged: {
                if (status === Image.Error && !fallbackActive && root.arc && root.arc.heroFallbackSource)
                    fallbackActive = true
            }
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.015, 0.02, 0.025, 0.58) }
    }

    OnePieceArcCatalogue {
        id: catalogue
        anchors.fill: parent
        arc: root.arc
        backdrop: pageBackdrop
        installedExtensions: root.installedExtensions
        reducedMotion: root.reducedMotion
        relatedItems: root.relatedItemsForArc()
        onBackRequested: root.backRequested()
        onEpisodeRequested: function(entry) { root.watchRequested(entry) }
        onMangaVolumeRequested: function(colorEdition, volumeNumber) {
            root.openCatalogueVolume(volumeNumber, colorEdition)
        }
        onRelatedRequested: function(entry) {
            if (!entry) return
            var routed = ({})
            for (var key in entry) routed[key] = entry[key]
            routed.requestedArc = root.arc
            root.watchRequested(routed)
        }
    }

    Row {
        z: 40
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        UniverseChromeAction {
            accessibleName: "Minimize"
            source: "../assets/icons/minimize.svg"
            onTriggered: root.minimizeRequested()
        }
        UniverseChromeAction {
            accessibleName: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                            ? "Enter fullscreen" : "Exit fullscreen"
            source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                    ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
            onTriggered: root.fullscreenRequested()
        }
        UniverseChromeAction {
            accessibleName: "Close Colosseum"
            source: "../assets/icons/power.svg"
            onTriggered: root.closeRequested()
        }
    }
}
