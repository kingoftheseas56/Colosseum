// ExtensionsStorePage — Extensions as a world (agents/colosseum-extensions-world-mock.html):
// the world top bar, the House hero, the Essentials row, then the Store rows filled live from
// stremio-addons.net (ExtensionsStoreApi.js). An add-on that needs setup is set up inside the app
// (ExtensionsSetupSheet), never in the outside browser. Manage opens the previous Extensions page.
//
// Keeps ExtensionsPage.qml's surface so Main.qml's extensionsLayer wiring is unchanged.
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
    signal worldRequested(string medium)
    signal trackersRequested()
    signal accountRequested(real anchorRight, real anchorBottom)

    property var catalogue: []               // every cleaned add-on, most starred first
    property string loadError: ""
    property bool loading: true
    property int revision: 0                 // bumps on every Extensions change
    property var busy: ({})                  // slug -> true while an install is in flight
    property bool manageOpen: false
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

    function takeKeyboardFocus() {
        if (root.manageOpen && manage.item && manage.item.takeKeyboardFocus) manage.item.takeKeyboardFocus()
        else storeWorld.forceActiveFocus()
    }
    // Main asks first: setup closes, then Manage, then the world itself.
    function requestEscape() {
        if (setup.open) { setup.addon = null; return true }
        if (root.seeAllRow || root.searching) { root.seeAllRow = null; root.searching = false; return true }
        if (root.manageOpen) {
            if (manage.item && manage.item.requestEscape && manage.item.requestEscape()) return true
            root.manageOpen = false
            return true
        }
        return false
    }

    Component.onCompleted: root.load()
    onManageOpenChanged: Qt.callLater(root.takeKeyboardFocus)

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
        anchors.fill: parent
        visible: !root.manageOpen && !seeAll.visible
        medium: "Extensions"
        showWorldPills: false
        backdrop: night
        onHomeRequested: root.backRequested()
        onMediumSelected: (m) => root.worldRequested(m)
        onSearchClicked: root.searching = true     // the Store searches add-ons, not the library
        onTrackersClicked: root.trackersRequested()
        onAccountClicked: (r, b) => root.accountRequested(r, b)
        onFullscreenClicked: root.fullscreenRequested()
        onMinimizeClicked: root.minimizeRequested()
        onPowerClicked: root.closeRequested()

        ExtensionsHouseHero {
            revision: root.revision
            onUniversesRequested: root.universeHallRequested()
            onManageRequested: root.manageOpen = true
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

    // Manage = the previous Extensions page (source ranking, Tankoyomi setup, install by link).
    Loader {
        id: manage
        anchors.fill: parent
        z: 30
        active: root.manageOpen
        visible: active
        source: "ExtensionsPage.qml"
        onLoaded: {
            item.backdrop = root.backdrop
            item.showExplicit = Qt.binding(function() { return root.showExplicit })
            item.world = root.world
            item.backRequested.connect(function() { root.manageOpen = false })
            item.minimizeRequested.connect(root.minimizeRequested)
            item.fullscreenRequested.connect(root.fullscreenRequested)
            item.closeRequested.connect(root.closeRequested)
            item.searchClicked.connect(root.searchClicked)
        }
    }
}
