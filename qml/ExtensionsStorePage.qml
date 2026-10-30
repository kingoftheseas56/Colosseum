// ExtensionsStorePage — Extensions as a world (agents/colosseum-extensions-world-mock.html):
// a top bar with only Back and the page name (as Your Colosseum), the House hero, Your Add-ons,
// Essentials, then Store rows from stremio-addons.net (ExtensionsStoreApi.js). Your Add-ons
// manages every profile-installed transport URL instance. The House corner holds Search and the
// manifest-preview installer, which waits for artwork or a bounded fallback before confirmation.
// An add-on needing setup uses ExtensionsSetupSheet, not the outside browser — except on Android,
// which has no QtWebEngine and hands setup to the phone's browser (ExtensionsSetupSheetAndroid).
// Tankoyomi's own settings open from its House tile (TankoyomiConfigurationPage).
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
    readonly property bool installUrlPreviewReady: root.installUrlManifest !== null
        && installPreviewLogo.artworkResolved
    property bool managingInstalled: false
    property var pendingInstallOperation: null
    property var replacingInstance: null
    property int profileGeneration: 0
    property int setupGeneration: 0
    property int urlInstallerGeneration: 0
    readonly property var installedEntries: {
        root.revision
        return typeof Extensions !== "undefined" ? Extensions.installed() : []
    }
    readonly property var searchPool: root.catalogue.filter(function(a) { return StoreApi.visible(a, root.showExplicit) })

    readonly property var essentials: StoreApi.essentials(root.catalogue)
    readonly property var rows: StoreApi.rows(root.catalogue, root.showExplicit)
    readonly property var setup: setupLoader.item

    Theme { id: theme }

    function isInstalled(a) {
        if (!a || typeof Extensions === "undefined") return false
        return Extensions.isInstalled(a.id) || Extensions.isInstalled(a.manifestUrl)
    }
    function safeInstallMessage(message) {
        var urlPattern = new RegExp("(?:https?|stremio)://[^\\s\"'<>]+", "gi")
        return String(message).replace(urlPattern, qsTr("[add-on address]"))
    }
    function showInstallError(message) {
        root.installError = root.safeInstallMessage(message)
        installErrorTimer.restart()
    }
    function setBusy(slug, on) {
        var next = {}
        for (var k in root.busy) if (k !== slug) next[k] = root.busy[k]
        if (on) next[slug] = true
        root.busy = next
    }
    function beginInstall(url, details) {
        if (root.pendingInstallOperation) {
            root.showInstallError(qsTr("An add-on install is already finishing. Try again shortly."))
            return false
        }
        var operation = Object.assign({}, details || {})
        operation.url = Extensions.normalizeUrl(url)
        operation.profileGeneration = root.profileGeneration
        operation.startedAt = Date.now()
        root.pendingInstallOperation = operation
        return true
    }
    function openSetup(addon, replacement) {
        if (root.pendingInstallOperation) {
            root.showInstallError(qsTr("An add-on install is already finishing. Try again shortly."))
            return false
        }
        root.setupGeneration++
        root.replacingInstance = replacement || null
        setup.addon = addon
        return true
    }
    function closeSetup() {
        if (root.pendingInstallOperation && root.pendingInstallOperation.kind === "setup")
            return false
        root.setupGeneration++
        root.replacingInstance = null
        if (setup) setup.addon = null
        Qt.callLater(root.takeKeyboardFocus)
        return true
    }
    function failInstallOperation(operation, message) {
        var safeMessage = root.safeInstallMessage(message)
        root.setBusy(operation.slug || "", false)
        if (operation.profileGeneration !== root.profileGeneration) return
        if (operation.kind === "url" && root.urlInstallerGeneration === operation.uiGeneration
                && root.installUrlOpen) {
            root.installUrlInstalling = false
            root.installUrlError = safeMessage
            return
        }
        if (operation.kind === "setup" && root.setupGeneration === operation.setupGeneration
                && setup.open) {
            setup.status = safeMessage
            return
        }
        root.showInstallError(safeMessage)
    }
    function finishInstallOperation(id) {
        var operation = root.pendingInstallOperation
        if (!operation) return
        if (operation.profileGeneration !== root.profileGeneration) {
            root.pendingInstallOperation = null
            root.setBusy(operation.slug || "", false)
            return // profile switching invalidates this UI transaction
        }
        var rows = Extensions.installed()
        var installed = rows.find(function(entry) { return entry.transportUrl === operation.url })
        if (!installed) return // another surface's completion; this operation is still pending
        if (installed.id !== id) return // installFinished is shared; ignore unrelated add-ons
        if (operation.expectedId && installed.id !== operation.expectedId) {
            root.pendingInstallOperation = null
            root.failInstallOperation(operation, qsTr("The address returned a different add-on. The previous installation was kept."))
            return
        }

        var desiredEnabled = operation.replacingUrl ? operation.replacingEnabled : true
        if (installed.enabled !== desiredEnabled) {
            Extensions.setEnabledInstance(operation.url, desiredEnabled)
            installed = Extensions.installed().find(function(entry) { return entry.transportUrl === operation.url })
            if (!installed || installed.enabled !== desiredEnabled) {
                root.pendingInstallOperation = null
                var message = operation.replacingUrl
                        ? qsTr("The new configuration was added, but its switch couldn't be saved. The previous installation was kept.")
                        : qsTr("The add-on was added, but it couldn't be enabled. Try again.")
                root.failInstallOperation(operation, message)
                return
            }
        }

        if (operation.replacingUrl && operation.replacingUrl !== operation.url) {
            Extensions.removeInstance(operation.replacingUrl)
            if (Extensions.installed().some(function(entry) { return entry.transportUrl === operation.replacingUrl })) {
                root.pendingInstallOperation = null
                root.failInstallOperation(operation, qsTr("The new configuration was added, but the previous one couldn't be removed. Both remain installed."))
                return
            }
        }

        root.pendingInstallOperation = null
        root.setBusy(operation.slug || "", false)
        if (operation.kind === "url" && root.urlInstallerGeneration === operation.uiGeneration
                && root.installUrlOpen) {
            root.installUrlInstalling = false
            root.installUrlComplete = true
        } else if (operation.kind === "setup" && root.setupGeneration === operation.setupGeneration
                   && setup.open) {
            root.replacingInstance = null
            setup.addon = null
            Qt.callLater(root.takeKeyboardFocus)
        }
    }
    function activate(a) {
        if (!a || root.busy[a.slug]) return
        if (root.pendingInstallOperation) {
            root.showInstallError(qsTr("An add-on install is already finishing. Try again shortly."))
            return
        }
        // An add-on with a setup page opens it in-app, installed or not: a bare install of
        // Comet/MediaFusion finds nothing, and an installed one is set up again from its card.
        if (a.configurable) { root.openSetup(a, null); return }
        if (root.isInstalled(a)) return
        root.setBusy(a.slug, true)
        if (!root.beginInstall(a.manifestUrl, {kind: "store", slug: a.slug, expectedId: a.id})) {
            root.setBusy(a.slug, false)
            return
        }
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

    function openConfiguration(extensionId) {
        root.replacingInstance = null
        root.configuringExtensionId = extensionId
    }
    function closeConfiguration() { root.replacingInstance = null; root.configuringExtensionId = "" }

    function instanceLabel(index) {
        var entry = root.installedEntries[index]
        if (!entry) return ""
        var name = String(entry.manifest && entry.manifest.name || "")
        var total = 0, ordinal = 0
        for (var i = 0; i < root.installedEntries.length; ++i) {
            var candidate = root.installedEntries[i]
            var candidateName = String(candidate.manifest && candidate.manifest.name || "")
            if (candidate.id === entry.id || (name.length && candidateName === name)) {
                total++
                if (i <= index) ordinal++
            }
        }
        return total > 1 ? qsTr("Instance %1").arg(ordinal) : ""
    }
    function canConfigure(entry) {
        if (entry.id === "colosseum.well.tankoyomi") return true
        var hints = entry.manifest ? entry.manifest.behaviorHints : null
        return !entry.core && /^https?:\/\//i.test(String(entry.transportUrl))
                && !!(hints && (hints.configurable || hints.configurationRequired))
    }
    function configureInstalled(entry) {
        if (!root.canConfigure(entry)) return
        if (entry.id === "colosseum.well.tankoyomi") {
            root.replacingInstance = null
            root.openConfiguration(entry.id)
            return
        }
        var replacement = { url: entry.transportUrl, id: entry.id, enabled: entry.enabled }
        root.openSetup({
            id: entry.id,
            name: String(entry.manifest.name || qsTr("Extension")),
            manifestUrl: entry.transportUrl,
            configureUrl: String(entry.transportUrl).replace(/\/manifest\.json(?=[?#]|$)/i, "/configure"),
            configurable: true,
            setupRequired: !!entry.manifest.behaviorHints.configurationRequired
        }, replacement)
    }
    function toggleInstalled(entry) {
        if (entry.core) return
        management.actionError = ""
        Extensions.setEnabledInstance(entry.transportUrl, !entry.enabled)
        var updated = Extensions.installed().find(function(e) { return e.transportUrl === entry.transportUrl })
        if (!updated || updated.enabled === entry.enabled)
            management.actionError = qsTr("Couldn't save the change. Try again.")
    }
    function removeInstalled(entry) {
        if (entry.core) return
        management.actionError = ""
        Extensions.removeInstance(entry.transportUrl)
        if (Extensions.installed().some(function(e) { return e.transportUrl === entry.transportUrl }))
            management.actionError = qsTr("Couldn't remove the add-on. Try again.")
    }

    function openUrlInstaller() {
        root.installUrlNormalized = ""
        root.installUrlManifest = null
        root.installUrlError = ""
        root.installUrlPreviewing = false
        root.installUrlInstalling = false
        root.installUrlComplete = false
        urlInput.text = ""
        root.urlInstallerGeneration++
        root.installUrlOpen = true
        Qt.callLater(function() { urlInput.forceActiveFocus() })
    }
    function closeUrlInstaller() {
        if (root.installUrlPreviewing || root.installUrlInstalling) return
        root.urlInstallerGeneration++
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
        if (!root.installUrlPreviewReady || root.installUrlInstalling || root.installUrlComplete) return
        if (Extensions.isInstalled(root.installUrlNormalized)) {
            root.installUrlError = "This add-on address is already installed."
            return
        }
        root.installUrlError = ""
        root.installUrlInstalling = true
        if (!root.beginInstall(root.installUrlNormalized, {
            kind: "url", expectedId: String(root.installUrlManifest.id || ""),
            uiGeneration: root.urlInstallerGeneration
        })) {
            root.installUrlInstalling = false
            return
        }
        Extensions.install(root.installUrlNormalized)
    }
    function configurePreviewedUrl() {
        if (!root.installUrlPreviewReady || !root.installUrlNormalized.length) return
        var url = root.installUrlNormalized
        root.installUrlOpen = false
        root.urlInstallerGeneration++
        root.openSetup({
            id: String(root.installUrlManifest.id || ""),
            name: String(root.installUrlManifest.name || "Extension"),
            manifestUrl: url,
            configureUrl: url.replace(/\/manifest\.json(?=[?#]|$)/i, "/configure"),
            configurable: true,
            setupRequired: true
        }, null)
    }
    function needsSetup(manifest) {
        return !!(manifest && manifest.behaviorHints && manifest.behaviorHints.configurationRequired)
    }
    function manifestResources(manifest) {
        var resources = manifest && manifest.resources ? manifest.resources : []
        var names = []
        var labels = {
            catalog: qsTr("Catalogs"),
            meta: qsTr("Details"),
            stream: qsTr("Streams"),
            subtitles: qsTr("Subtitles"),
            universe: qsTr("Universes"),
            universes: qsTr("Universes")
        }
        for (var i = 0; i < resources.length; i++) {
            var name = typeof resources[i] === "string" ? resources[i] : resources[i].name
            if (!name) continue
            var key = String(name).toLowerCase()
            var label = labels[key] || String(name)
                    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
                    .replace(/[_-]+/g, " ").replace(/^\s+|\s+$/g, "")
                    .replace(/\b\w/g, function(letter) { return letter.toUpperCase() })
            if (names.indexOf(label) < 0) names.push(label)
        }
        return names.length ? names.join(" · ") : "No resources declared"
    }

    function takeKeyboardFocus() {
        if (tankoyomiConfiguration.visible) tankoyomiConfiguration.takeKeyboardFocus()
        else if (management.visible) management.takeKeyboardFocus()
        else if (seeAll.visible) seeAll.takeKeyboardFocus()
        else storeWorld.forceActiveFocus()
    }
    // Main asks first: setup closes, then a settings page, then search / See all, then the world.
    function requestEscape() {
        if (urlTextMenu.opened) { urlTextMenu.close(); return true }
        if (root.installUrlOpen) { root.closeUrlInstaller(); return true }
        if (setup && setup.open) { root.closeSetup(); return true }
        if (root.configuringExtensionId.length) { root.replacingInstance = null; root.closeConfiguration(); return true }
        if (root.managingInstalled) return management.requestEscape()
        if (root.seeAllRow || root.searching) { root.seeAllRow = null; root.searching = false; return true }
        return false
    }

    Component.onCompleted: root.load()
    onConfiguringExtensionIdChanged: Qt.callLater(root.takeKeyboardFocus)

    Connections {
        target: typeof NamedCollectionsProfileScope !== "undefined" ? NamedCollectionsProfileScope : null
        function onStoresAboutToChange() {
            root.profileGeneration++
            root.setupGeneration++
            root.urlInstallerGeneration++
            root.installUrlOpen = false
            root.installUrlManifest = null
            root.installUrlNormalized = ""
            urlInput.text = ""
            root.installUrlPreviewing = false
            root.installUrlInstalling = false
            // ExtensionsStore invalidates pending manifest callbacks by profile generation.
            root.pendingInstallOperation = null
            root.replacingInstance = null
            root.busy = ({})
            if (root.setup) root.setup.addon = null
            management.actionError = ""
        }
    }

    Connections {
        target: (typeof Extensions !== "undefined") ? Extensions : null
        function onChanged() { root.revision++ }
        function onInstallFinished(id, name) { root.finishInstallOperation(id) }
        function onInstallFailed(url, reason) {
            var operation = root.pendingInstallOperation
            if (!operation || Extensions.normalizeUrl(url) !== operation.url) return
            root.pendingInstallOperation = null
            root.failInstallOperation(operation, qsTr("Couldn't install it: %1").arg(reason))
        }
        function onPreviewReady(url, manifest) {
            if (!root.installUrlOpen || !root.installUrlPreviewing
                    || url !== root.installUrlNormalized) return
            root.installUrlPreviewing = false
            root.installUrlManifest = manifest
        }
        function onPreviewFailed(url, reason) {
            if (!root.installUrlOpen || !root.installUrlPreviewing
                    || Extensions.normalizeUrl(url) !== root.installUrlNormalized) return
            root.installUrlPreviewing = false
            root.installUrlError = qsTr("Couldn't preview it: %1").arg(root.safeInstallMessage(reason))
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
        visible: !seeAll.visible && !tankoyomiConfiguration.visible && !management.visible
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

        ExtensionsInstalledSection {
            entries: root.installedEntries
            capabilitiesFor: root.manifestResources
            instanceLabelFor: root.instanceLabel
            onManageRequested: (index) => {
                management.initialIndex = index
                root.managingInstalled = true
            }
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

    ExtensionsInstalledPage {
        id: management
        objectName: "extensionsInstalledPage"
        anchors.fill: parent
        z: 10
        visible: root.managingInstalled && !tankoyomiConfiguration.visible
        entries: root.installedEntries
        capabilitiesFor: root.manifestResources
        instanceLabelFor: root.instanceLabel
        configurableFor: root.canConfigure
        onBackRequested: {
            root.managingInstalled = false
            Qt.callLater(root.takeKeyboardFocus)
        }
        onToggleRequested: (entry) => root.toggleInstalled(entry)
        onConfigureRequested: (entry) => root.configureInstalled(entry)
        onRemoveRequested: (entry) => root.removeInstalled(entry)
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
        function onCloseRequested() { root.closeSetup() }
        function onInstallRequested(url) {
            if (!root.setup || !root.setup.open) return
            root.setup.pendingUrl = Extensions.normalizeUrl(url)
            if (!root.setup.pendingUrl.length) {
                root.setup.status = qsTr("That isn't a valid add-on address.")
                return
            }
            if (root.replacingInstance && root.setup.pendingUrl !== root.replacingInstance.url
                    && Extensions.isInstalled(root.setup.pendingUrl)) {
                root.setup.status = qsTr("That configuration is already installed. Choose a different configuration.")
                return
            }
            var operation = {
                kind: "setup",
                expectedId: String(root.setup.addon.id || (root.replacingInstance ? root.replacingInstance.id : "")),
                setupGeneration: root.setupGeneration,
                replacingUrl: root.replacingInstance ? root.replacingInstance.url : "",
                replacingEnabled: root.replacingInstance ? root.replacingInstance.enabled : true
            }
            if (!root.beginInstall(root.setup.pendingUrl, operation)) return
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
            height: Math.min(parent.height - 24,
                             root.installUrlPreviewReady ? 488 : 358)
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
                        // Qt 6.9+ intercepts mouse, touch and keyboard context requests.
                        ContextMenu.menu: ExtensionsTextMenu {
                            id: urlTextMenu
                            editor: urlInput
                        }
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
                    visible: root.installUrlPreviewReady
                    width: parent.width; height: 132; radius: 16
                    color: Qt.rgba(1, 1, 1, 0.045)
                    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.12)
                    Row {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.top: parent.top; anchors.margins: 14
                        spacing: 13
                        AddonLogo {
                            id: installPreviewLogo
                            objectName: "extensionsInstallPreviewLogo"
                            size: 52
                            addonId: root.installUrlManifest ? String(root.installUrlManifest.id || "") : ""
                            addonName: root.installUrlManifest ? String(root.installUrlManifest.name || "") : ""
                            manifestLogo: root.installUrlManifest ? String(root.installUrlManifest.logo || "") : ""
                            preferManifestLogo: true
                            waitForArtworkResolution: true
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
                              : (root.installUrlPreviewing
                                 || (root.installUrlManifest !== null && !root.installUrlPreviewReady)) ? "Checking…"
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
                                     || (root.installUrlPreviewReady
                                         && !Extensions.isInstalled(root.installUrlNormalized))
                                     || (root.installUrlManifest === null
                                         && urlInput.text.trim().length > 0))
                        accessibleName: root.installUrlComplete ? "Done"
                                         : root.installUrlManifest !== null && !root.installUrlPreviewReady
                                           ? "Checking add-on artwork"
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
