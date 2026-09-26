import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: shell
    property bool lifecycleActive: false
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
    signal wallpaperClicked()
    signal fullscreenClicked()
    signal minimizeClicked()
    signal powerClicked()
    onLifecycleActiveChanged: {
        if (lifecycleActive && liveDiscovery && !discoveryStarted) {
            discoveryStarted = true
            discovery.refresh()
        }
        if (lifecycleActive) content.forceActiveFocus()
    }

    Colosseum.Theme { id: theme }
    FontLoader { id: displayLoader; source: Qt.resolvedUrl("../../assets/fonts/Fraunces-Regular.ttf") }

    readonly property real unit: Math.max(9, Math.min(width / 120, 32))
    readonly property real marginX: 3.375 * unit
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
    property string selectedApp: activeApps.length ? activeApps[0] : ""
    property string selectedTitle: ""
    property var titleHistory: []
    property string hostApp: ""
    property string hostTitle: ""
    property string hostMode: "home"
    property string hostUrl: ""
    property string hostReturnState: "home"
    property double hostOpenedAt: 0
    property bool providerWebViewReady: false
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
    property bool recording: true
    property string accountMonth: "2026-09"
    property var recordedSessions: []
    property bool showSampleHistory: false
    property bool clearPending: false
    property string toast: ""

    Settings {
        id: settings
        category: "Feria"
        property string appsJson: ""
        property string savedJson: ""
        property string sessionsJson: ""
        property bool historySamples: false
        property bool historyRecording: true
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
    onLensChanged: if (liveDiscovery) discovery.lens = lens
    onRegionChanged: if (liveDiscovery) discovery.region = regionCodes[region] || "US"
    function providerName(pk) { return Data.P[pk] ? Data.P[pk].n : (contentStore ? contentStore.providerLabel(pk) : pk) }
    function titleObj(id) {
        if (liveDiscovery) { contentStore.revision; var live = contentStore.title(id); return live && live.t ? live : null }
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
        if (it._trend && liveDiscovery) return discovery.destinationsFor(it._trend).map(function(d, i) {
            var action = destinationRouter ? destinationRouter.actionFor(d) : d
            return { pk:action.providerId, label:d.label, url:action.url, exact:action.exact,
                level:action.mode === "exact" ? "t" : "s", fallback:action.mode !== "exact",
                appOnly:action.appOnly, actionable:action.actionable, reason:d.reason,
                mode:action.mode, order:i }
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
    function appColumns() { return width > 1100 ? 7 : (width > 760 ? 5 : 3) }
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
        lens = keys[(i + delta + keys.length) % keys.length]
        shelfIndex = 0; shelfCardIndex = 0
        if (viewState === "home" && focusArea === "shelf")
            Qt.callLater(function() { homeView.revealShelf(shelfIndex) })
    }
    function openTitle(id) {
        if (!titleObj(id)) return
        titleHistory = titleHistory.concat([{state:viewState,id:selectedTitle}])
        selectedTitle = id; titleActionIndex = 0; viewState = "title"; content.forceActiveFocus()
    }
    function openSearch(seed) {
        query = seed || ""; searchResultIndex = -1; viewState = "search"
        Qt.callLater(function() {
            if (searchView.searchField) searchView.searchField.forceActiveFocus()
        })
    }
    function openHost(pk,id,mode,url) {
        hostReturnState = viewState; hostApp = pk; hostTitle = id || ""; hostMode = mode || "home"; hostUrl = url || ""; viewState = "host"; content.forceActiveFocus()
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
            viewState = hostReturnState
        }
        else if (viewState === "title") {
            var previous = titleHistory.length ? titleHistory[titleHistory.length - 1] : {state:"home",id:""}
            titleHistory = titleHistory.slice(0, -1); viewState = previous.state; selectedTitle = previous.id
        } else if (viewState === "search" || viewState === "apps") viewState = "home"
        else if (focusArea === "shelf") { focusArea = "app"; shelfIndex = 0; shelfCardIndex = 0; homeView.revealApp(focusIndex) }
        else { homeRequested(); return }
        if (viewState === "search") Qt.callLater(function() { searchView.searchField.forceActiveFocus() })
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
    function selectLens(key) { lens = key; shelfIndex = 0; shelfCardIndex = 0 }
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
        accountView.resetContentFocus()
        accountShellFocusIndex = Math.max(0, Math.min(4, index))
    }
    function historyActionSessionIndices() {
        var sessions = allSessions()
        var indices = []
        for (var i = 0; i < sessions.length; ++i)
            if (sessions[i].id) indices.push(i)
        return indices
    }
    function accountHistoryFocusCount() { return 2 + historyActionSessionIndices().length }
    function setAccountHistoryFocus(index) {
        if (viewState !== "account" || accountTab !== "history") return
        var count = accountHistoryFocusCount()
        if (count <= 0) return
        var targetIndex = Math.max(0, Math.min(count - 1, index))
        accountView.resetContentFocus()
        accountShellFocusIndex = -1
        accountHistoryFocusIndex = targetIndex
        Qt.callLater(function() { accountView.revealHistoryFocus(targetIndex) })
    }
    function enterAccountHistoryContent() {
        if (viewState === "account" && accountTab === "history") setAccountHistoryFocus(0)
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
        // The legacy Feria account mock is never a live destination. The shared
        // TopBar emits accountClicked into Main.qml's real account flyout.
        if (viewState === "account") { viewState = "home"; return }
        accountHistoryFocusIndex = -1
        accountContentFocusActive = false
    }
    onAccountTabChanged: {
        accountHistoryFocusIndex = -1
        accountContentFocusActive = false
        if (viewState === "account")
            accountShellFocusIndex = accountFocusForTab(accountTab)
    }
    function sampleSessions() {
        return [
            ["2026-09-24","07:40","frieren","crunchyroll",48],["2026-09-23","22:05","severance","appletv",112],["2026-09-23","19:30","","youtube",35],
            ["2026-09-22","21:15","dune2","hbomax",171],["2026-09-21","18:02","gnx","spotify",44],["2026-09-20","23:10","kagurabachi","mangaplus",22],
            ["2026-09-19","20:45","severance","appletv",96],["2026-09-17","08:15","dtmf","spotify",61],["2026-09-15","22:40","phm","kindle",75],
            ["2026-09-14","21:00","frieren","crunchyroll",94],["2026-09-12","16:20","opmanga","mangaplus",31],["2026-09-10","21:30","thebear","disney",64],
            ["2026-09-08","20:10","phm","kindle",58],["2026-09-03","22:00","frieren","crunchyroll",72],
            ["2026-08-30","21:40","lastofus","hbomax",118],["2026-08-28","19:05","showgirl","spotify",46],["2026-08-25","22:30","arcane","netflix",130],
            ["2026-08-22","20:15","arcane","netflix",84],["2026-08-19","09:00","wok","kindle",88],["2026-08-16","21:45","shogun","disney",121],
            ["2026-08-12","18:30","","youtube",52],["2026-08-09","23:00","dandadan","mangaplus",19],["2026-08-05","21:10","fallout","prime",109],
            ["2026-08-02","20:00","wok","kindle",66],
            ["2026-07-29","21:20","squid","netflix",140],["2026-07-24","19:00","hmhas","spotify",39],["2026-07-20","22:10","theboys","prime",98],
            ["2026-07-14","20:30","lore","webtoon",27],["2026-07-08","21:00","andor","disney",105]
        ].map(function(x) { return {at:new Date(x[0]+"T"+x[1]+":00").getTime(),id:x[2],pk:x[3],mins:x[4],sample:true} })
    }
    function allSessions() {
        return (showSampleHistory ? sampleSessions() : []).concat(recordedSessions).filter(function(s) {
            return Data.P[s.pk] && (!s.id || titleObj(s.id))
        }).sort(function(a,b) { return b.at-a.at })
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
    function toggleRecording() { recording=!recording; settings.historyRecording=recording; clearPending=false }
    function clearHistory() {
        if (!clearPending) { clearPending=true; setToast("Press Clear history again to confirm"); return }
        recordedSessions=[]; showSampleHistory=false; settings.sessionsJson="[]"; settings.historySamples=false; clearPending=false
    }
    function durationText(m) { return m < 1 ? "Under a minute" : m < 60 ? Math.round(m) + " min" : Math.floor(m / 60) + "h " + String(Math.round(m % 60)).padStart(2,"0") + "m" }
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
            if (settings.sessionsJson) {
                var loadedSessions = JSON.parse(settings.sessionsJson)
                recordedSessions = identityBridge && liveDiscovery ? identityBridge.migrateSessions(loadedSessions) : loadedSessions
            }
        } catch (e) { activeApps = Data.DEFAULT_APPS.slice(); saved = [] }
        showSampleHistory=settings.historySamples
        recording=settings.historyRecording
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
                        "account", accountView.visible, accountView.width, accountView.height,
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
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) { shell.back(); event.accepted = true; return }
            if (shell.viewState === "search") return
            if (shell.viewState === "title") {
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) shell.titleActionIndex = Math.min(shell.titleActionCount() - 1, shell.titleActionIndex + 1)
                else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) shell.titleActionIndex = Math.max(0, shell.titleActionIndex - 1)
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) shell.activateTitleAction()
                else return
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
                if (shell.accountTab === "history" && shell.accountHistoryFocusIndex >= 0) {
                    if (event.key === Qt.Key_Down) shell.moveAccountHistoryFocus(1)
                    else if (event.key === Qt.Key_Up) shell.moveAccountHistoryFocus(-1)
                    else if (event.key === Qt.Key_Right && shell.accountHistoryFocusIndex === 0) shell.setAccountHistoryFocus(1)
                    else if (event.key === Qt.Key_Left && shell.accountHistoryFocusIndex === 1) shell.setAccountHistoryFocus(0)
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) shell.activateAccountHistoryFocus()
                    else return
                    event.accepted = true; return
                }
                if (shell.accountTab === "highlights" && shell.accountContentFocusActive) {
                    if (accountView.handleHighlightsKey(event.key)) event.accepted = true
                    return
                }
                if (shell.accountTab === "stats" && accountView.statsContentActive) {
                    if (accountView.handleStatsKey(event.key)) event.accepted = true
                    return
                }
                if (shell.accountTab === "apps" && accountView.appsContentFocused) {
                    if (accountView.handleAppsKey(event.key)) event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Down && shell.accountShellFocusIndex === shell.accountFocusForTab(shell.accountTab)) {
                    if (shell.accountTab === "history") shell.enterAccountHistoryContent()
                    else if (shell.accountTab === "highlights" && !accountView.enterHighlightsContent()) shell.setAccountShellFocus(Math.min(4, shell.accountShellFocusIndex + 1))
                    else if (shell.accountTab === "stats") accountView.enterStatsContent()
                    else if (shell.accountTab === "apps" && !accountView.enterAppsContent()) shell.setAccountShellFocus(4)
                    event.accepted = true; return
                }
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
                    shell.setAccountShellFocus(Math.max(0, shell.accountShellFocusIndex - 1))
                else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
                    shell.setAccountShellFocus(Math.min(4, shell.accountShellFocusIndex + 1))
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    shell.activateAccountShellFocus()
                else return
                event.accepted = true; return
            }
            if (shell.viewState === "host") return
            if (event.key === Qt.Key_BracketLeft) { shell.shiftLens(-1); event.accepted = true; return }
            if (event.key === Qt.Key_BracketRight) { shell.shiftLens(1); event.accepted = true; return }
            if (shell.viewState !== "home") return
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
                    shell.selectApp(shell.focusIndex + d)
                    homeView.revealApp(shell.focusIndex)
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
                    if (v < 0) { shell.focusArea = "feature"; homeView.revealFeature() }
                    else { shell.focusArea = "lens"; homeView.focusLens() }
                } else if (shell.focusArea === "lens") {
                    if (v < 0) { shell.focusArea = "app"; homeView.revealApp(shell.focusIndex) }
                    else if (shell.shownShelves().length) { shell.focusArea = "shelf"; shell.shelfIndex = 0; shell.shelfCardIndex = 0; homeView.revealShelf(0) }
                } else if (shell.focusArea === "shelf") {
                    shell.shelfIndex += v
                    if (shell.shelfIndex < 0) { shell.focusArea = "lens"; shell.shelfIndex = 0; homeView.focusLens() }
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
                    else shell.viewState = "apps"
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
                gradient: Gradient {
                    GradientStop { position: 0; color: shell.dusk }
                    GradientStop { position: 1; color: shell.night }
                }
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
                visible: false
                enabled: false
            }
            PorticoDiscoveryHostView {
                id: hostView
                anchors.fill: parent
                controller: shell
                visible: shell.viewState === "host"
            }
            Colosseum.TopBar {
                id: topbar
                objectName: "feriaTopBar"
                backdrop: shell.backdrop
                activeMedium: shell.medium
                lifecycleActive: shell.lifecycleActive && shell.viewState !== "host"
                visible: shell.viewState !== "host"
                x: theme.margin
                y: 30
                width: shell.width - 2 * theme.margin
                z: 80
                onHomeRequested: shell.goHome()
                onMediumSelected: (medium) => shell.mediumSelected(medium)
                onSearchClicked: shell.openSearch("")
                onAccountClicked: (anchorRight, anchorBottom) => shell.accountClicked(anchorRight, anchorBottom)
                onWallpaperClicked: shell.wallpaperClicked()
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
