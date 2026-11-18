// Desktop Vault library. Native services own scanning, identity and durable progress.
import QtQuick
import QtQuick.Controls
import QtCore
import "VaultLibraryData.js" as Data

FocusScope {
    id: root
    objectName: "vaultPage"
    property Item backdrop: null
    property real desktopDockInset: 0
    property var adapter: ownedAdapter
    property var sidebarSettings: ownedSidebarSettings
    property string currentView: "home"
    property string currentServer: ""
    property bool providerConnectionOpen: false
    property string connectionProvider: "jellyfin"
    property Item connectionInvoker: null
    property string notice: ""
    readonly property int itemCount: adapter.allItems.length
    readonly property string vaultState: adapter.busy ? "scanning" : adapter.empty ? "empty" : "populated"
    readonly property bool cardVisible: admission.cardVisible
    readonly property bool sourcesOpen: sources.open
    readonly property bool searchViewActive: bar.query.trim().length > 0
    readonly property var searchRows: adapter.search(bar.query, "all", currentView === "server" ? currentServer : "")
    readonly property var selectedServer: adapter.providers.find(s => s.id === currentServer) || ({})
    signal backRequested()
    signal addFolderRequested()
    signal openMediaRequested(string path)
    signal viewWorldRequested(var identity)
    signal connectServerRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    Theme { id: theme }
    Settings {
        id: ownedSidebarSettings
        category: "vaultBrowseV1"
        property bool railExpanded: true
    }
    VaultLibraryAdapter { id: ownedAdapter }
    function showView(view, serverId) {
        currentView = ({ movie: "movies", show: "shows", book: "books", comic: "comics" })[view] || view
        currentServer = view === "server" ? root.adapter.providerKey(serverId || "") : (serverId || "")
        bar.query = ""
        viewReveal.restart()
        Qt.callLater(function() { var page = content.item as Item; if (page) page.forceActiveFocus(Qt.OtherFocusReason) })
    }
    function selectProvider(provider) {
        const entry = root.adapter.providers.find(row => row.id === provider)
        if (!entry) return
        if (entry.connected) root.showView("server", provider)
        else root.openProviderConnection(provider)
    }
    function openProviderConnection(provider) {
        if (["jellyfin", "emby", "plex"].indexOf(provider) < 0) return
        root.connectionInvoker = root.Window.window ? root.Window.window.activeFocusItem : null
        root.connectionProvider = provider
        root.providerConnectionOpen = true
    }
    function providerConnected(id) {
        const service = root.adapter.homeServers
        const connection = service ? (service.connections || []).find(row => row.id === id) : null
        if (!connection) return
        root.adapter.refresh()
        root.showView("server", connection.provider)
    }
    Connections {
        target: root.adapter
        function onProvidersChanged() {
            if (root.currentView === "server" && !root.selectedServer.connected) root.showView("home", "")
        }
    }
    function openSearch() { bar.focusSearch() }
    function leaveSearchView() { bar.query = "" }
    function handleBack() {
        if (root.providerConnectionOpen && providerConnection.item) { providerConnection.item.close(); return }
        if (extrasDialog.opened) { extrasDialog.close(); return }
        if (admission.handleBack()) return
        if (identifyDialog.opened) { identifyDialog.close(); return }
        if (sources.open) { sources.close(); return }
        if (bar.query.length) { bar.query = ""; return }
        if (currentView !== "home") { showView("home", ""); return }
        backRequested()
    }
    function openRow(row) {
        if (!row) return
        if (row.offline) { flash(row.serverId ? "Reconnect this server in the Sync Center." : "Drive not connected. Reconnect it to open this title."); return }
        if (row.serverId) {
            if (!adapter.homeServers || row.profileGeneration !== adapter.homeServers.profileGeneration) { flash("The server connection changed. Choose the title again."); adapter.refresh(); return }
            if (row.identity && row.identity.identityId) viewWorldRequested(row.identity)
            return
        }
        if (row.kind === "book" || row.kind === "comic") {
            if (row.resumePath && row.resumeOffline) { flash("Drive not connected. Reconnect it to open this title."); return }
            var path = row.resumePath || row.path
            if (path) openMediaRequested(path)
            else flash("No readable copy is available. Reconnect its source.")
        } else if (row.identity && row.identity.identityId) viewWorldRequested(row.identity)
        else matchRow(row)
    }
    function resumeRow(row) {
        if (!row || row.offline) { openRow(row); return }
        if (!row.serverId && row.resumeOffline) {
            flash("Drive not connected. Reconnect it to open this title."); return
        }
        if (row.serverId && (!adapter.homeServers || row.profileGeneration !== adapter.homeServers.profileGeneration)) {
            flash("The server connection changed. Choose the title again."); adapter.refresh(); return
        }
        if (!row.serverId && (row.resumePath || row.path)) {
            openMediaRequested(row.resumePath || row.path)
            return
        }
        if (row.serverId && row.progressRecord) {
            var identity = Object.assign({}, row.identity)
            var progress = row.progressRecord
            identity.resumeSources = { id: progress.id, title: row.title,
                type: row.kind === "show" ? "series" : "movie",
                context: Object.assign(Data.canonicalContext(progress.id), { title: row.title,
                    backdrop: row.backdrop || row.cover || "",
                    resumePosition: Number((progress.resume || {}).position) || 0 }) }
            viewWorldRequested(identity)
        } else openRow(row)
    }
    // Collections still address an exact Vault group, but the destination is now Theatre.
    function openDetailSheet(request) {
        var row = adapter.allItems.find(r => r.seriesKey === request.key || (r.copies || []).some(c => c.key === request.key))
        if (row) openRow(row)
        else { showView("match", ""); flash("This title needs a match before it can open in Theatre.") }
    }
    function matchRow(row) {
        identifyDialog.groupKey = row.seriesKey || row.key || ""
        var raw = row.payload || ({})
        var filePath = String(row.path || "")
        if (!filePath && row.items) {
            for (var i = 0; i < row.items.length; ++i) {
                if (row.items[i].path) { filePath = String(row.items[i].path); break }
            }
        }
        var fileName = filePath.split(/[\\/]/).pop() || ""
        var fileStem = fileName.replace(/\.[^.]+$/, "")
        identifyDialog.titleText = fileStem || raw.title || raw.displayTitle || row.matchTitle
            || String(row.seriesKey || "").split(/[\\/]/).pop()
        identifyDialog.kind = row.nativeKind || (row.kind === "book" ? "book" : row.kind === "comic" ? "comic" : "video")
        identifyDialog.embeddedIdentity = row.kind === "book" && row.items && row.items.length ? row.items[0] : ({})
        identifyDialog.feedback = ""
        identifyDialog.open()
    }
    function flash(text) { notice = text; noticeTimer.restart() }
    function fileExtras(row) { extrasDialog.sourceRow = row; extrasDialog.show = null; extrasDialog.open() }
    function showExtras(show) { extrasDialog.sourceRow = null; extrasDialog.show = show; extrasDialog.open() }
    function showsWithSupplementalFiles() {
        const result = []
        const shows = root.adapter.shows || []
        for (let i = 0; i < shows.length; ++i) {
            const show = shows[i]
            const count = Number(show.supplementalCount) || 0
            if (count > 0) result.push({ show: show, count: count })
        }
        return result
    }
    function activateExtras(item) {
        if (extrasDialog.sourceRow) {
            if (root.adapter.attachExtras(extrasDialog.sourceRow, item)) {
                extrasDialog.sourceRow = null
                extrasDialog.show = item
            } else root.flash("These files could not be filed as extras.")
        } else if (item.path) root.openMediaRequested(item.path)
    }
    Keys.onEscapePressed: event => { handleBack(); event.accepted = true }
    Component.onCompleted: { forceActiveFocus(); Qt.callLater(admission.initialize) }
    // Capture a non-recursive page surface for the drawer’s backdrop blur.
    Item {
    id: pageSurface; anchors.fill: parent
    Rectangle { anchors.fill: parent; color: theme.vaultBackdrop }
    ShaderEffectSource { anchors.fill: parent; sourceItem: root.backdrop; live: true; hideSource: false }
    Rectangle { anchors.fill: parent; gradient: Gradient { GradientStop { position: 0; color: theme.vaultVeilTop } GradientStop { position: 1; color: theme.vaultVeilBottom } } }
    MouseArea { anchors.fill: parent }
    Loader {
        id: content
        objectName: "vaultLibraryContent"
        x: sidebar.x + sidebar.width + (theme.vaultContentLeft - theme.vaultSidebarInset - theme.vaultSidebarWidth); y: theme.vaultSidebarTop
        width: Math.max(240, root.width - x - theme.vaultContentRight)
        height: Math.max(100, root.height - y - 24)
        sourceComponent: root.searchViewActive ? searchComponent : root.adapter.empty ? emptyComponent : root.currentView === "home" ? homeComponent : root.currentView === "server" ? serverComponent : root.currentView === "match" || root.currentView === "hidden" ? listComponent : kindComponent
        NumberAnimation { id: viewReveal; target: content; property: "opacity"; from: 0; to: 1; duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic }
    }
    VaultLibrarySidebar {
        id: sidebar
        x: theme.vaultSidebarInset; y: bar.y + bar.height
        height: Math.max(0, root.height - y - (root.desktopDockInset > 0 ? root.desktopDockInset + 8 : 4))
        expanded: root.sidebarSettings.railExpanded
        backdrop: root.backdrop; counts: root.adapter.counts; servers: root.adapter.providers; currentView: root.currentView; currentServer: root.currentServer
        unmatchedCount: root.adapter.unmatchedItems.length; status: root.adapter.status; busy: root.adapter.busy
        onSelected: (view, serverId) => view === "server" ? root.selectProvider(serverId) : root.showView(view, serverId)
        onSourcesRequested: sources.show(sidebar)
        onAddFolderRequested: root.addFolderRequested()
        onToggleRequested: root.sidebarSettings.railExpanded = !root.sidebarSettings.railExpanded
        onRescanRequested: { if (!root.adapter.rescan()) sources.show(sidebar) }
    }
    VaultLibraryBar {
        id: bar; objectName: "vaultHeader"
        x: 16; y: 12; width: parent.width - 32; z: 10
        backdrop: root.backdrop; contentBackdrop: content
        scrollOffset: (content.item as Flickable) ? (content.item as Flickable).contentY : 0
        onBackRequested: root.handleBack()
        onMinimizeRequested: root.minimizeRequested()
        onFullscreenRequested: root.fullscreenRequested()
        onCloseRequested: root.closeRequested()
        onAccepted: if (root.searchRows.length) root.openRow(root.searchRows[0])
    }
    }
    VaultStorageSheet {
        id: sources; anchors.fill: parent; z: 20; backdrop: pageSurface; adapter: root.adapter
        onAddFolderRequested: { sources.close(); root.addFolderRequested() }
        onConnectServerRequested: root.connectServerRequested()
    }
    Loader {
        id: providerConnection
        objectName: "vaultProviderConnectionLoader"
        active: root.providerConnectionOpen
        sourceComponent: HomeServerConnectPanel {
            service: root.adapter.homeServers
            initialProvider: root.connectionProvider
            invoker: root.connectionInvoker
            onConnected: id => root.providerConnected(id)
            onDismissed: root.providerConnectionOpen = false
            Component.onCompleted: open()
        }
    }
    VaultFolderAdmission { id: admission; anchors.fill: parent; z: 25; library: root.adapter.library || null }
    VaultIdentifyDialog {
        id: identifyDialog; anchors.centerIn: parent; z: 30
        onIdentityChosen: (key, identity) => {
            if (root.adapter.identify({ seriesKey: key }, identity)) close()
            else feedback = "That match could not be applied. Your files have not moved."
        }
    }
    Dialog {
        id: extrasDialog; objectName: "vaultExtrasDialog"
        property var sourceRow: null
        property var show: null
        readonly property var supplemental: show ? root.adapter.extrasForShow(show) : []
        anchors.centerIn: parent; width: Math.min(560, root.width - 48); height: Math.min(440, root.height - 48)
        modal: true; title: sourceRow ? "File as extras under a show" : "Supplemental files · " + (show ? show.title : "")
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent; contentWidth: availableWidth
            Column {
                width: parent.width; spacing: 10
                Text { width: parent.width; visible: extrasDialog.sourceRow && !root.adapter.shows.length; text: "Match a show first to file extras under it."; wrapMode: Text.Wrap; color: theme.ink }
                Repeater {
                    model: extrasDialog.sourceRow ? root.adapter.shows : extrasDialog.supplemental
                    delegate: VaultLibraryButton {
                        required property var modelData
                        width: parent.width
                        text: extrasDialog.sourceRow ? modelData.title : (modelData.title || modelData.path) + (modelData.away ? " · Drive not connected" : "")
                        enabled: !!extrasDialog.sourceRow || !modelData.away
                        onClicked: root.activateExtras(modelData)
                    }
                }
                Text { visible: !!extrasDialog.show && !extrasDialog.supplemental.length; text: "No supplemental files filed here."; color: theme.inkDim }
            }
        }
    }
    Rectangle {
        visible: root.notice.length > 0; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 24; z: 40
        width: Math.min(parent.width - 48, noticeText.implicitWidth + 32); height: noticeText.implicitHeight + 24; radius: 12; color: theme.vaultSurface; border.color: theme.edge
        Text { id: noticeText; anchors.fill: parent; anchors.margins: 12; text: root.notice; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.vaultBody; wrapMode: Text.Wrap }
    }
    Timer { id: noticeTimer; interval: 5000; onTriggered: root.notice = "" }
    Component {
        id: homeComponent
        VaultHomeView {
            heroRows: root.adapter.heroItems; continueRows: root.adapter.continueItems
            kindRows: ({ movie: root.adapter.movies, show: root.adapter.shows, book: root.adapter.books, comic: root.adapter.comics })
            onActivated: row => root.openRow(row)
            onDetailsRequested: row => root.openRow(row)
            onResumeRequested: row => root.resumeRow(row)
            onShowKind: kind => root.showView(kind, "")
            onHideRequested: row => root.adapter.hide(row)
            footer: Component {
                Column {
                    visible: root.adapter.unmatchedItems.length > 0; spacing: 12
                    Text { text: "Unmatched"; color: theme.ink; font.family: theme.display; font.pixelSize: theme.vaultSectionTitle }
                    Text { width: parent.width; text: "Files Vault couldn't name. Search once and they join the shelves."; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.Wrap }
                    VaultMatchList { width: parent.width; rows: root.adapter.unmatchedItems; compact: true; onMatchRequested: row => root.matchRow(row); onExtrasRequested: row => root.fileExtras(row) }
                    VaultLibraryButton { visible: root.adapter.unmatchedItems.length > 3; text: "See all unmatched files ›"; quiet: true; onClicked: root.showView("match", "") }
                }
            }
        }
    }
    Component {
        id: kindComponent
        VaultKindView {
            kind: ({ movies: "movie", shows: "show", books: "book", comics: "comic" })[root.currentView] || "movie"
            title: ({ movies: "Movies", shows: "Shows", books: "Books", comics: "Comics" })[root.currentView] || "Library"
            rows: root.adapter.rowsForKind(kind)
            onActivated: row => root.openRow(row)
            onHideRequested: row => root.adapter.hide(row)
        }
    }
    Component {
        id: searchComponent
        VaultKindView { title: "Search"; subtitle: root.searchRows.length + " titles · “" + bar.query + "”"; rows: root.searchRows; onActivated: row => root.openRow(row); onHideRequested: row => root.adapter.hide(row) }
    }
    Component {
        id: serverComponent
        VaultServerView {
            server: root.selectedServer; rows: root.adapter.rowsForServer(root.currentServer); groups: root.adapter.groupsForServer(root.currentServer)
            onActivated: row => root.openRow(row)
            onReconnectRequested: {
                if (root.selectedServer.state === "disabled") sources.show(sidebar)
                else root.openProviderConnection(root.currentServer)
            }
        }
    }
    Component {
        id: listComponent
        Flickable {
            id: listScroll; clip: true; contentHeight: listBody.implicitHeight + theme.vaultDockClearance; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: listScroll }
            Column {
                id: listBody; width: parent.width - 12; spacing: 18
                Text { text: root.currentView === "hidden" ? "Hidden" : "Unmatched"; color: theme.ink; font.family: theme.display; font.pixelSize: theme.vaultPageTitle }
                Text { width: parent.width; text: root.currentView === "hidden" ? "Titles you have hidden. Restore one to return it to your library." : "Choose the title these files belong to. Unmatched files stay off the shelves."; wrapMode: Text.Wrap; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultBody }
                VaultMatchList { width: parent.width; hiddenMode: root.currentView === "hidden"; rows: hiddenMode ? root.adapter.hiddenItems : root.adapter.unmatchedItems; onMatchRequested: row => root.matchRow(row); onRestoreRequested: row => root.adapter.restore(row); onExtrasRequested: row => root.fileExtras(row) }
                Repeater {
                    model: root.currentView === "match" ? root.showsWithSupplementalFiles() : []
                    delegate: VaultLibraryButton {
                        required property var modelData
                        objectName: "vaultShowExtras_" + (modelData.show.id || modelData.show.seriesKey)
                        text: qsTr("Supplemental files · %1").arg(modelData.show.title)
                        accessibleName: qsTr("%1 supplemental file%2 for %3").arg(modelData.count)
                            .arg(modelData.count === 1 ? "" : "s").arg(modelData.show.title)
                        onClicked: root.showExtras(modelData.show)
                    }
                }
            }
        }
    }
    Component {
        id: emptyComponent
        Item {
            Glass {
                anchors.left: parent.left; anchors.right: parent.right; y: 28; height: Math.min(360, parent.height - 32); backdrop: root.backdrop; radius: theme.vaultRadius
                Column {
                    anchors.centerIn: parent; width: parent.width - 80; spacing: 18
                    Text { text: "Your stories, together."; width: parent.width; color: theme.ink; font.family: theme.display; font.pixelSize: theme.vaultPageTitle; wrapMode: Text.Wrap }
                    Text { width: parent.width; text: "Add your movies, shows, books and comics. Vault turns your folders into a library worth exploring."; color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.vaultBody; wrapMode: Text.Wrap }
                    Row { spacing: 10; VaultLibraryButton { text: "Add a folder"; primary: true; onClicked: root.addFolderRequested() } VaultLibraryButton { text: "Connect a server"; onClicked: root.connectServerRequested() } }
                }
                DropArea { anchors.fill: parent; onDropped: drop => { if (drop.hasUrls) { root.addFolderRequested(); drop.acceptProposedAction() } } }
            }
        }
    }
}
