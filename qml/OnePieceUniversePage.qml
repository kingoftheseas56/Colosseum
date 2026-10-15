pragma ComponentBehavior: Bound
import QtQuick
import "OnePieceEastBlueAtlasData.js" as EastBlueAtlasData

FocusScope {
    id: root
    objectName: "onePieceUniversePage"
    anchors.fill: parent
    focus: true
    activeFocusOnTab: true

    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    property var installedExtensions: []
    property string activeSeaId: "east-blue"
    property bool arcPageOpen: false
    property var loadedSeas: ({ "east-blue": true })
    readonly property var activeAtlasLoader: root.activeSeaId === "paradise" ? paradiseLoader
                                          : root.activeSeaId === "new-world" ? newWorldLoader
                                          : root.activeSeaId === "final-sea" ? finalSeaLoader
                                          : eastBlueLoader
    readonly property var activeAtlas: root.activeAtlasLoader.item
    readonly property bool atlasTransientOpen: !!root.activeAtlas && root.activeAtlas.transientOpen
    enabled: !root.arcPageOpen

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal arcRequested(var arc)
    signal mediaRequested(var entry)

    function requestEscape() {
        var atlas = root.activeAtlas
        if (atlas && atlas.requestEscape)
            return atlas.requestEscape()
        return false
    }

    function showSea(id) {
        if (["east-blue", "paradise", "new-world", "final-sea"].indexOf(id) < 0)
            return false
        root.activeSeaId = id
        var visited = ({})
        for (var key in root.loadedSeas) visited[key] = root.loadedSeas[key]
        visited[id] = true
        root.loadedSeas = visited
        Qt.callLater(root.takeKeyboardFocus)
        return true
    }

    function takeKeyboardFocus() {
        if (!root.visible || !root.enabled) return
        root.forceActiveFocus(Qt.OtherFocusReason)
        var atlas = root.activeAtlas
        // FocusScope restores the last focused marker or index row after an arc.
        if (atlas) atlas.forceActiveFocus(Qt.OtherFocusReason)
    }

    function openEastBlueArc(arc) {
        if (!arc) return
        var routed = ({})
        for (var key in arc) routed[key] = arc[key]
        routed.seaId = "east-blue"
        routed.seaLabel = "East Blue"
        routed.sagaLabel = "East Blue"
        routed.heroSource = EastBlueAtlasData.bannerSource(arc.id)
        routed.heroFallbackSource = EastBlueAtlasData.canonMarker(arc.id).poster
        root.arcRequested(routed)
    }

    onArcPageOpenChanged: if (!root.arcPageOpen) Qt.callLater(root.takeKeyboardFocus)

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#07131d" }

    // Retain visited maps so a sea change preserves zoom and chart position.
    Loader {
        id: eastBlueLoader
        anchors.fill: parent
        active: true
        visible: root.activeSeaId === "east-blue"
        enabled: visible && root.enabled
        onLoaded: Qt.callLater(root.takeKeyboardFocus)
        sourceComponent: Component {
            OnePieceEastBlueAtlas {
                reducedMotion: root.reducedMotion
                onArcRequested: function(arc) { root.openEastBlueArc(arc) }
                onMediaRequested: function(entry) { root.mediaRequested(entry) }
                onParadiseRequested: root.showSea("paradise")
            }
        }
    }
    Loader {
        id: paradiseLoader
        anchors.fill: parent
        active: root.loadedSeas["paradise"] === true
        visible: root.activeSeaId === "paradise"
        enabled: visible && root.enabled
        onLoaded: Qt.callLater(root.takeKeyboardFocus)
        sourceComponent: Component {
            OnePieceParadiseAtlas {
                reducedMotion: root.reducedMotion
                onArcRequested: function(arc) { root.arcRequested(arc) }
                onEastBlueRequested: root.showSea("east-blue")
                onNewWorldRequested: root.showSea("new-world")
            }
        }
    }
    Loader {
        id: newWorldLoader
        anchors.fill: parent
        active: root.loadedSeas["new-world"] === true
        visible: root.activeSeaId === "new-world"
        enabled: visible && root.enabled
        onLoaded: Qt.callLater(root.takeKeyboardFocus)
        sourceComponent: Component {
            OnePieceNewWorldAtlas {
                reducedMotion: root.reducedMotion
                onArcRequested: function(arc) { root.arcRequested(arc) }
                onParadiseRequested: root.showSea("paradise")
                onBeyondWanoRequested: root.showSea("final-sea")
            }
        }
    }
    Loader {
        id: finalSeaLoader
        anchors.fill: parent
        active: root.loadedSeas["final-sea"] === true
        visible: root.activeSeaId === "final-sea"
        enabled: visible && root.enabled
        onLoaded: Qt.callLater(root.takeKeyboardFocus)
        sourceComponent: Component {
            OnePieceFinalSeaAtlas {
                reducedMotion: root.reducedMotion
                onArcRequested: function(arc) { root.arcRequested(arc) }
                onNewWorldRequested: root.showSea("new-world")
            }
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
