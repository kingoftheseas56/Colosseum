pragma ComponentBehavior: Bound
import QtQuick
import "UniverseExtApi.js" as UniverseApi

Item {
    id: root
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
    Component.onCompleted: root.reload()

    function reload() {
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

    function openCatalogueVolume(volumeNumber, colorEdition) {
        var manga = colorEdition ? root.colorMangaEntry() : root.entry("manga", "30013")
        if (!manga) return
        var routed = ({})
        for (var key in manga) routed[key] = manga[key]
        routed.requestedArc = root.arc
        routed.requestedVolumeNumber = String(volumeNumber || "")
        routed.colorEdition = colorEdition === true
        root.seriesRequested(routed)
    }

    function relatedItemsForArc() {
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
        var id = root.arc && root.arc.id ? String(root.arc.id) : "romance"
        return "../assets/universes/one-piece/east-blue-markers/" + id + ".png"
    }

    Item {
        id: pageBackdrop
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: "#080b0f" }
        Image {
            anchors.fill: parent
            source: root.heroSourceForArc()
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: 0.30
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
