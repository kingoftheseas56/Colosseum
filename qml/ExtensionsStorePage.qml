import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "ExtensionsStoreApi.js" as StoreApi

Item {
    id: root
    objectName: "extensionsStorePage"

    property Item shellWallpaper: null
    property bool showExplicit: false
    property string world: "theatre"
    property var tributes: []
    property bool reducedMotion: false
    signal tributeRequested(var tribute)
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal searchClicked()
    signal catalogueWorldRequested(string world)

    property var nuvioRepositoryStore: typeof NuvioRepos !== "undefined" ? NuvioRepos : null
    property real bottomInset: 24
    property var catalogue: StoreApi.withTorrentio([]) // every cleaned add-on, most starred first; the bundled Torrentio card is present while/if the catalogue cannot load
    property string loadError: ""
    property bool loading: true
    property int revision: 0                 // bumps on every Extensions change
    readonly property bool limitArtworkToViewport: {
        const w = root.Window.window
        return Qt.platform.os === "android" || !!(w && w.androidHost)
    }
    readonly property bool phoneLayout: {
        const w = root.Window.window
        return !!(w && w.androidHost && w.desktopPresentation !== true && w.width < 840)
    }
    property var busy: ({})                  // slug -> true while an install is in flight
    property string configuringExtensionId: ""   // a House source whose settings page is open
    property string categoryName: ""
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
    property bool pendingPlaySourcesBrowse: false
    property var pendingInstallOperation: null
    property var replacingInstance: null
    property int profileGeneration: 0
    property int setupGeneration: 0
    property int urlInstallerGeneration: 0
    readonly property var installedEntries: {
        root.revision
        return typeof Extensions !== "undefined" ? Extensions.installed() : []
    }
    readonly property var directoryEntries: root.catalogue.filter(function(a) {
        return StoreApi.visible(a, root.showExplicit)
    })
    readonly property var essentials: StoreApi.essentialsPreview(root.catalogue)
    readonly property var moreSources: StoreApi.curated(directoryEntries, StoreApi.MORE_SOURCES)
    readonly property var enrichment: StoreApi.curated(directoryEntries, StoreApi.ENRICHMENT)
    readonly property var searchPool: {
        const pinned = root.essentials.concat(root.moreSources, root.enrichment)
        return pinned.concat(root.directoryEntries.filter(function(a) {
            return !pinned.some(function(item) { return item.id === a.id || item.slug === a.slug })
        }))
    }
    readonly property var categories: StoreApi.supportedCategories(directoryEntries)
    readonly property var selectedCategory: root.categories.find(category => category.name === root.categoryName) || null
    readonly property var catalogues: StoreApi.catalogues().concat(enrichment.filter(function(a) {
        return a.title === "Streaming Catalogs"
    }))
    readonly property var rows: StoreApi.rows(root.catalogue, root.showExplicit)
    readonly property var setup: setupLoader.item

    Theme { id: theme }
    FontLoader {
        id: extensionsTitleFont
        source: "../assets/fonts/cinzel/Cinzel-Variable.ttf"
    }
    function rowArtworkActive(row) {
        if (!root.limitArtworkToViewport) return true
        if (!storeWorld.visible) return false
        const height = storeWorld.viewportHeight
        if (height <= 0) return true
        const top = storeWorld.viewportContentY
        row.y
        tierBoard.y
        const rowTop = row.mapToItem(storeWorld.pageFlickable.contentItem, 0, 0).y
        return rowTop + row.height >= top - height && rowTop <= top + height * 2
    }

    function isInstalled(a) {
        if (!a || typeof Extensions === "undefined") return false
        return (!!a.id && Extensions.isInstalled(a.id))
            || (!!a.manifestUrl && Extensions.isInstalled(a.manifestUrl))
    }
    function isDisabled(a) {
        if (!a || typeof Extensions === "undefined") return false
        var matches = root.installedEntries.filter(function(entry) {
            return entry.id === a.id || entry.transportUrl === a.manifestUrl
        })
        return matches.length > 0 && !matches.some(function(entry) { return entry.enabled === true })
    }
    function enableInstalledUrl(url) {
        if (root.pendingInstallOperation) return false
        var normalized = Extensions.normalizeUrl(url)
        var existing = Extensions.installed().find(function(entry) { return entry.transportUrl === normalized })
        if (!existing || existing.core) return false
        if (existing.enabled === true) return true
        Extensions.setEnabledInstance(normalized, true)
        var updated = Extensions.installed().find(function(entry) { return entry.transportUrl === normalized })
        return !!updated && updated.enabled === true
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
        // An add-on with a setup page opens it in-app, installed or not. A seeded,
        // disabled source can be enabled there without fetching its manifest again.
        if (a.configurable) { root.openSetup(a, null); return }
        if (root.isInstalled(a)) {
            if (root.isDisabled(a)) {
                var existing = root.installedEntries.find(function(entry) {
                    return entry.transportUrl === a.manifestUrl || entry.id === a.id
                })
                if (!existing || !root.enableInstalledUrl(existing.transportUrl))
                    root.showInstallError(qsTr("Couldn't enable the add-on. Try again in Your Add-ons."))
            }
            return
        }
        root.setBusy(a.slug, true)
        if (!root.beginInstall(a.manifestUrl, {kind: "store", slug: a.slug})) {
            root.setBusy(a.slug, false)
            return
        }
        Extensions.install(a.manifestUrl)
    }
    function essentialInstanceIndices(addon) {
        var indices = []
        root.installedEntries.forEach(function(entry, index) {
            if (!entry.core && (entry.id === addon.id || entry.transportUrl === addon.manifestUrl))
                indices.push(index)
        })
        return indices
    }
    function installEssential(addon) {
        if (addon.id === "colosseum.well.tankoyomi" || !addon.setupRequired)
            root.activate(Object.assign({}, addon, { configurable: false }))
        else
            root.activate(addon)
    }
    function configureEssential(addon) {
        if (addon.id === "colosseum.well.tankoyomi") {
            root.openConfiguration(addon.id)
            return
        }
        var indices = root.essentialInstanceIndices(addon)
        if (indices.length > 1) {
            management.initialIndex = indices[0]
            root.managingInstalled = true
        } else if (indices.length && root.canConfigure(root.installedEntries[indices[0]])) {
            root.configureInstalled(root.installedEntries[indices[0]])
        } else {
            root.openSetup(addon, null)
        }
    }
    function browseCatalogue(addon) {
        if (addon.world === "Tributes") {
            root.seeAllRow = { title: qsTr("Tributes"), items: root.tributes.map(function(tribute) {
                return { slug: tribute.extensionId, title: tribute.name, logo: tribute.logo,
                         description: "", world: "Tribute", tribute: tribute }
            }) }
        } else if (addon.world === "Tribute") {
            root.tributeRequested(addon.tribute)
        } else {
            root.catalogueWorldRequested(addon.world)
        }
    }
    function openSuggestedStreamSource() {
        // The empty Sources sheet should reach the actual first-run action even
        // when the community catalogue is slow or unavailable.
        var torrentio = root.essentials.find(function(a) { return a.name === "Torrentio" })
        var installed = root.installedEntries.find(function(entry) {
            return entry.id === "com.stremio.torrentio.addon"
                    && entry.transportUrl === "https://torrentio.strem.fun/manifest.json"
        })
        var addon = torrentio || {
            id: "com.stremio.torrentio.addon", name: "Torrentio",
            manifestUrl: installed ? installed.transportUrl : "https://torrentio.strem.fun/manifest.json",
            configureUrl: "https://torrentio.strem.fun/configure",
            configurable: true, setupRequired: false
        }
        root.managingInstalled = false
        root.categoryName = ""
        root.seeAllRow = null
        root.searching = false
        root.openSetup(addon, null)
    }
    function openPlaySources() {
        root.managingInstalled = false
        root.categoryName = ""
        root.searching = false
        root.seeAllRow = null
        root.pendingPlaySourcesBrowse = true
        root.finishPlaySourcesBrowse()
    }
    function finishPlaySourcesBrowse() {
        if (!root.pendingPlaySourcesBrowse || root.loading) return
        root.pendingPlaySourcesBrowse = false
        root.seeAllRow = root.rows.find(function(row) { return row.title === "Play sources" }) || null
    }
    function load() {
        root.loading = true
        root.loadError = ""
        StoreApi.fetchAll(function(all, error) {
            root.catalogue = all
            root.loadError = error
            root.loading = false
            root.finishPlaySourcesBrowse()
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
        if (Extensions.installed().some(function(e) { return e.transportUrl === entry.transportUrl })) {
            const message = qsTr("Couldn't remove the add-on. Try again.")
            if (root.managingInstalled) management.actionError = message
            else root.showInstallError(message)
        }
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
            subtitles: qsTr("Subtitles"),
            universe: qsTr("Universes"),
            universes: qsTr("Universes")
        }
        for (var i = 0; i < resources.length; i++) {
            var name = typeof resources[i] === "string" ? resources[i] : resources[i] && resources[i].name
            if (!name) continue
            var key = String(name).toLowerCase()
            var label = key === "stream" ? root.sourceLabel(manifest, resources[i]) : labels[key]
            if (!label) {
                label = String(name).replace(/([a-z0-9])([A-Z])/g, "$1 $2")
                    .replace(/[_-]+/g, " ").replace(/^\s+|\s+$/g, "")
                    .replace(/\b\w/g, function(letter) { return letter.toUpperCase() })
            }
            if (names.indexOf(label) < 0) names.push(label)
        }
        return names.length ? names.join(" · ") : "No resources declared"
    }

    function sourceLabel(manifest, resource) {
        const bundledSource = root.essentials.find(addon => addon.id === manifest.id
                                                        && addon.id.startsWith("colosseum.well."))
        if (bundledSource)
            return bundledSource.description

        const types = resource && resource.types && resource.types.length
            ? resource.types : manifest.types || []
        const labels = {
            manga: qsTr("Manga sources"),
            comic: qsTr("Comic sources"),
            book: qsTr("Book sources"),
            audiobook: qsTr("Audiobook sources")
        }
        const descriptions = []
        for (const type of types) {
            const label = labels[type]
            if (typeof label === "string" && !descriptions.includes(label))
                descriptions.push(label)
        }
        if (types.some(type => type === "movie" || type === "series" || type === "anime"))
            descriptions.unshift(qsTr("Playback sources"))
        return descriptions.length ? descriptions.join(" · ") : qsTr("Playback sources")
    }

    function takeKeyboardFocus() {
        if (tankoyomiConfiguration.visible) tankoyomiConfiguration.takeKeyboardFocus()
        else if (management.visible) management.takeKeyboardFocus()
        else if (categoryLoader.visible && categoryLoader.status === Loader.Ready)
            categoryLoader.item.takeKeyboardFocus()
        else if (seeAll.visible) seeAll.takeKeyboardFocus()
        else storeWorld.forceActiveFocus()
    }
    // Main asks first: setup closes, then a settings page, then search / See all, then the world.
    function requestEscape() {
        if (urlTextMenu.opened) { urlTextMenu.close(); return true }
        if (root.installUrlOpen) { root.closeUrlInstaller(); return true }
        if (setup && setup.open) { root.closeSetup(); return true }
        if (root.configuringExtensionId.length) { root.replacingInstance = null; root.closeConfiguration(); return true }
        if (management.dismissRemoval()) return true
        if (nuvioDrawer.visible) { nuvioDrawer.close(); return true }
        if (root.managingInstalled) return management.requestEscape()
        if (root.categoryName.length) {
            root.categoryName = ""
            Qt.callLater(root.takeKeyboardFocus)
            return true
        }
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
    Connections {
        target: typeof Extensions !== "undefined" ? Extensions : null
        ignoreUnknownSignals: true
        function onNuvioRepositoryPreviewed(url) {
            if (!root.installUrlOpen || !root.installUrlPreviewing || url !== root.installUrlNormalized)
                return
            root.installUrlPreviewing = false
            if (!root.nuvioRepositoryStore) {
                root.installUrlError = qsTr("Nuvio plugins are unavailable on this device.")
                return
            }
            root.closeUrlInstaller()
            nuvioDrawer.open()
            Qt.callLater(nuvioBar.offerLink, url)
        }
    }

    // A direct install that fails says so (it used to clear the busy badge and nothing else).
    property string installError: ""
    Timer { id: installErrorTimer; interval: 6000; onTriggered: root.installError = "" }

    Rectangle {
        id: storeBackdrop
        objectName: "extensionsBackdrop"
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#22232a" }
            GradientStop { position: 0.52; color: "#16171c" }
            GradientStop { position: 1; color: "#0b0d12" }
        }

        Loader {
            objectName: "extensionsWallpaperBlur"
            anchors.fill: parent
            active: root.visible && !categoryLoader.visible && root.shellWallpaper !== null
                && root.GraphicsInfo.api !== GraphicsInfo.Software
            sourceComponent: Glass {
                backdrop: root.shellWallpaper
                radius: 0
                blurAmount: 0.85
                tint: 0
                edge: "transparent"
                scrim: 0.58
            }
        }
    }

    WorldPage {
        id: storeWorld
        objectName: "extensionsStoreWorld"
        androidHost: root.phoneLayout
        anchors.fill: parent
        visible: !categoryLoader.visible && !seeAll.visible
            && !tankoyomiConfiguration.visible && !management.visible
        medium: "Extensions"
        prominentScrollBar: true
        contentBottomInset: root.bottomInset
        topBarBackOnly: true
        topBarTitle: qsTr("Extensions")
        topBarTitleFont: extensionsTitleFont.name
        onVisibleChanged: if (!visible) nuvioDrawer.close()
        backdrop: storeBackdrop
        onHomeRequested: root.backRequested()

        Column {
            id: header
            width: parent.width
            spacing: 12

            FeaturedCarousel {
                width: parent.width
                height: Math.round(Math.max(width < 520 ? 160 : 236, Math.min(320, width * 0.24)))
                visible: slides.length > 0
                automationId: "extensionsTributeCarousel"
                androidHost: root.phoneLayout
                artworkOnly: true
                autoAdvance: !root.reducedMotion
                kicker: qsTr("Tributes")
                slides: root.tributes.map(tribute => ({
                    id: tribute.extensionId,
                    title: tribute.name,
                    art: tribute.extensionId === "com.colosseum.universe.cosmere"
                        ? Qt.resolvedUrl("../assets/extensions/house/cosmere.jpg") : tribute.banner
                }))
                onPrimaryClicked: index => {
                    const tribute = root.tributes[index]
                    if (tribute) root.tributeRequested(tribute)
                }
            }

            Item {
                id: headerActions
                width: parent.width
                height: nuvioTrigger.visible ? nuvioTrigger.height : installUrlButton.height

                KeyboardAction {
                    id: nuvioTrigger
                    objectName: "extensionsNuvioRepositoriesButton"
                    visible: root.nuvioRepositoryStore !== null
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, Math.min(272, parent.width - installUrlButton.width - 12))
                    height: 60
                    readonly property bool compact: width < 222
                    accessibleName: nuvioDrawer.visible ? qsTr("Hide Nuvio repositories") : qsTr("Show Nuvio repositories")
                    onTriggered: if (nuvioDrawer.visible) nuvioDrawer.close(); else nuvioDrawer.open()

                    Rectangle {
                        anchors.fill: parent
                        radius: 16
                        color: nuvioTrigger.interactionActive ? "#cf202839" : "#b8141923"
                        border.width: 1
                        border.color: nuvioTrigger.interactionActive ? theme.gold : "#8f829bc2"
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        spacing: 9
                        Image {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 32
                            source: "../assets/icons/nuvio-official.png"
                            sourceSize.width: 68
                            fillMode: Image.PreserveAspectFit
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: nuvioTrigger.compact ? qsTr("Nuvio") : qsTr("Nuvio repositories")
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: theme.fontSize(15)
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: !nuvioTrigger.compact
                                text: qsTr("Add a repository link")
                                color: "#b2c8ec"
                                font.family: theme.ui
                                font.pixelSize: theme.fontSize(12)
                            }
                        }
                        Image {
                            Layout.preferredWidth: 15
                            Layout.preferredHeight: 15
                            source: "../assets/icons/lucide/chevron-right.svg"
                            sourceSize.width: 30
                            rotation: nuvioDrawer.visible ? -90 : 90
                        }
                    }
                }

                ExtensionsActionButton {
                    id: installUrlButton
                    objectName: "extensionsInstallUrlButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 44
                    text: qsTr("Install from URL")
                    onClicked: root.openUrlInstaller()
                }
            }
        }

        Column {
            id: tierBoard
            x: root.width < 700 ? sectionIndex.closedWidth + 12
               : (sectionIndex.collapsed ? sectionIndex.closedWidth : sectionIndex.openWidth) + 28
            width: parent.width - x
            spacing: 28

            ExtensionGrid {
                id: essentialsRail
                title: qsTr("Essentials")
                automationId: "extensionsEssentialsRail"
                items: root.essentials
            }
            ExtensionGrid {
                id: moreSourcesRail
                title: qsTr("More Sources")
                items: root.moreSources
            }
            ExtensionGrid {
                id: enrichmentRail
                title: qsTr("Enrichment")
                items: root.enrichment
            }
            ExtensionGrid {
                id: cataloguesRail
                title: qsTr("Catalogues")
                items: root.catalogues
            }
            GenreMosaic {
                objectName: "extensionsCategoryMosaic"
                visible: genres.length > 0
                title: qsTr("Categories")
                genres: root.categories
                columns: Math.max(1, Math.min(5, Math.floor(width / 158)))
                navigable: false
                onGenreClicked: index => root.categoryName = root.categories[index].name
            }
        }

        RowIndexSidebar {
            id: sectionIndex
            parent: storeWorld.pageFlickable.contentItem
            z: 2
            x: storeWorld.outerPageMargin
            pageFlick: storeWorld.pageFlickable
            flowHost: tierBoard
            worldName: qsTr("Extensions")
            contextLabel: ""
            automationPrefix: "extensionsRowIndex"
            currentOffset: storeWorld.parkGap
            rows: [
                { key: "essentials", title: essentialsRail.title, target: essentialsRail, iconSource: "../assets/icons/star.svg" },
                { key: "sources", title: moreSourcesRail.title, target: moreSourcesRail, iconSource: "../assets/icons/play.svg" },
                { key: "enrichment", title: enrichmentRail.title, target: enrichmentRail, iconSource: "../assets/icons/extensions.svg" },
                { key: "catalogues", title: cataloguesRail.title, target: cataloguesRail, iconSource: "../assets/icons/book-library.svg" }
            ]
            onRowRequested: target => storeWorld.parkRow(target)

            Rectangle {
                anchors.fill: parent
                z: -1
                visible: root.width < 700 && !sectionIndex.collapsed
                color: "#f217181e"
                radius: 14
            }
        }

        RowLayout {
            objectName: "extensionsDataSourceCredits"
            visible: root.nuvioRepositoryStore !== null
            width: parent.width
            spacing: 12

            Image {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                source: "../assets/addon-logos/tmdb.png"
                sourceSize.width: 80
                sourceSize.height: 80
                fillMode: Image.PreserveAspectFit
                Accessible.name: qsTr("TMDB")
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: qsTr("Data source credits")
                    color: theme.ink
                    font.family: theme.ui
                    font.weight: Font.DemiBold
                    font.pixelSize: 13
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("This product uses the TMDB API but is not endorsed or certified by TMDB.")
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }

        Text {
            visible: root.loading || root.loadError.length > 0
            width: parent.width
            text: root.loading ? qsTr("Loading community add-ons…")
                               : qsTr("Couldn’t load community add-ons. Try again later.")
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }

        Text {
            visible: root.catalogue.length > 0
            width: parent.width
            text: qsTr("Community directory by Stremio Addons")
            color: Qt.rgba(1, 1, 1, 0.34)
            font.family: theme.ui; font.pixelSize: 12
        }
    }

    component ExtensionGrid: ExtensionsStoreRail {
        id: rail
        essentialsLayout: true
        canSeeAll: false
        limitArtworkToViewport: root.limitArtworkToViewport
        artworkActive: root.rowArtworkActive(rail)
        installedFn: root.isInstalled
        disabledFn: root.isDisabled
        busy: root.busy
        revision: root.revision
        onAddonActivated: addon => root.installEssential(addon)
        onAddonConfigured: addon => root.configureEssential(addon)
        onAddonRemoved: addon => management.requestRemoval(root.essentialInstanceIndices(addon))
        onCatalogueRequested: addon => root.browseCatalogue(addon)
    }

    Popup {
        id: nuvioDrawer
        objectName: "extensionsNuvioRepositoryDrawer"
        parent: root
        readonly property real viewportTop: storeWorld.pageFlickable.y + 8
        readonly property real viewportBottom: root.height - root.bottomInset
        readonly property real triggerBottom: storeWorld.pageFlickable.y + header.y
            + headerActions.y + nuvioTrigger.y + nuvioTrigger.height - storeWorld.viewportContentY
        readonly property real anchorY: Math.max(viewportTop, triggerBottom + 12)
        x: storeWorld.outerPageMargin
        y: Math.max(viewportTop, Math.min(anchorY, viewportBottom - height))
        width: Math.max(0, Math.min(640, root.width - 2 * storeWorld.outerPageMargin))
        height: Math.min(nuvioBar.implicitHeight, Math.max(160, viewportBottom - anchorY),
                         Math.max(0, viewportBottom - viewportTop))
        padding: 0
        focus: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        background: Glass {
            backdrop: storeBackdrop
            radius: 18
            scrim: 0.56
        }
        onOpened: Qt.callLater(nuvioBar.focusAddress)
        onClosed: {
            if (storeWorld.visible && !root.installUrlOpen) {
                if (triggerBottom > viewportTop && triggerBottom <= viewportBottom)
                    nuvioTrigger.forceActiveFocus(Qt.TabFocusReason)
                else
                    root.takeKeyboardFocus()
            }
        }
        contentItem: Flickable {
            id: repoScroll
            contentWidth: width
            contentHeight: nuvioBar.height
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar {
                flick: repoScroll
                prominent: true
            }

            NuvioRepositoriesBar {
                id: nuvioBar
                objectName: "nuvioRepositoriesBar"
                width: repoScroll.width
                store: root.nuvioRepositoryStore
                onCloseRequested: nuvioDrawer.close()
            }
        }
    }

    Loader {
        id: categoryLoader
        anchors.fill: parent
        z: 10
        active: root.selectedCategory !== null
        visible: active && !root.managingInstalled && !tankoyomiConfiguration.visible
        onLoaded: Qt.callLater(root.takeKeyboardFocus)
        onVisibleChanged: if (visible) Qt.callLater(root.takeKeyboardFocus)
        sourceComponent: GenrePage {
            id: categoryPage
            objectName: "extensionsCategoryPage"
            backdrop: root.shellWallpaper
            bottomInset: root.bottomInset
            genreName: root.categoryName
            genreTitle: genreName.charAt(0).toUpperCase() + genreName.slice(1)
            genreContext: qsTr("EXTENSIONS · ")
            genreKind: qsTr("CATEGORY")
            siblingGenres: root.categories.map(category => category.name)
            filterPlaceholder: qsTr("Search this category")
            readonly property var categoryAddons: root.selectedCategory ? root.selectedCategory.all : []
            readonly property var results: filterText.trim().length
                ? StoreApi.search(categoryAddons, filterText) : categoryAddons
            readonly property var artwork: {
                var logos = []
                for (const addon of categoryAddons) {
                    if (addon.logo && !logos.includes(addon.logo))
                        logos.push(addon.logo)
                    if (logos.length === 6) break
                }
                return logos
            }
            genreData: ({ count: results.length, desc: "", cards: results, montage: artwork })
            catalogSummary: qsTr("%n add-on(s)", "", results.length)
            loading: false
            resultDelegate: ExtensionsStoreCard {
                id: categoryCard
                property alias card: categoryCard.addon
                essentialsLayout: true
                installed: { root.revision; return root.isInstalled(addon) }
                disabled: { root.revision; return root.isDisabled(addon) }
                busy: root.busy[addon.slug] === true
                onActivated: root.installEssential(addon)
                onConfigureRequested: root.configureEssential(addon)
                onRemoveRequested: management.requestRemoval(root.essentialInstanceIndices(addon))
            }
            onGenreRequested: name => root.categoryName = name
            onBackRequested: {
                root.categoryName = ""
                Qt.callLater(root.takeKeyboardFocus)
            }
            onMinimizeRequested: root.minimizeRequested()
            onFullscreenRequested: root.fullscreenRequested()
            onCloseRequested: root.closeRequested()
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
        disabledFn: root.isDisabled
        busy: root.busy
        revision: root.revision
        bottomInset: root.bottomInset
        onAddonActivated: addon => root.installEssential(addon)
        onAddonConfigured: addon => root.configureEssential(addon)
        onAddonRemoved: addon => management.requestRemoval(root.essentialInstanceIndices(addon))
        onCatalogueRequested: addon => root.browseCatalogue(addon)
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
        titleFont: extensionsTitleFont.name
        bottomInset: root.bottomInset
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

    // Keep the existing Back-only Store masthead, but restore the house
    // Minimize / Fullscreen / Quit trio on Store, Search and Your Add-ons.
    Row {
        anchors.right: extensionsWindowControls.left
        anchors.rightMargin: extensionsWindowControls.visible ? 24 : 0
        anchors.verticalCenter: extensionsWindowControls.verticalCenter
        z: 15
        visible: storeWorld.visible
        spacing: 10

        KeyboardAction {
            id: installedButton
            objectName: "extensionsInstalledButton"
            width: installedLabel.implicitWidth + 30
            height: 44
            accessibleName: qsTr("Installed add-ons")
            onTriggered: {
                management.initialIndex = 0
                root.managingInstalled = true
            }
            Rectangle {
                anchors.fill: parent
                radius: 12
                color: installedButton.interactionActive ? theme.glassHi : theme.glassTint
                border.width: 1
                border.color: installedButton.interactionActive ? theme.gold : theme.edge
            }
            Text {
                id: installedLabel
                anchors.centerIn: parent
                text: qsTr("Installed")
                font.family: extensionsTitleFont.name || theme.display
                font.pixelSize: theme.fontSize(17)
                font.weight: Font.DemiBold
                color: installedButton.interactionActive ? theme.gold : theme.ink
            }
        }
        KeyboardAction {
            id: storeSearch
            objectName: "extensionsSearchButton"
            width: 44
            height: 44
            accessibleName: qsTr("Search add-ons")
            onTriggered: root.searching = true
            Image {
                anchors.centerIn: parent
                width: 22
                height: 22
                sourceSize.width: 44
                sourceSize.height: 44
                source: "../assets/icons/search.svg"
                opacity: storeSearch.interactionActive ? 1 : 0.74
            }
        }
    }
    HouseWindowControls {
        id: extensionsWindowControls
        objectPrefix: "extensionsWindow"
        anchors.right: parent.right; anchors.rightMargin: theme.margin
        y: 46
        z: 15
        visible: Qt.platform.os !== "android" && !tankoyomiConfiguration.visible && !categoryLoader.visible
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onQuitRequested: root.closeRequested()
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
        function onEnableRequested(url) {
            if (!root.setup || !root.setup.open) return
            if (root.enableInstalledUrl(url)) root.closeSetup()
            else root.setup.status = qsTr("Couldn't enable the add-on. Try again in Your Add-ons.")
        }
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
                // Catalogue IDs can lag the live manifest (Comet does). Only a
                // deliberate replacement must retain its installed identity.
                expectedId: root.replacingInstance ? String(root.replacingInstance.id) : "",
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
        backdrop: storeBackdrop
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
