import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "PorticoData.js" as Data
import ".." as Colosseum
import "../CatalogueVisualMetrics.js" as Metrics

Item {
    id: shell
    objectName: "feriaAppPage"
    property bool lifecycleActive: false
    property var browserDeck: null
    property string hostSessionId: ""
    property bool managedBrowserRoute: false
    readonly property rect browserViewport: hostView.viewportRect
    signal browserOpenRequested(var descriptor, bool fresh, bool navigate)
    signal browserParkRequested(bool returnToFeria)
    signal browserCloseRequested(string sessionId)
    function activateBrowserRecord(record) {
        if (!record || record.contentKind !== "browser") return
        var target = record.target || ({})
        if (viewState !== "host") hostReturnState = viewState
        managedBrowserRoute = true
        hostSessionId = record.id
        hostApp = target.providerId
        hostUrl = target.url
        hostTitle = target.titleId || ""
        hostMode = target.launchMode || "home"
        viewState = "host"
    }
    function restoreBrowserWorld() {
        viewState = hostReturnState === "host" ? "home" : hostReturnState
        hostSessionId = ""
        managedBrowserRoute = false
        content.forceActiveFocus()
    }
    property int navigationEpoch: 0
    property bool navigationSuspended: false
    property bool applyingNavigationSnapshot: false
    function deferNavigation(action) {
        var epoch = navigationEpoch
        var route = viewState
        var tab = lens
        Qt.callLater(function() {
            if (shell.navigationEpoch === epoch && shell.viewState === route
                    && shell.lens === tab && shell.visible && !shell.navigationSuspended)
                action()
        })
    }
    function noteNavigationInput() {
        navigationEpoch++
        seeAllResetTimer.stop()
        if (viewState === "home") homeView.cancelRestoration()
        if (viewState === "seeAll") seeAllView.cancelRestoration()
    }
    onVisibleChanged: {
        navigationEpoch++
        if (!visible) {
            if (homeView) homeView.suspendRestoration()
            if (seeAllView) seeAllView.cancelRestoration()
        } else if (homeView && viewState === "home") {
            homeView.resumeRestoration()
        }
    }
    property bool discoveryStarted: false
    property string medium: "Feria"
    property var backdrop: null
    signal homeRequested()
    signal mediumSelected(string medium)
    signal seriesRequested(var series)
    signal bookRequested(var book)
    signal genreRequested(var genre)
    signal searchClicked()
    signal accountClicked(real anchorRight, real anchorBottom)
    signal accountRequested(string tab)
    signal accountDestinationRequested()
    property bool sharedAccount: false
    property bool sharedAccountActive: false
    property var sharedAccountView: null
    readonly property var activeAccountView: sharedAccountView || accountView
    readonly property bool accountContentActive: sharedAccountActive || viewState === "account"
    signal wallpaperClicked()
    signal fullscreenClicked()
    signal minimizeClicked()
    signal powerClicked()
    onLifecycleActiveChanged: {
        navigationEpoch++
        navigationSuspended = !lifecycleActive
        if (!lifecycleActive) {
            homeView.suspendRestoration()
            seeAllView.cancelRestoration()
        }
        if (!lifecycleActive && accountStore) accountStore.endVisit()
        if (lifecycleActive && liveDiscovery && !discoveryStarted) {
            discoveryStarted = true
            discovery.refresh()
        }
        if (lifecycleActive) {
            homeView.resumeRestoration()
            content.forceActiveFocus()
        }
    }

    Colosseum.Theme { id: theme }
    FontLoader { id: displayLoader; source: Qt.resolvedUrl("../../assets/fonts/Fraunces-Regular.ttf") }

    // Qt already handles display DPI. Resize the layout, not its text and controls.
    readonly property real unit: 16
    readonly property real marginX: theme.margin
    readonly property var galleryMetrics: Metrics.gallery
    readonly property color night: theme.biblioWashBottom
    readonly property color dusk: theme.biblioWashTop
    readonly property color ink: theme.ink
    readonly property color mist: theme.inkDim
    readonly property color slate: theme.inkDimmer
    readonly property color gold: theme.gold
    readonly property string displayFont: displayLoader.status === FontLoader.Ready ? displayLoader.name : theme.display
    readonly property string uiFont: theme.ui

    property var activeApps: Data.DEFAULT_APPS.slice()
    property var saved: []
    property string lens: "all"
    property string viewState: "home"
    property string searchReturnState: "home"
    property var seeAllReturnSnapshot: null
    property string seeAllKind: "shelf"
    property string seeAllShelfId: ""
    property string seeAllTitle: ""
    property string seeAllReturnLink: ""
    readonly property var seeAllShelf: shownShelves().find(function(row) { return row.id === seeAllShelfId }) || ({})
    property string selectedApp: activeApps.length ? activeApps[0] : ""
    property string selectedTitle: ""
    property var titleHistory: []
    property string hostApp: ""
    property string hostTitle: ""
    property string hostMode: "home"
    property string hostUrl: ""
    readonly property bool hostFullScreenActive: viewState === "host" && hostView.fullScreenActive
    property string hostReturnState: "home"
    property double hostOpenedAt: 0
    property bool providerWebViewReady: false
    property bool browserSuppressed: false
    property string query: ""
    property var chipSelection: ({ albums: "spotify", artists: "spotify" })

    property string focusArea: "app"
    property int focusIndex: 0
    property int shelfIndex: 0
    property int shelfCardIndex: 0
    property bool movingApp: false
    property int titleActionIndex: 0
    property int searchResultIndex: 0
    property int appManagerIndex: 0
    property int accountShellFocusIndex: 1
    property int accountHistoryFocusIndex: -1
    property bool accountContentFocusActive: false

    property string accountTab: "history"
    readonly property var accountTabKeys: ["history", "highlights", "stats", "apps"]
    property string accountApp: "netflix"
    property string statsBy: "title"
    property string region: "United States"
    readonly property var regionCodes: ({ "United States": "US", "United Kingdom": "GB",
                                   "India": "IN", "Canada": "CA", "Australia": "AU",
                                   "Germany": "DE", "Japan": "JP", "Brazil": "BR" })
    readonly property bool recording: accountStore && accountStore.recording
    property string accountMonth: Qt.formatDate(new Date(), "yyyy-MM")
    readonly property var accountStore: typeof FeriaAccount !== "undefined" ? FeriaAccount : null
    readonly property var recordedSessions: accountStore ? accountStore.sessions : []
    readonly property var continueItems: accountStore ? accountStore.continueItems : []
    function continueEntries() {
        return continueItems.filter(function(row) {
            return lens === "all" || (lens === "read" ? row.kind === "book"
                : lens === "listen" ? row.kind === "audio" : row.kind === "video")
        }).map(function(row) {
            var entry = Object.assign({}, row)
            entry.progress = row.duration > 0 ? row.position / row.duration : 0
            entry.source = providerName(row.pk)
            entry.sub = entry.source + " · " + (row.kind === "book"
                ? (row.duration > 0 ? Math.round(entry.progress * 100) + "% read" : "Saved reading place")
                : durationText(row.position / 60) + " / " + durationText(row.duration / 60))
            return entry
        })
    }
    property real hostResumePosition: 0
    property var hostResumeLocator: null
    property string signingOutProvider: ""
    property bool signOutQuiesced: false
    property string signOutMessage: ""
    property var signOutScope: ({})
    function providerMode(pk) { return Data.P[pk] ? Data.P[pk].v : "watch" }
    function signOutHere(pk) {
        if (signingOutProvider || viewState === "host") return
        var scope = FeriaBrowserPolicy.sessionScope(pk)
        if (!scope.domains || !scope.domains.length) { signOutMessage = "This service has no saved browser session."; return }
        signOutScope = scope; signOutMessage = browserDeck ? "Closing browser sessions before signing out…" : "Signing out…"; signingOutProvider = pk
        signOutQuiesced = !browserDeck
        if (browserDeck) browserDeck.startQuiescence(true)
    }
    Connections {
        target: shell.browserDeck
        function onQuiesced() {
            if (shell.signingOutProvider.length > 0) shell.signOutQuiesced = true
        }
        function onQuiescenceFailed(reason) {
            shell.signOutMessage = reason
            shell.signingOutProvider = ""
            shell.signOutQuiesced = false
            if (shell.browserDeck) shell.browserDeck.finishCleanup()
        }
    }
    Loader {
        id: sessionCleaner
        active: shell.signingOutProvider.length > 0 && shell.signOutQuiesced
        sourceComponent: FeriaSessionCleaner {
            profilePath: FeriaBrowserPolicy.storageRoot
            domains: shell.signOutScope.domains
            origins: shell.signOutScope.origins
            onFinished: function(success) {
                shell.signOutMessage = success
                    ? "Cookies and available site storage cleared for known origins. Other sign-in state may remain."
                    : "Site cleanup was incomplete. Some sign-in state may remain; retry or sign out on the service."
                shell.signingOutProvider = ""
                shell.signOutQuiesced = false
                if (shell.browserDeck) shell.browserDeck.finishCleanup()
            }
        }
    }
    Connections {
        target: FeriaBrowserPolicy
        function onStorageRootChanged() { shell.signingOutProvider = ""; shell.signOutMessage = "" }
    }
    property bool clearPending: false
    property string toast: ""

    Settings {
        id: settings
        category: "Feria"
        property string appsJson: ""
        property string savedJson: ""
        property string catalogueRegion: "United States"
    }
    Timer { id: toastTimer; interval: 2200; onTriggered: shell.toast = "" }
    Timer { id: clockTimer; interval: 1000; repeat: true; running: true; onTriggered: shell.clockRevision++ }
    property int clockRevision: 0
    property alias keyboardTarget: content

    readonly property var discovery: typeof porticoDiscovery !== "undefined" ? porticoDiscovery : null
    readonly property var contentStore: typeof porticoContent !== "undefined" ? porticoContent : null
    readonly property var appStateBridge: typeof porticoAppState !== "undefined" ? porticoAppState : null
    readonly property var destinationRouter: typeof porticoDestinationRouter !== "undefined" ? porticoDestinationRouter : null
    readonly property var identityBridge: typeof porticoIdentityState !== "undefined" ? porticoIdentityState : null
    readonly property bool liveDiscovery: discovery !== null && contentStore !== null
    onActiveAppsChanged: if (liveDiscovery && appStateBridge) appStateBridge.settingsApps = activeApps
    onLensChanged: {
        navigationEpoch++
        if (!applyingNavigationSnapshot && homeView) homeView.cancelRestoration()
        if (liveDiscovery) discovery.lens = lens
    }
    onRegionChanged: {
        if (liveDiscovery) {
            discovery.region = regionCodes[region] || "US"
            if (viewState === "title") requestAvailability(titleObj(selectedTitle))
        }
    }
    function providerName(pk) { return Data.P[pk] ? Data.P[pk].n : (contentStore ? contentStore.providerLabel(pk) : pk) }
    function isReadingProvider(pk) { return !!Data.P[pk] && Data.P[pk].v === "read" }
    function titleObj(id) {
        if (liveDiscovery) { contentStore.revision; var live = contentStore.title(id); if (live && live.t) return live; }
        var recorded = recordedSessions.find(function(s) { return s.id === id })
        if (recorded) return {t:recorded.title,k:recorded.kind,imageUrl:recorded.cover,_trend:true,resume:recorded}
        if (liveDiscovery) return null
        return Data.T[id] || null
    }
    function kindLabel(it) { return it ? (it._trend && contentStore ? contentStore.kindLabel(it.k) : Data.KIND[it.k] || it.k) : "" }
    function verbFor(it) { return it ? (it._trend && contentStore ? contentStore.verbForKind(it.k) : Data.VERB[it.k] || "watch") : "watch" }
    function searchableIds() { contentStore && contentStore.revision; return liveDiscovery ? contentStore.ids() : Object.keys(Data.T) }
    function artUrl(it, wide) {
        if (!it) return ""
        if (it._trend) return it.imageUrl || ""
        var localPoster = {
            tt4574334: "stranger", tt13443470: "wednesday",
            tt10919420: "squid", tt11126994: "arcane",
            tt11737520: "oplive", tt11564570: "glassonion",
            tt6155172: "roma", tt10233448: "vinland",
            tt15239678: "dune2", tt11280740: "severance",
            tt22248376: "frieren"
        }
        if (it.imdb === "tt4574334" && wide)
            return Qt.resolvedUrl("assets/stranger-bg.jpg")
        if (!wide && it.imdb && localPoster[it.imdb])
            return Qt.resolvedUrl("assets/" + localPoster[it.imdb] + "-poster.jpg")
        if (!wide && it.isbn === "9780441172719")
            return Qt.resolvedUrl("assets/dune-book.jpg")
        if (!wide && it.isbn === "9780593135204")
            return Qt.resolvedUrl("assets/phm-cover.jpg")
        if (it.imdb) return "https://images.metahub.space/" + (wide ? "background" : "poster") + "/medium/" + it.imdb + "/img"
        if (it.isbn) return "https://covers.openlibrary.org/b/isbn/" + it.isbn + "-L.jpg?default=false"
        return ""
    }
    function shapeFor(it) {
        if (!it) return "poster"
        if (it._trend && contentStore) return contentStore.shapeForKind(it.k)
        if (it.k === "album") return "square"
        if (it.k === "artist") return "circle"
        return "poster"
    }
    function toneFor(id) {
        var tones = [["#3f5a78","#16222e"],["#78503f","#2e1c16"],["#5a3f78","#241630"],["#3f785a","#16281e"],["#78703f","#2e2a16"],["#783f5a","#301624"],["#3f6478","#16242e"],["#785a3f","#2e2216"]]
        var ids = Object.keys(Data.T); var i = Math.max(0, ids.indexOf(id)); return tones[i % tones.length]
    }
    function setToast(s) { toast = s; toastTimer.restart() }

    function shownShelves() {
        var rows = []
        if (liveDiscovery) {
            discovery.revision; contentStore.revision
            if (saved.length) {
                var liveSaved = saved.filter(function(id) { var it = titleObj(id); return it && (lens === "all" || verbFor(it) === lens) })
                if (liveSaved.length) rows.push({ id:"saved", title:"Saved", src:"Your Feria", items:liveSaved, rank:false })
            }
            var liveRows = discovery.shelves()
            for (var j = 0; j < liveRows.length; ++j) {
                var s = liveRows[j], ids = []
                for (var k = 0; k < s.items.length; ++k) {
                    var key = s.items[k].canonicalKey
                    if (key && contentStore.contains(key)) ids.push(key)
                }
                if (ids.length) rows.push({ id:s.shelfId, title:s.title, src:s.sourceLabel,
                    items:ids, rank:s.ranked, stale:s.stale, providerId:s.providerId })
            }
            return rows
        }
        if (saved.length && (lens === "all" || lens === "read" || lens === "watch" || lens === "listen")) {
            var filteredSaved = saved.filter(function(id) { return lens === "all" || Data.VERB[Data.T[id].k] === lens })
            if (filteredSaved.length) rows.push({ id:"saved", title:"Saved", src:"Your Feria", v:lens, items:filteredSaved, apps:[] })
        }
        for (var i = 0; i < Data.SHELVES.length; ++i) {
            var sh = Data.SHELVES[i]
            if (lens !== "all" && sh.v !== lens) continue
            if (!sh.apps.some(function(pk) { return activeApps.indexOf(pk) >= 0 })) continue
            rows.push(sh)
        }
        return rows
    }
    function shelfItems(sh) {
        if (!sh) return []
        if (!sh.chips) return sh.items || []
        var pk = chipSelection[sh.id]
        if (!sh.chips[pk] || activeApps.indexOf(pk) < 0)
            pk = Object.keys(sh.chips).find(function(k) { return activeApps.indexOf(k) >= 0 })
        return pk ? sh.chips[pk] : []
    }
    function featured() {
        var pk = selectedApp
        if (liveDiscovery) {
            var shelves = shownShelves()
            for (var j = 0; j < shelves.length; ++j) {
                var row = shelves[j]
                if (row.providerId !== pk || !row.items.length) continue
                var id = row.items[0]
                return { id:id, item:titleObj(id), source:row.title, app:pk }
            }
            return { id:"", item:null, source:providerName(pk), app:pk }
        }
        for (var i = 0; i < Data.SHELVES.length; ++i) {
            var sh = Data.SHELVES[i]
            if (sh.apps.indexOf(pk) < 0) continue
            var items = shelfItems(sh)
            if (items.length) return { id:items[0], item:Data.T[items[0]], source:sh.title, app:pk }
        }
        return { id:"", item:null, source:providerName(pk), app:pk }
    }
    function offers(it) {
        if (!it) return []
        if (liveDiscovery) {
            discovery.availabilityRevision
            discovery.region
        }
        if (it._trend && liveDiscovery) return discovery.destinationsFor(it._trend).map(function(d, i) {
            var action = destinationRouter ? destinationRouter.actionFor(d) : d
            return { pk:action.providerId, label:d.label, url:action.url, exact:action.exact,
                level:action.mode === "exact" ? "t" : "s", fallback:action.mode !== "exact",
                appOnly:action.appOnly, actionable:action.actionable, reason:d.reason,
                mode:action.mode, order:i, availabilityConfirmed:d.availabilityConfirmed === true,
                offerType:d.offerType || "", availabilitySource:d.availabilitySource || "",
                region:d.region || "", catalogListed:d.catalogListed === true }
        })
        var list = (it.d || []).map(function(d, i) { return {pk:d[0], level:d[1], order:i, appOnly:!!Data.P[d[0]].app} })
        if (!list.length) {
            var pool = Data.CATALOG[Data.VERB[it.k]] || []
            var targets = pool.filter(function(pk) { return !Data.P[pk].app && activeApps.indexOf(pk) >= 0 }).slice(0, 4)
            if (!targets.length) targets = pool.filter(function(pk) { return !Data.P[pk].app }).slice(0, 3)
            return targets.map(function(pk, i) { return {pk:pk, level:"s", order:i, appOnly:false, fallback:true} })
        }
        list.sort(function(a, b) {
            return Number(a.appOnly) - Number(b.appOnly) || Number(a.level === "s") - Number(b.level === "s") ||
                Number(activeApps.indexOf(b.pk) >= 0) - Number(activeApps.indexOf(a.pk) >= 0) || a.order - b.order
        })
        return list
    }
    function requestAvailability(it, force) {
        if (liveDiscovery && it && it._trend && typeof discovery.requestAvailability === "function")
            discovery.requestAvailability(it._trend, force === true)
    }
    function availabilityFor(it) {
        if (!liveDiscovery || !it || !it._trend || typeof discovery.availabilityFor !== "function")
            return {status:"unsupported"}
        discovery.availabilityRevision
        discovery.region
        return discovery.availabilityFor(it._trend)
    }
    function relatedTitles(it) {
        if (!it) return []
        if (it._trend) return []
        if (it.k === "artist") return Object.keys(Data.T).filter(function(id) { return Data.T[id].a === selectedTitle })
        return (it.x || []).filter(function(id) { return !!Data.T[id] })
    }
    function visibleResults() {
        var q = query.trim().toLowerCase()
        if (!q) return []
        if (liveDiscovery) { contentStore.revision; return contentStore.search(q, 40) }
        var words = q.split(/\s+/)
        return Object.keys(Data.T).filter(function(id) {
            var it = Data.T[id]
            var hay = (it.t + " " + it.by + " " + Data.KIND[it.k]).toLowerCase()
            return words.every(function(word) { return hay.indexOf(word) >= 0 })
        }).sort(function(a,b) { return Number(Data.T[b].t.toLowerCase().startsWith(q)) - Number(Data.T[a].t.toLowerCase().startsWith(q)) }).slice(0, 40)
    }
    function searchableApps() { return activeApps.filter(function(pk) { return Data.P[pk] && Data.P[pk].sp }) }
    function appColumns() { return homeView.appColumnCount }
    function moveAppSelection(horizontal, vertical) {
        var columns = appColumns()
        var last = activeApps.length // The Apps management tile is a real grid cell.
        var index = Math.max(0, Math.min(last, focusIndex))
        var row = Math.floor(index / columns)
        var next = index + horizontal
        if (horizontal) {
            if (next < 0 || next > last || Math.floor(next / columns) !== row)
                return
        } else if (vertical < 0) {
            if (row === 0) {
                focusArea = "feature"
                homeView.revealFeature()
                return
            }
            next = index - columns
        } else {
            if (row === Math.floor(last / columns)) {
                homeView.focusLens()
                return
            }
            // A short final row has no cell in every column; use its last cell.
            next = Math.min(last, index + columns)
        }
        selectApp(next)
        homeView.revealApp(focusIndex)
    }
    function toggleSaved(id) {
        var next = saved.slice(); var p = next.indexOf(id)
        if (p < 0) next.unshift(id); else next.splice(p, 1)
        saved = next
        settings.savedJson = JSON.stringify(identityBridge && liveDiscovery ? next.map(function(key) {
            return identityBridge.canonicalRecord(key, "")
        }) : next)
        setToast(p < 0 ? "Saved to the top of your shelves" : "Removed from Saved")
    }
    function toggleApp(pk) {
        if (!Data.P[pk] || Data.P[pk].app) return
        var next = activeApps.slice(); var p = next.indexOf(pk)
        if (p < 0) next.push(pk); else next.splice(p, 1)
        activeApps = next; settings.appsJson = JSON.stringify(next)
        if (selectedApp === pk && p >= 0) selectedApp = next.length ? next[0] : ""
    }
    function persistApps() { settings.appsJson = JSON.stringify(activeApps) }
    function moveApp(delta) {
        var from = Math.min(focusIndex, activeApps.length - 1), to = from + delta
        if (from < 0 || to < 0 || to >= activeApps.length) return
        var next = activeApps.slice(), item = next.splice(from, 1)[0]
        next.splice(to, 0, item); activeApps = next; focusIndex = to; selectedApp = item
        settings.appsJson = JSON.stringify(next)
        Qt.callLater(function() {
            if (viewState === "home" && focusArea === "app")
                homeView.revealApp(focusIndex)
        })
    }
    function shiftLens(delta) {
        var keys = ["all","watch","listen","read"], i = keys.indexOf(lens)
        selectLens(keys[(i + delta + keys.length) % keys.length])
    }
    function cloneNavigationValue(value, fallback) {
        try {
            return JSON.parse(JSON.stringify(value))
        } catch (e) {
            return fallback
        }
    }
    function navigationSnapshot() {
        var safeState = viewState === "host" ? hostReturnState : viewState
        if (["home", "title", "search", "seeAll", "apps"].indexOf(safeState) < 0)
            safeState = "home"

        var gridSnapshot = seeAllReturnSnapshot
        if (safeState === "seeAll")
            gridSnapshot = seeAllView.returnSnapshot()

        return {
            version: 1,
            viewState: safeState,
            lens: lens,
            selectedApp: selectedApp,
            selectedTitle: selectedTitle,
            titleHistory: cloneNavigationValue(titleHistory, []),
            query: query,
            searchReturnState: searchReturnState,
            seeAllKind: seeAllKind,
            seeAllShelfId: seeAllShelfId,
            seeAllTitle: seeAllTitle,
            seeAllReturnLink: seeAllReturnLink,
            seeAllReturnSnapshot: cloneNavigationValue(gridSnapshot, null),
            focusArea: focusArea,
            focusIndex: focusIndex,
            shelfIndex: shelfIndex,
            shelfCardIndex: shelfCardIndex,
            titleActionIndex: titleActionIndex,
            searchResultIndex: searchResultIndex,
            appManagerIndex: appManagerIndex,
            chipSelection: cloneNavigationValue(chipSelection, ({ albums:"spotify", artists:"spotify" })),
            home: homeView.navigationSnapshot()
        }
    }
    function restoreNavigationSnapshot(snapshot) {
        if (!snapshot || Number(snapshot.version) !== 1)
            return false

        applyingNavigationSnapshot = true
        navigationEpoch++
        homeView.cancelRestoration()
        seeAllView.cancelRestoration()
        var nextLens = String(snapshot.lens || "all")
        if (["all", "watch", "listen", "read"].indexOf(nextLens) < 0)
            nextLens = "all"
        lens = nextLens

        chipSelection = cloneNavigationValue(snapshot.chipSelection, ({ albums:"spotify", artists:"spotify" }))

        var nextApp = String(snapshot.selectedApp || "")
        selectedApp = activeApps.indexOf(nextApp) >= 0 ? nextApp : (activeApps.length ? activeApps[0] : "")
        selectedTitle = String(snapshot.selectedTitle || "")
        titleHistory = cloneNavigationValue(snapshot.titleHistory, [])
        query = String(snapshot.query || "")

        var searchOrigin = String(snapshot.searchReturnState || "home")
        searchReturnState = ["home", "title", "seeAll", "apps"].indexOf(searchOrigin) >= 0 ? searchOrigin : "home"

        seeAllKind = snapshot.seeAllKind === "continue" ? "continue" : "shelf"
        seeAllShelfId = String(snapshot.seeAllShelfId || "")
        seeAllTitle = String(snapshot.seeAllTitle || "")
        seeAllReturnLink = String(snapshot.seeAllReturnLink || "")
        seeAllReturnSnapshot = cloneNavigationValue(snapshot.seeAllReturnSnapshot, null)

        focusArea = ["feature", "app", "lens", "continue", "shelf"].indexOf(snapshot.focusArea) >= 0
                ? snapshot.focusArea : "app"
        focusIndex = Math.max(0, Math.min(activeApps.length, Number(snapshot.focusIndex || 0)))
        shelfIndex = Math.max(0, Number(snapshot.shelfIndex || 0))
        shelfCardIndex = Math.max(0, Number(snapshot.shelfCardIndex || 0))
        titleActionIndex = Math.max(0, Number(snapshot.titleActionIndex || 0))
        searchResultIndex = Number(snapshot.searchResultIndex !== undefined ? snapshot.searchResultIndex : -1)
        appManagerIndex = Math.max(0, Number(snapshot.appManagerIndex || 0))
        movingApp = false

        var nextState = String(snapshot.viewState || "home")
        if (["home", "title", "search", "seeAll", "apps"].indexOf(nextState) < 0)
            nextState = "home"
        if (nextState === "title" && !selectedTitle.length)
            nextState = "home"
        if (nextState === "seeAll" && seeAllKind === "shelf" && !seeAllShelfId.length)
            nextState = "home"

        homeView.restoreNavigationSnapshot(snapshot.home || ({}))
        viewState = nextState
        applyingNavigationSnapshot = false

        if (viewState === "title")
            requestAvailability(titleObj(selectedTitle))

        deferNavigation(function() {
            if (viewState === "search") {
                if (searchView.searchField) searchView.searchField.forceActiveFocus()
            } else if (viewState === "seeAll") {
                seeAllView.restoreReturn(shell.seeAllReturnSnapshot)
            } else {
                content.forceActiveFocus()
            }
        })
        return true
    }
    function openSeeAll(shelf) {
        navigationEpoch++
        seeAllView.cancelRestoration()
        homeView.rememberLens()
        seeAllReturnSnapshot = null
        seeAllKind = shelf ? "shelf" : "continue"
        seeAllShelfId = shelf ? shelf.id : ""
        seeAllTitle = shelf ? shelf.title : "Continue"
        seeAllReturnLink = shelf ? "feriaSeeAll_" + shelf.id : "feriaContinueRailHeader"
        viewState = "seeAll"
        seeAllResetTimer.restart()
    }
    Timer {
        id: seeAllResetTimer
        interval: 0
        onTriggered: if (shell.viewState === "seeAll") seeAllView.resetSelection()
    }
    function openTitle(id) {
        if (!titleObj(id)) return
        if (viewState === "seeAll") seeAllReturnSnapshot = seeAllView.returnSnapshot()
        if (sharedAccountActive) accountDestinationRequested()
        if (titleObj(id).resume) { resumeSession(titleObj(id).resume); return }
        titleHistory = titleHistory.concat([{state:viewState,id:selectedTitle}])
        selectedTitle = id; titleActionIndex = 0; viewState = "title"; content.forceActiveFocus()
        requestAvailability(titleObj(id))
    }
    function openSearch(seed) {
        if (viewState === "seeAll") seeAllReturnSnapshot = seeAllView.returnSnapshot()
        if (viewState !== "search") searchReturnState = viewState
        query = seed || ""; searchResultIndex = -1; viewState = "search"
        deferNavigation(function() {
            if (searchView.searchField) searchView.searchField.forceActiveFocus()
        })
    }
    function openHost(pk,id,mode,url,position,locator,fresh) {
        if (signingOutProvider) { setToast("Wait for sign-out to finish."); return }
        hostResumeLocator = locator || null
        hostResumePosition = position || 0
        var provider = Data.P[pk]
        var destination = url || (provider && provider.d ? "https://" + provider.d : "")
        if (!url && mode === "search" && provider && provider.sp && query.trim().length)
            destination = "https://" + provider.sp + encodeURIComponent(query.trim())
        if (!destination.length || !FeriaBrowserPolicy.allowsNavigation(destination)) {
            setToast("This service has no web destination.")
            return
        }
        if (sharedAccountActive) accountDestinationRequested()
        var retained = browserDeck && hostView.preferredEngine(pk) === "qtwebengine"
        if (retained && !browserDeck.ready) { setToast("Wait for browser cleanup to finish."); return }
        managedBrowserRoute = !!retained
        var title = titleObj(id)
        if (!retained && accountStore) {
            accountStore.beginVisit({pk:pk,resumePending:position > 0,title:title ? title.t : "",cover:title ? artUrl(title,false) : ""})
        }
        hostReturnState = viewState === "host" ? hostReturnState : viewState
        hostApp = pk; hostTitle = id || ""; hostMode = mode || "home"; hostUrl = destination
        hostSessionId = ""; providerWebViewReady = false; viewState = "host"; content.forceActiveFocus()
        if (retained) browserOpenRequested({
            providerId: pk, providerName: providerName(pk), title: title ? title.t : providerName(pk),
            visitContext: { title: title ? title.t : "", cover: title ? artUrl(title, false) : "" },
            logo: Qt.resolvedUrl(Data.glyphSource(pk)), url: destination, mode: providerMode(pk),
            titleId: id || "", launchMode: mode || "home", position: position || 0, locator: locator || null
        }, fresh === true, !!url && fresh !== true)
    }
    function beginHostVisit() {
        if (!accountStore) return
        var title = titleObj(hostTitle)
        accountStore.beginVisit({pk:hostApp,resumePending:hostResumePosition > 0,title:title ? title.t : "",cover:title ? artUrl(title,false) : ""})
    }
    function goHome() {
        if (viewState === "home") { homeRequested(); return }
        titleHistory = []
        viewState = "home"
        focusArea = "app"
        content.forceActiveFocus()
        homeView.revealApp(focusIndex)
    }
    function back() {
        if (movingApp) { movingApp = false; setToast("Order unchanged"); return }
        if (viewState === "host") {
            if (hostView.fullScreenActive) { hostView.exitFullScreen(); return }
            if (hostView.cancelAddressEditing()) return
            if (hostView.optionsOpen) { hostView.focusApp(); return }
            if (hostSessionId.length > 0) {
                browserParkRequested(true)
                return
            }
            viewState = hostReturnState
        }
        else if (viewState === "title") {
            var previous = titleHistory.length ? titleHistory[titleHistory.length - 1] : {state:"home",id:""}
            titleHistory = titleHistory.slice(0, -1); viewState = previous.state; selectedTitle = previous.id
        } else if (viewState === "seeAll") {
            viewState = "home"
            homeView.restoreLens()
            deferNavigation(function() { homeView.focusRowLink(shell.seeAllReturnLink) })
            return
        } else if (viewState === "search") {
            var returnState = searchReturnState
            viewState = ["home", "title", "seeAll", "apps"].indexOf(returnState) >= 0 ? returnState : "home"
        } else if (viewState === "apps" || viewState === "account") viewState = "home"
        else if (focusArea === "shelf") { focusArea = "app"; shelfIndex = 0; shelfCardIndex = 0; homeView.revealApp(focusIndex) }
        else { homeRequested(); return }
        if (viewState === "search") deferNavigation(function() { searchView.searchField.forceActiveFocus() })
        else if (viewState === "seeAll") deferNavigation(function() { seeAllView.restoreReturn(shell.seeAllReturnSnapshot) })
        else content.forceActiveFocus()
    }
    function activateTitleAction() {
        var it = titleObj(selectedTitle), doors = offers(it), related = relatedTitles(it)
        if (titleActionIndex < doors.length) {
            var d = doors[titleActionIndex]; if (!d.appOnly && d.actionable !== false) openHost(d.pk, selectedTitle, d.level === "t" ? "title" : "search", d.url)
        } else if (titleActionIndex === doors.length) toggleSaved(selectedTitle)
        else if (related[titleActionIndex - doors.length - 1]) openTitle(related[titleActionIndex - doors.length - 1])
    }
    function titleActionCount() { return offers(titleObj(selectedTitle)).length + 1 + relatedTitles(titleObj(selectedTitle)).length }
    function activateSearchChoice() {
        var ids = visibleResults(), apps = searchableApps()
        if (searchResultIndex < ids.length && ids[searchResultIndex]) openTitle(ids[searchResultIndex])
        else if (apps[searchResultIndex - ids.length]) openHost(apps[searchResultIndex - ids.length], "", "search")
    }
    function selectLens(key) {
        if (["all","watch","listen","read"].indexOf(key) < 0 || lens === key) return
        homeView.rememberLens()
        lens = key
        shelfIndex = 0; shelfCardIndex = 0
        homeView.restoreLens()
    }
    function selectApp(index) {
        focusArea = "app"; focusIndex = Math.max(0, Math.min(activeApps.length, index))
        if (activeApps[focusIndex]) selectedApp = activeApps[focusIndex]
    }
    function accountFocusForTab(tab) {
        var i = accountTabKeys.indexOf(tab)
        return i >= 0 ? i + 1 : 1
    }
    function setAccountShellFocus(index) {
        accountHistoryFocusIndex = -1
        accountContentFocusActive = false
        activeAccountView.resetContentFocus()
        accountShellFocusIndex = Math.max(sharedAccountActive ? 1 : 0, Math.min(sharedAccountActive && accountTab !== "apps" ? 3 : 4, index))
        if (sharedAccountActive && activeAccountView) activeAccountView.forceActiveFocus()
    }
    function historyActionSessionIndices() {
        var sessions = allSessions().slice(0, 60)
        var indices = []
        for (var i = 0; i < sessions.length; ++i)
            if (sessions[i].id) indices.push(i)
        return indices
    }
    function accountHistoryFocusCount() { return 2 + historyActionSessionIndices().length }
    function setAccountHistoryFocus(index) {
        if (!accountContentActive || accountTab !== "history") return
        var count = accountHistoryFocusCount()
        if (count <= 0) return
        var targetIndex = Math.max(0, Math.min(count - 1, index))
        activeAccountView.resetContentFocus()
        accountShellFocusIndex = -1
        accountHistoryFocusIndex = targetIndex
        if (sharedAccountActive && activeAccountView) activeAccountView.forceActiveFocus()
        Qt.callLater(function() { activeAccountView.revealHistoryFocus(targetIndex) })
    }
    function enterAccountHistoryContent() {
        if (accountContentActive && accountTab === "history") setAccountHistoryFocus(0)
    }
    function leaveAccountHistoryContent() {
        accountHistoryFocusIndex = -1
        accountShellFocusIndex = accountFocusForTab("history")
    }
    function moveAccountHistoryFocus(delta) {
        if (accountHistoryFocusIndex < 0) return
        var next = accountHistoryFocusIndex + delta
        if (next < 0) { leaveAccountHistoryContent(); return }
        setAccountHistoryFocus(next)
    }
    function activateAccountHistoryFocus() {
        if (accountHistoryFocusIndex === 0) { toggleRecording(); return }
        if (accountHistoryFocusIndex === 1) { clearHistory(); return }
        var sessionIndices = historyActionSessionIndices()
        var sessionIndex = sessionIndices[accountHistoryFocusIndex - 2]
        var sessions = allSessions()
        if (sessionIndex !== undefined && sessions[sessionIndex] && sessions[sessionIndex].id)
            openTitle(sessions[sessionIndex].id)
    }
    function activateAccountShellFocus() {
        if (accountShellFocusIndex === 0) {
            back()
            return
        }
        var tab = accountTabKeys[accountShellFocusIndex - 1]
        if (tab) accountTab = tab
    }
    onViewStateChanged: {
        navigationEpoch++
        if (!applyingNavigationSnapshot) {
            if (homeView) {
                if (viewState === "home") homeView.resumeRestoration()
                else homeView.suspendRestoration()
            }
            if (seeAllView) seeAllView.cancelRestoration()
        }
        if (viewState !== "host" && accountStore) accountStore.endVisit()
        accountHistoryFocusIndex = -1
        accountContentFocusActive = false
    }
    onAccountTabChanged: {
        accountHistoryFocusIndex = -1
        accountContentFocusActive = false
        if (accountContentActive)
            accountShellFocusIndex = accountFocusForTab(accountTab)
    }
    function openAccount() {
        if (sharedAccount) { accountRequested("apps"); return }
        viewState = "account"
        accountShellFocusIndex = accountFocusForTab(accountTab)
        content.forceActiveFocus()
    }
    function openApps() {
        accountTab = "apps"
        accountApp = activeApps.length ? activeApps[0] : "region"
        openAccount()
    }
    function resumeSession(session) {
        openHost(session.pk, "", "resume", session.url, session.completed || session.kind === "book" ? 0 : session.position, session.completed ? null : session.locator)
    }
    function allSessions() { return recordedSessions }
    Connections {
        target: shell.accountStore
        function onProfileChanged() {
            shell.navigationEpoch++
            homeView.cancelRestoration()
            seeAllView.cancelRestoration()
            shell.viewState = "home"
        }
    }
    function sessionDay(s) {
        var d = new Date(s.at), n = new Date(), y = new Date(n)
        y.setDate(y.getDate()-1)
        if (d.toDateString() === n.toDateString()) return "Today"
        if (d.toDateString() === y.toDateString()) return "Yesterday"
        return Qt.formatDate(d, "dddd, MMMM d")
    }
    function monthSessions(key) { return allSessions().filter(function(s) { return Qt.formatDate(new Date(s.at), "yyyy-MM") === key }) }
    function totalMins(ss) { return ss.reduce(function(n,s) { return n+s.mins },0) }
    function distinct(ss,key) { return Array.from(new Set(ss.map(function(s) { return s[key] }).filter(Boolean))).length }
    function tallySessions(ss,key) {
        var found = {}
        ss.forEach(function(s) { var k=s[key]; if (!k) return; if (!found[k]) found[k]={key:k,mins:0,n:0,apps:[],titles:[]};
            found[k].mins+=s.mins; found[k].n++;
            if (found[k].apps.indexOf(s.pk)<0) found[k].apps.push(s.pk)
            if (s.id && found[k].titles.indexOf(s.id)<0) found[k].titles.push(s.id)
        })
        return Object.keys(found).map(function(k) { return found[k] }).sort(function(a,b) { return b.mins-a.mins })
    }
    function changeAccountMonth(delta) {
        var d = new Date(accountMonth+"-01T12:00:00")
        d.setMonth(d.getMonth()+delta)
        var key=Qt.formatDate(d,"yyyy-MM")
        var earliest=allSessions().length ? Qt.formatDate(new Date(allSessions()[allSessions().length-1].at),"yyyy-MM") : Qt.formatDate(new Date(),"yyyy-MM")
        if (key>=earliest && key<=Qt.formatDate(new Date(),"yyyy-MM")) accountMonth=key
    }
    function setRegion(value) { region=value; settings.catalogueRegion=value }
    function toggleRecording() {
        if (accountStore) accountStore.setRecording(!recording)
        clearPending=false
    }
    function clearHistory() {
        if (!clearPending) { clearPending=true; setToast("Press Clear history again to confirm"); return }
        if (accountStore && accountStore.clearActivity()) clearPending=false
    }
    function durationText(m) { return m <= 0 ? "0 min" : m < 1 ? "Under a minute" : m < 60 ? Math.round(m) + " min" : Math.floor(m / 60) + "h " + String(Math.round(m % 60)).padStart(2,"0") + "m" }
    function clockText() { clockRevision; return Qt.formatTime(new Date(), "h:mm") }
    function clockSuffix() { clockRevision; return Qt.formatTime(new Date(), "AP") }

    Component.onCompleted: {
        try {
            if (settings.appsJson) activeApps = JSON.parse(settings.appsJson).filter(function(k) { return Data.P[k] && !Data.P[k].app })
            if (settings.savedJson) {
                var loadedSaved = JSON.parse(settings.savedJson)
                if (identityBridge && liveDiscovery) loadedSaved = identityBridge.migrateSaved(loadedSaved).map(function(record) {
                    return record.canonicalKey || record.legacyId || ""
                })
                saved = loadedSaved.filter(function(k) { return k && (liveDiscovery || Data.T[k]) })
            }
        } catch (e) { activeApps = Data.DEFAULT_APPS.slice(); saved = [] }
        region=settings.catalogueRegion
        if (liveDiscovery) {
            discovery.lens = lens
            if (appStateBridge) appStateBridge.settingsApps = activeApps
            else discovery.activeApps = activeApps
            discovery.region = regionCodes[region] || "US"
        }
        selectedApp = activeApps.length ? activeApps[0] : ""
        content.forceActiveFocus()
    }
    property string snapshotState: ""
    property string snapshotOutput: ""
    function argumentValue(prefix) {
        var args = Qt.application.arguments
        for (var i = 0; i < args.length; ++i) if (String(args[i]).indexOf(prefix) === 0) return String(args[i]).slice(prefix.length)
        return ""
    }
    function applySnapshotState() {
        snapshotState = argumentValue("--snapshot=")
        snapshotOutput = argumentValue("--output=")
        var s = snapshotState
        if (!s || s === "home") { viewState = "home"; focusArea = "app"; selectApp(0) }
        else if (s === "title") { selectedTitle = "stranger"; titleHistory = [{state:"home",id:""}]; viewState = "title"; titleActionIndex = 0 }
        else if (s === "search" || s === "search-results") { query = "Dune"; viewState = "search"; searchResultIndex = 0 }
        else if (s === "search-empty") { query = ""; viewState = "search"; searchResultIndex = -1 }
        else if (s === "search-no-results") { query = "qzxvjk"; viewState = "search"; searchResultIndex = 0 }
        else if (s === "apps") { viewState = "apps"; appManagerIndex = 0 }
        else if (s === "host") { hostApp = "netflix"; hostTitle = "stranger"; hostMode = "title"; hostReturnState = "title"; viewState = "host" }
    }
    Timer {
        interval: 2600
        running: shell.snapshotOutput.length > 0
        repeat: false
        onTriggered: {
            console.log("PARITY_STATE", shell.viewState,
                        "home", homeView.visible, homeView.width, homeView.height,
                        "title", titleView.visible, titleView.width, titleView.height,
                        "search", searchView.visible, searchView.width, searchView.height,
                        "apps", appsView.visible, appsView.width, appsView.height,
                        "account", activeAccountView.visible, activeAccountView.width, activeAccountView.height,
                        "host", hostView.visible, hostView.width, hostView.height)
            stage.grabToImage(function(result) {
            var ok = result.saveToFile(shell.snapshotOutput)
            console.log("PORTICO_SNAPSHOT", shell.snapshotState, shell.snapshotOutput, ok)
            Qt.quit()
        }, Qt.size(shell.width, shell.height))
        }
    }

    Item {
        id: content
        objectName: "keyboardTarget"
        anchors.fill: parent
        focus: true
        onActiveFocusChanged: {
            // Restoring/returning to Home must hand actual focus back to the shared rail.
            if (activeFocus && shell.viewState === "home" && shell.focusArea === "continue")
                shell.deferNavigation(function() { homeView.restoreContinueFocus() })
        }
        Keys.onPressed: function(event) {
            if (shell.viewState === "search" || shell.viewState === "seeAll") return
            if (shell.viewState === "title") {
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) shell.titleActionIndex = Math.min(shell.titleActionCount() - 1, shell.titleActionIndex + 1)
                else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) shell.titleActionIndex = Math.max(0, shell.titleActionIndex - 1)
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) shell.activateTitleAction()
                else return
                if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                    titleView.revealCurrentAction()
                event.accepted = true; return
            }
            if (shell.viewState === "apps") {
                var allApps = Data.CATALOG.watch.concat(Data.CATALOG.listen, Data.CATALOG.read)
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) shell.appManagerIndex = Math.min(allApps.length - 1, shell.appManagerIndex + 1)
                else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) shell.appManagerIndex = Math.max(0, shell.appManagerIndex - 1)
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) shell.toggleApp(allApps[shell.appManagerIndex])
                else return
                event.accepted = true; return
            }
            if (shell.viewState === "account") {
                event.accepted = activeAccountView.handleKey(event.key)
                return
            }
            if (shell.viewState === "host") return
            if (event.key === Qt.Key_BracketLeft) { shell.shiftLens(-1); event.accepted = true; return }
            if (event.key === Qt.Key_BracketRight) { shell.shiftLens(1); event.accepted = true; return }
            if (shell.viewState !== "home") return
            shell.noteNavigationInput()
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                var n = event.key - Qt.Key_1
                if (n < shell.activeApps.length) { shell.selectApp(n); shell.openHost(shell.activeApps[n], "", "home") }
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Space && shell.focusArea === "app" && shell.focusIndex < shell.activeApps.length) {
                shell.movingApp = !shell.movingApp
                shell.setToast(shell.movingApp ? "Use arrows to reorder, Space to place" : "App order saved")
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                var d = event.key === Qt.Key_Right ? 1 : -1
                if (shell.movingApp) shell.moveApp(d)
                else if (shell.focusArea === "app") {
                    shell.moveAppSelection(d, 0)
                }
                else if (shell.focusArea === "shelf") {
                    var items = shell.shelfItems(shell.shownShelves()[shell.shelfIndex])
                    shell.shelfCardIndex = Math.max(0, Math.min(items.length - 1, shell.shelfCardIndex + d))
                }
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                var v = event.key === Qt.Key_Down ? 1 : -1
                if (shell.focusArea === "feature") {
                    shell.focusArea = v > 0 ? "app" : "feature"
                    if (v > 0) homeView.revealApp(shell.focusIndex)
                }
                else if (shell.focusArea === "app") {
                    if (!shell.movingApp) shell.moveAppSelection(0, v)
                    else if (v < 0) { shell.focusArea = "feature"; homeView.revealFeature() }
                    else { shell.focusArea = "lens"; homeView.focusLens() }
                } else if (shell.focusArea === "lens") {
                    if (v < 0) { shell.focusArea = "app"; homeView.revealApp(shell.focusIndex) }
                    else homeView.focusBelowLens()
                } else if (shell.focusArea === "shelf") {
                    shell.shelfIndex += v
                    if (shell.shelfIndex < 0) { shell.shelfIndex = 0; homeView.focusAboveShelves() }
                    else { shell.shelfIndex = Math.min(shell.shownShelves().length - 1, shell.shelfIndex); shell.shelfCardIndex = 0; homeView.revealShelf(shell.shelfIndex) }
                }
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (shell.focusArea === "feature") {
                    if (shell.featured().item) shell.openTitle(shell.featured().id)
                    else shell.openHost(shell.selectedApp, "", "home")
                }
                else if (shell.focusArea === "app") {
                    if (shell.focusIndex < shell.activeApps.length) shell.openHost(shell.activeApps[shell.focusIndex], "", "home")
                    else shell.openApps()
                } else if (shell.focusArea === "shelf") {
                    var sh = shell.shownShelves()[shell.shelfIndex], ids = shell.shelfItems(sh)
                    if (ids[shell.shelfCardIndex]) shell.openTitle(ids[shell.shelfCardIndex])
                }
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Slash || (event.text && /^[a-zA-Z0-9]$/.test(event.text) && !(event.modifiers & Qt.ControlModifier))) {
                shell.openSearch(event.key === Qt.Key_Slash ? "" : event.text)
                event.accepted = true
            }
        }

        Item {
            id: stage
            anchors.fill: parent
            Rectangle {
                anchors.fill: parent
                // Both header levels share the actual wallpaper on provider routes.
                visible: shell.viewState !== "host" || !shell.backdrop
                gradient: Gradient {
                    GradientStop { position: 0; color: shell.dusk }
                    GradientStop { position: 1; color: shell.night }
                }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: hostView.y + hostView.chromeHeight
                visible: shell.viewState === "host" && !!shell.backdrop && !shell.hostFullScreenActive
                color: shell.night
                opacity: 0.38
            }
            PorticoDiscoveryHome {
                id: homeView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "home" || shell.viewState === "apps"
            }
            PorticoDiscoveryTitleView {
                id: titleView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "title"
            }
            FeriaSeeAllView {
                id: seeAllView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "seeAll"
            }
            PorticoDiscoverySearchRedesign {
                id: searchView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "search"
            }
            PorticoCombinedParityAppsView {
                id: appsView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "apps"
            }
            PorticoCombinedParityAccountView {
                id: accountView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "account"
                enabled: visible
            }
            PorticoDiscoveryHostView {
                id: hostView
                anchors.fill: parent
                anchors.topMargin: shell.hostFullScreenActive ? 0 : (shell.sharedAccount ? 108 : 0)
                controller: shell
                visible: shell.viewState === "host"
            }
            PorticoCombinedMast {
                id: topbar
                objectName: "feriaMast"
                controller: shell
                backdrop: shell.backdrop
                showLenses: false
                visible: !shell.sharedAccount && shell.viewState !== "host" && shell.viewState !== "account"
                anchors { left: parent.left; right: parent.right; top: parent.top }
                z: 80
            }
            Colosseum.TopBar {
                objectName: "feriaWorldTopBar"
                backdrop: shell.backdrop
                activeMedium: "Feria"
                compactBrowserChrome: shell.viewState === "host" && shell.width < 900
                backLabel: shell.viewState === "home" ? "Home" : "Feria"
                lifecycleActive: shell.lifecycleActive
                wallpaperEnabled: false
                x: shell.marginX; y: 30
                width: parent.width - 2 * shell.marginX
                z: 80
                visible: shell.sharedAccount && shell.viewState !== "account" && !shell.hostFullScreenActive
                onMediumSelected: (medium) => shell.mediumSelected(medium)
                onHomeRequested: {
                    if (shell.viewState === "home") shell.homeRequested()
                    else shell.back()
                }
                onSearchClicked: shell.openSearch("")
                onAccountClicked: shell.openAccount()
                onFullscreenClicked: shell.fullscreenClicked()
                onMinimizeClicked: shell.minimizeClicked()
                onPowerClicked: shell.powerClicked()
            }
            Rectangle {
                anchors { right: parent.right; top: parent.top; rightMargin: shell.marginX; topMargin: 6.2 * shell.unit }
                width: toastText.implicitWidth + 2.2 * shell.unit
                height: 2.6 * shell.unit
                radius: height / 2
                color: Qt.rgba(8/255,10/255,16/255,0.94)
                border.width: 1
                border.color: Qt.rgba(1,1,1,0.14)
                visible: shell.toast.length > 0
                z: 90
                Row {
                    anchors.centerIn: parent
                    spacing: 0.6 * shell.unit
                    Rectangle { width: 0.45 * shell.unit; height: width; radius: width/2; color: shell.gold }
                    Text { id: toastText; text: shell.toast; color: shell.mist; font.family: theme.ui; font.pixelSize: 0.88 * shell.unit }
                }
            }
        }
    }
}
