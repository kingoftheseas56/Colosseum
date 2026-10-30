// ExtensionsStorePage — Extensions as a world (agents/colosseum-extensions-world-mock.html):
// a top bar with only Back and the page name (as Your Colosseum), the House hero, the Essentials row, then the Store rows filled
// live from stremio-addons.net (ExtensionsStoreApi.js). Search and the manifest-preview URL installer
// live in the House corner. An add-on needing setup uses ExtensionsSetupSheet, not the outside
// browser — except on Android, which has no QtWebEngine and hands setup to the phone's browser
// (ExtensionsSetupSheetAndroid). Tankoyomi's own settings open from its House tile
// (TankoyomiConfigurationPage).
import QtQuick
import QtQuick.Controls
import QtQuick.Window
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
    readonly property bool phoneLayout: {
        const w = root.Window.window
        return !!(w && w.androidHost && w.width < 840)
    }
    property var busy: ({})                  // slug -> true while an install is in flight
    property string configuringExtensionId: ""   // a House source whose settings page is open
    property var seeAllRow: null             // a row whose "See all ›" is open
    property bool searching: false           // the Store's own search is open
    property bool installUrlOpen: false
    property string installUrlNormalized: ""
    property var installUrlManifest: null
    property string installUrlError: ""
    property bool installUrlPreviewing: false
    property bool installUrlInstalling: false
    property bool installUrlComplete: false
    readonly property var searchPool: root.catalogue.filter(function(a) { return StoreApi.visible(a, root.showExplicit) })

    readonly property var essentials: StoreApi.essentials(root.catalogue)
    readonly property var rows: StoreApi.rows(root.catalogue, root.showExplicit)
    readonly property Item setup: setupLoader.item

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
        if (!a || root.busy[a.slug]) return
        // An add-on with a setup page opens it in-app, installed or not: a bare install of
        // Comet/MediaFusion finds nothing, and an installed one is set up again from its card.
        if (a.configurable) { setup.addon = a; return }
        if (root.isInstalled(a)) return
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

    function openUrlInstaller() {
        root.installUrlNormalized = ""
        root.installUrlManifest = null
        root.installUrlError = ""
        root.installUrlPreviewing = false
        root.installUrlInstalling = false
        root.installUrlComplete = false
        urlInput.text = ""
        root.installUrlOpen = true
        Qt.callLater(function() { urlInput.forceActiveFocus() })
    }
    function closeUrlInstaller() {
        if (root.installUrlPreviewing || root.installUrlInstalling) return
        root.installUrlOpen = false
        Qt.callLater(root.takeKeyboardFocus)
    }
    function previewInstallUrl() {
        if (root.installUrlPreviewing || root.installUrlInstalling) return
        var pasted = urlInput.text.trim()
        if (!pasted.length) { root.installUrlError = "Paste an add-on link first."; return }
        var normalized = Extensions.normalizeUrl(pasted)
        if (!normalized.length) { root.installUrlError = "That isn't a valid add-on address."; return }
        root.installUrlManifest = null
        root.installUrlComplete = false
        root.installUrlError = ""
        root.installUrlNormalized = normalized
        root.installUrlPreviewing = true
        Extensions.preview(pasted)
    }
    function installPreviewedUrl() {
        if (!root.installUrlManifest || root.installUrlInstalling || root.installUrlComplete) return
        if (Extensions.isInstalled(root.installUrlNormalized)) {
            root.installUrlError = "This add-on address is already installed."
            return
        }
        root.installUrlError = ""
        root.installUrlInstalling = true
        Extensions.install(root.installUrlNormalized)
    }
    function configurePreviewedUrl() {
        if (!root.installUrlManifest || !root.installUrlNormalized.length) return
        var url = root.installUrlNormalized
        root.installUrlOpen = false
        setup.addon = {
            name: String(root.installUrlManifest.name || "Extension"),
            manifestUrl: url,
            configureUrl: url.replace(/\/manifest\.json(?=[?#]|$)/i, "/configure"),
            configurable: true,
            setupRequired: true
        }
    }
    function needsSetup(manifest) {
        return !!(manifest && manifest.behaviorHints && manifest.behaviorHints.configurationRequired)
    }
    function manifestResources(manifest) {
        var resources = manifest && manifest.resources ? manifest.resources : []
        var names = []
        for (var i = 0; i < resources.length; i++) {
            var name = typeof resources[i] === "string" ? resources[i] : resources[i].name
            if (name && names.indexOf(name) < 0) names.push(name)
        }
        return names.length ? names.join(" · ") : "No resources declared"
    }

    function takeKeyboardFocus() {
        if (tankoyomiConfiguration.visible) tankoyomiConfiguration.takeKeyboardFocus()
        else if (seeAll.visible) seeAll.takeKeyboardFocus()
        else storeWorld.forceActiveFocus()
    }
    // Main asks first: setup closes, then a settings page, then search / See all, then the world.
    function requestEscape() {
        if (root.installUrlOpen) { root.closeUrlInstaller(); return true }
        if (setup && setup.open) { setup.addon = null; return true }
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
            if (root.installUrlOpen && root.installUrlInstalling) {
                root.installUrlInstalling = false
                root.installUrlComplete = true
            }
            // Installing means switching on. A built-in Theatre add-on that ships switched off
            // (Torrentio, NoTorrent) is "updated in place" by an install at its own address and
            // kept its off switch: the Store went on saying Set up and no stream ever appeared
            // (seen on the Nokia, 2026-10-10). Core rows refuse this call, which is right.
            Extensions.setEnabled(id, true)
            if (setup.open) {
                // A set-up link is a new row beside the old bare one; the bare row only
                // errors (Comet answers 403), so it goes once the configured one is in.
                var bare = Extensions.normalizeUrl(setup.addon.manifestUrl)
                if (setup.pendingUrl.length && setup.pendingUrl !== bare && Extensions.isInstalled(bare))
                    Extensions.removeInstance(bare)
                setup.addon = null
            }
        }
        function onInstallFailed(url, reason) {
            root.busy = ({})
            if (root.installUrlOpen && root.installUrlInstalling) {
                root.installUrlInstalling = false
                root.installUrlError = "Couldn't install it: " + reason
                return
            }
            if (setup.open) setup.status = "Couldn't add it: " + reason
            else { root.installError = "Couldn't add it: " + reason; installErrorTimer.restart() }
        }
        function onPreviewReady(url, manifest) {
            if (!root.installUrlOpen || !root.installUrlPreviewing
                    || url !== root.installUrlNormalized) return
            root.installUrlPreviewing = false
            root.installUrlManifest = manifest
        }
        function onPreviewFailed(url, reason) {
            if (!root.installUrlOpen || !root.installUrlPreviewing) return
            root.installUrlPreviewing = false
            root.installUrlError = "Couldn't preview it: " + reason
        }
    }
    // A direct install that fails says so (it used to clear the busy badge and nothing else).
    property string installError: ""
    Timer { id: installErrorTimer; interval: 6000; onTriggered: root.installError = "" }

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
        androidHost: root.phoneLayout
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
            onInstallUrlRequested: root.openUrlInstaller()
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
            phoneLayout: root.phoneLayout
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
                phoneLayout: root.phoneLayout
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

    Rectangle {
        visible: root.installError.length > 0
        z: 15
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 40
        width: Math.min(errorText.implicitWidth + 44, parent.width - 120); height: 44; radius: 22
        color: Qt.rgba(0.045, 0.05, 0.075, 0.96)
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14)
        Text {
            id: errorText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 44)
            elide: Text.ElideRight
            text: root.installError
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 13
        }
    }

    // A Loader, not a static child: naming ExtensionsSetupSheet here would pull QtWebEngine into
    // the Android build, where it does not exist, and the whole Store would fail to load.
    Loader {
        id: setupLoader
        anchors.fill: parent
        z: 20
        source: Qt.platform.os === "android" ? "ExtensionsSetupSheetAndroid.qml" : "ExtensionsSetupSheet.qml"
    }
    Connections {
        target: setupLoader.item
        function onCloseRequested() { setup.addon = null }
        function onInstallRequested(url) {
            setup.pendingUrl = Extensions.normalizeUrl(url)
            Extensions.install(url)
        }
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

    // A catalogue-independent entry point into the existing native preview/install flow.
    Item {
        id: urlInstallOverlay
        anchors.fill: parent
        z: 40
        visible: root.installUrlOpen
        focus: visible
        Keys.onEscapePressed: root.closeUrlInstaller()

        Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.72) }
        MouseArea { anchors.fill: parent; onClicked: root.closeUrlInstaller() }

        Rectangle {
            id: urlInstallPanel
            objectName: "extensionsInstallUrlDialog"
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 540)
            height: Math.min(parent.height - 24, root.installUrlManifest ? 488 : 358)
            radius: 24
            color: "#141820"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.22)
            MouseArea { anchors.fill: parent; onClicked: {} }

            Column {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 26
                spacing: 12

                Item {
                    width: parent.width; height: 37
                    Text {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        text: "Install from URL"
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 28; font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: 16
                        color: closeUrlAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                        Text { anchors.centerIn: parent; text: "×"; color: theme.inkDim; font.pixelSize: 22 }
                        KeyboardAction {
                            id: closeUrlAction
                            objectName: "extensionsInstallUrlClose"
                            anchors.fill: parent; focusRadius: 16
                            accessibleName: "Close install from URL"
                            onTriggered: root.closeUrlInstaller()
                        }
                    }
                }
                Text {
                    width: parent.width
                    text: "Add a Stremio-compatible extension, even if it isn't in the Store."
                    color: theme.inkDim
                    font.family: theme.ui; font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
                Text {
                    text: "ADD-ON ADDRESS"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                }
                Rectangle {
                    width: parent.width; height: 50; radius: 14
                    color: Qt.rgba(1, 1, 1, 0.055)
                    border.width: 1
                    border.color: urlInput.activeFocus ? theme.gold : Qt.rgba(1, 1, 1, 0.18)
                    TextField {
                        id: urlInput
                        objectName: "extensionsInstallUrlField"
                        anchors.fill: parent
                        leftPadding: 15; rightPadding: 15
                        color: theme.ink
                        placeholderText: "https://example.com/manifest.json"
                        placeholderTextColor: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13
                        background: null
                        selectByMouse: true
                        enabled: !root.installUrlPreviewing && !root.installUrlInstalling
                        onAccepted: root.previewInstallUrl()
                        onTextChanged: {
                            root.installUrlManifest = null
                            root.installUrlComplete = false
                            root.installUrlError = ""
                        }
                        Keys.onEscapePressed: root.closeUrlInstaller()
                    }
                }
                Text {
                    width: parent.width
                    text: "HTTPS and stremio:// links are supported."
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12
                }
                Rectangle {
                    visible: root.installUrlManifest !== null
                    width: parent.width; height: 132; radius: 16
                    color: Qt.rgba(1, 1, 1, 0.045)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
                    Row {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top; anchors.margins: 14
                        spacing: 13
                        Rectangle {
                            width: 52; height: 52; radius: 13
                            color: Qt.rgba(1, 1, 1, 0.10)
                            Text {
                                anchors.centerIn: parent
                                text: root.installUrlManifest
                                      ? String(root.installUrlManifest.name || "?").slice(0, 1).toUpperCase() : "?"
                                color: theme.gold; font.family: theme.display; font.pixelSize: 25
                            }
                        }
                        Column {
                            width: parent.width - 65; spacing: 5
                            Text {
                                width: parent.width; elide: Text.ElideRight
                                text: root.installUrlManifest
                                      ? String(root.installUrlManifest.name || "Unnamed extension") : ""
                                color: theme.ink
                                font.family: theme.ui; font.pixelSize: 16; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width; wrapMode: Text.WordWrap
                                maximumLineCount: 2; elide: Text.ElideRight
                                text: root.installUrlManifest
                                      ? String(root.installUrlManifest.description || "No description provided.") : ""
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                            }
                        }
                    }
                    Text {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.bottom: parent.bottom; anchors.margins: 14
                        elide: Text.ElideRight
                        text: "Provides: " + root.manifestResources(root.installUrlManifest)
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    }
                }
                Text {
                    visible: root.installUrlError.length > 0
                    width: parent.width
                    text: root.installUrlError
                    color: "#ffb4ab"
                    font.family: theme.ui; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: root.installUrlComplete
                    text: "Added to your extensions."
                    color: theme.gold
                    font.family: theme.ui; font.pixelSize: 13
                }
            }
            Row {
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.margins: 26
                spacing: 10
                Rectangle {
                    width: 102; height: 42; radius: 21
                    color: cancelUrlAction.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.065)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.17)
                    Text {
                        anchors.centerIn: parent; text: "Cancel"
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                    }
                    KeyboardAction {
                        id: cancelUrlAction
                        anchors.fill: parent; focusRadius: 21
                        accessibleName: "Cancel URL installation"
                        onTriggered: root.closeUrlInstaller()
                    }
                }
                Rectangle {
                    width: 154; height: 42; radius: 21
                    color: theme.gold
                    opacity: installUrlPrimary.enabled ? 1 : 0.46
                    Text {
                        anchors.centerIn: parent
                        text: root.installUrlComplete ? "Done"
                              : root.installUrlPreviewing ? "Checking…"
                              : root.installUrlInstalling ? "Installing…"
                              : root.installUrlManifest
                                ? (Extensions.isInstalled(root.installUrlNormalized)
                                   ? "Already installed"
                                   : root.needsSetup(root.installUrlManifest) ? "Set up add-on" : "Install add-on")
                                : "Preview add-on"
                        color: "#17150f"
                        font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        id: installUrlPrimary
                        objectName: "extensionsInstallUrlConfirm"
                        anchors.fill: parent; focusRadius: 21; focusPrimary: true
                        enabled: !root.installUrlPreviewing && !root.installUrlInstalling
                                 && (root.installUrlComplete
                                     || (root.installUrlManifest !== null
                                         && !Extensions.isInstalled(root.installUrlNormalized))
                                     || (root.installUrlManifest === null
                                         && urlInput.text.trim().length > 0))
                        accessibleName: root.installUrlComplete ? "Done"
                                         : root.installUrlManifest ? "Install add-on" : "Preview add-on"
                        onTriggered: {
                            if (root.installUrlComplete) root.closeUrlInstaller()
                            else if (root.installUrlManifest) {
                                if (root.needsSetup(root.installUrlManifest)) root.configurePreviewedUrl()
                                else root.installPreviewedUrl()
                            }
                            else root.previewInstallUrl()
                        }
                    }
                }
            }
        }
    }
}
