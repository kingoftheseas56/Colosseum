// ExtensionsStorePage — Extensions as a world (agents/colosseum-extensions-world-mock.html):
// a top bar with only Back and the page name (as Your Colosseum), the House hero, the Essentials row, then the Store rows filled
// live from stremio-addons.net (ExtensionsStoreApi.js). Search lives in the House corner. An
// add-on that needs setup is set up inside the app (ExtensionsSetupSheet), never in the outside
// browser; Tankoyomi's own settings open from its House tile (TankoyomiConfigurationPage).
import QtQuick
import "ExtensionsStoreApi.js" as StoreApi

Item {
    id: root
    objectName: "extensionsStorePage"

    property Item backdrop: null
    property bool showExplicit: false
    property string world: "theatre"
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal universeHallRequested()

    property var catalogue: []               // every cleaned add-on, most starred first
    property string loadError: ""
    property bool loading: true
    property int revision: 0                 // bumps on every Extensions change
    property var busy: ({})                  // slug -> true while an install is in flight
    property string configuringExtensionId: ""   // a House source whose settings page is open
    property var seeAllRow: null             // a row whose "See all ›" is open
    property bool searching: false           // the Store's own search is open
    readonly property var searchPool: root.catalogue.filter(function(a) { return StoreApi.visible(a, root.showExplicit) })

    readonly property var essentials: StoreApi.essentials(root.catalogue)
    readonly property var rows: StoreApi.rows(root.catalogue, root.showExplicit)

    Theme { id: theme }

    function isInstalled(a) {
        if (!a || typeof Extensions === "undefined") return false
        return Extensions.isInstalled(a.id) || Extensions.isInstalled(a.manifestUrl)
    }
    function setBusy(slug, on) {
        var next = {}
        for (var k in root.busy) if (k !== slug) next[k] = root.busy[k]
        if (on) next[slug] = true
        root.busy = next
    }
    function activate(a) {
        if (!a || root.isInstalled(a) || root.busy[a.slug]) return
        if (a.setupRequired) { setup.addon = a; return }
        root.setBusy(a.slug, true)
        Extensions.install(a.manifestUrl)
    }
    function load() {
        root.loading = true
        root.loadError = ""
        StoreApi.fetchAll(function(all, error) {
            root.catalogue = all
            root.loadError = error
            root.loading = false
        })
    }

    function openConfiguration(extensionId) { root.configuringExtensionId = extensionId }
    function closeConfiguration() { root.configuringExtensionId = "" }

    function takeKeyboardFocus() {
        if (tankoyomiConfiguration.visible) tankoyomiConfiguration.takeKeyboardFocus()
        else if (seeAll.visible) seeAll.takeKeyboardFocus()
        else storeWorld.forceActiveFocus()
    }
    // Main asks first: setup closes, then a settings page, then search / See all, then the world.
    function requestEscape() {
        if (setup.open) { setup.addon = null; return true }
        if (root.configuringExtensionId.length) { root.closeConfiguration(); return true }
        if (root.seeAllRow || root.searching) { root.seeAllRow = null; root.searching = false; return true }
        return false
    }

    Component.onCompleted: root.load()
    onConfiguringExtensionIdChanged: Qt.callLater(root.takeKeyboardFocus)

    Connections {
        target: (typeof Extensions !== "undefined") ? Extensions : null
        function onChanged() { root.revision++ }
        function onInstallFinished(id, name) {
            root.busy = ({})
            if (setup.open) setup.addon = null
        }
        function onInstallFailed(url, reason) {
            root.busy = ({})
            if (setup.open) setup.status = "Couldn't add it: " + reason
        }
    }

    // The Store's own wallpaper (Hemanth's pick, 2026-09-28): the Jolly Roger, under a dark veil
    // so the cards read over it. It stays put while the board scrolls, like a world wallpaper.
    Item {
        id: night
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: "#06070a" }
        Image {
            anchors.fill: parent
            source: "../assets/extensions/store-jolly-roger.jpg"
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.55) }
                GradientStop { position: 0.5; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.66) }
                GradientStop { position: 1.0; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.80) }
            }
        }
    }

    WorldPage {
        id: storeWorld
        objectName: "extensionsStoreWorld"
        anchors.fill: parent
        visible: !seeAll.visible && !tankoyomiConfiguration.visible
        medium: "Extensions"
        topBarBackOnly: true
        topBarTitle: "Extensions"
        topBarSubtitle: "Add-ons and sources for every world."
        backdrop: night
        onHomeRequested: root.backRequested()

        ExtensionsHouseHero {
            revision: root.revision
            onUniversesRequested: root.universeHallRequested()
            onSearchRequested: root.searching = true
            onConfigureRequested: (id) => root.openConfiguration(id)
        }

        Text {
            visible: root.loading || (root.loadError.length > 0 && root.catalogue.length === 0)
            width: parent.width
            text: root.loading ? "Opening the Store…"
                               : "The Store couldn't reach stremio-addons.net (" + root.loadError + "). Try again in a moment."
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }

        ExtensionsStoreRail {
            title: "Essentials"
            sub: "The five a fresh Colosseum needs."
            automationId: "extensionsEssentialsRail"
            canSeeAll: false                 // all five are already on screen
            items: root.essentials
            installedFn: root.isInstalled
            busy: root.busy
            revision: root.revision
            onAddonActivated: (a) => root.activate(a)
        }

        Repeater {
            model: root.rows
            delegate: ExtensionsStoreRail {
                required property var modelData
                required property int index
                title: modelData.title
                sub: modelData.sub
                automationId: "extensionsStoreRail_" + index
                items: modelData.items
                installedFn: root.isInstalled
                busy: root.busy
                revision: root.revision
                onAddonActivated: (a) => root.activate(a)
                onSeeAllRequested: root.seeAllRow = modelData
            }
        }

        Text {
            visible: root.catalogue.length > 0
            width: parent.width
            text: "Community add-ons and stars from stremio-addons.net. Their authors run them, not Colosseum."
            color: Qt.rgba(1, 1, 1, 0.34)
            font.family: theme.ui; font.pixelSize: 12
        }
    }

    ExtensionsSeeAllPage {
        id: seeAll
        anchors.fill: parent
        z: 10
        row: root.seeAllRow
        searching: root.searching
        pool: root.searchPool
        installedFn: root.isInstalled
        busy: root.busy
        revision: root.revision
        onAddonActivated: (a) => root.activate(a)
        onBackRequested: { root.seeAllRow = null; root.searching = false }
        onVisibleChanged: if (visible) Qt.callLater(seeAll.takeKeyboardFocus)
                          else Qt.callLater(root.takeKeyboardFocus)
    }

    ExtensionsSetupSheet {
        id: setup
        anchors.fill: parent
        z: 20
        onCloseRequested: setup.addon = null
        onInstallRequested: (url) => Extensions.install(url)
    }

    // Tankoyomi's own settings (source languages and providers), from its House tile.
    TankoyomiConfigurationPage {
        id: tankoyomiConfiguration
        anchors.fill: parent
        z: 30
        visible: root.configuringExtensionId === "colosseum.well.tankoyomi"
        backdrop: night
        onBackRequested: root.closeConfiguration()
        onSearchClicked: root.searchClicked()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onCloseRequested: root.closeRequested()
    }
}
