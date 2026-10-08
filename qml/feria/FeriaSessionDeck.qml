import QtQuick
import QtQuick.Window
import QtWebEngine
import "PorticoData.js" as ProviderData

// Retained browser objects live under the shared shell, outside FeriaWorld's
// visual Loader. SessionStore remains the single open-activity registry.
Item {
    id: deck
    objectName: "feriaSessionDeck"
    required property var sessions
    required property var accountStore
    required property var policy
    property bool displayed: false
    property bool suppressed: false
    property var entries: ({})
    property var sharedProfile: null
    property var privateProfile: null
    property bool privateProfileRetiring: false
    property bool restoredProfile: false
    readonly property var restoreSource: accountStore.browserRestoreOffers || []
    readonly property int restorableTabCount: restoreSource.length
    property string snapshotRoot: ""
    property int presentationRevision: 0
    property int hostSerial: 0
    property Item fullSizeSurface: null
    property var pendingRetirements: []
    readonly property var closedTabs: { sessions.revision; return activeEntry
        ? sessions.closedBrowserRecords(activeEntry.descriptor.providerId).map(function(r) { return r.descriptor }) : [] }
    readonly property var embeddedConsent: {
        presentationRevision
        for (var id of Object.keys(entries)) {
            var entry = entries[id]
            if (entry.closeConsent && !entry.hostWindow) return entry.closeConsent
        }
        return null
    }
    // The caller reserves visible chrome outside the page viewport. Parenting
    // this presentation there does not expand a sidebar or reveal hidden pages.
    property var consentSurface: null
    property var consentPresentation: null
    readonly property string embeddedDiagnostic: {
        presentationRevision
        for (var id of Object.keys(entries)) {
            var entry = entries[id]
            if (!entry.hostWindow && entry.closeController.failure.length)
                return entry.closeController.failure
        }
        return ""
    }
    function attachConsentSurface(surface) {
        if (!surface || surface.Window.window !== deck.Window.window) return false
        if (consentPresentation) consentPresentation.destroy()
        consentSurface = surface
        consentPresentation = consentFactory.createObject(deck, {parent: surface, deckOwner: deck})
        return consentPresentation !== null
    }
    Component {
        id: consentFactory
        FeriaCloseConsent {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            required property var deckOwner
            deck: deckOwner
            consent: deckOwner.activeEntry && deckOwner.activeEntry.hostWindow
                ? null : deckOwner.embeddedConsent
            diagnostic: deckOwner.activeEntry && deckOwner.activeEntry.hostWindow
                ? "" : deckOwner.embeddedDiagnostic
        }
    }
    readonly property var downloads: sharedProfile ? regularDownloads : null
    // The engine-neutral operation/request seam owned by SessionStore. Both
    // engine controllers and the WebView2 wrapper fence through this object.
    readonly property var operations: sessions ? sessions.browserOperations() : null
    property bool quiescing: false
    property bool holdForCleanup: false
    property bool cleanupNotified: false
    property bool syncing: false
    property bool syncAgain: false
    property var activeEntry: null
    readonly property var activeBrowser: activeEntry ? activeEntry.browser : null
    readonly property string activeError: activeEntry ? activeEntry.errorText : ""
    readonly property bool ready: !quiescing && policy.storageRoot.length > 0
    signal quiesced()
    signal quiescenceFailed(string reason)
    Timer {
        id: closeDeadline
        interval: 15000
        onTriggered: if (deck.quiescing && !deck.cleanupNotified)
            deck.quiescenceFailed("A browser is still closing. Sign-out has not run; retry after it closes.")
    }
    signal message(string sessionId, string text)

    FeriaBrowserDownloads {
        id: regularDownloads
        profile: deck.sharedProfile
        accountStore: deck.accountStore
        onMessage: (text) => deck.message(deck.sessions.activeId, text)
    }
    FeriaBrowserDownloads {
        id: privateDownloads
        profile: deck.privateProfile
        accountStore: deck.accountStore
        privateBrowsing: true
        onMessage: (text) => deck.message(deck.sessions.activeId, text)
    }
    function showDownloads() {
        if (activeEntry && activeEntry.descriptor.privateBrowsing) privateDownloads.openPanel()
        else regularDownloads.openPanel()
    }
    function tabDescriptors() {
        return sessions.browsers().map(function(record) {
            var descriptor = Object.assign({}, record.target)
            descriptor.title = record.title
            descriptor.restoreId = descriptor.restoreId || record.id
            return descriptor
        })
    }
    function persistTabs() {
        if (ready && !quiescing && captureRestoreSource()) accountStore.saveBrowserTabs(tabDescriptors())
    }
    // AccountStore owns durable offers independently of the current snapshot.
    function captureRestoreSource() {
        var captured = accountStore.captureBrowserRestoreOffers()
        if (!captured)
            message("", "Could not prepare previous spaces. The saved snapshot is still available.")
        return captured
    }
    function restorableCountForProvider(provider) {
        return restoreSource.filter(function(row) { return !provider || row.providerId === provider }).length
    }
    function dismissRestoreTabs(provider) {
        var pending = restoreSource.filter(function(row) { return provider && row.providerId !== provider })
        if (!accountStore.saveBrowserRestoreOffers(pending)) {
            message("", "Could not dismiss previous spaces. Try again.")
            return false
        }
        for (var id of Object.keys(entries)) {
            if (!provider || entries[id].descriptor.providerId === provider) entries[id].restoreOfferId = ""
        }
        return true
    }
    function completeRestore(entry) {
        if (!entry.restoreOfferId || entry.retiring || entries[entry.sessionId] !== entry) return
        var pending = restoreSource.filter(function(row) { return row.restoreId !== entry.restoreOfferId })
        if (accountStore.saveBrowserRestoreOffers(pending)) entry.restoreOfferId = ""
        else message(entry.sessionId, "The page opened, but its restore offer could not be saved. Try Restore again.")
    }
    Timer {
        id: snapshotTimer
        interval: 250
        onTriggered: if (deck.snapshotRoot === deck.policy.storageRoot) deck.persistTabs()
    }
    function scheduleSnapshot() {
        if (quiescing || !ready) return
        snapshotRoot = policy.storageRoot
        snapshotTimer.restart()
    }
    function browserForSession(id) {
        var entry = entries[id]
        return ready && !quiescing && entry && !entry.retiring ? entry.browser : null
    }
    function navigationForSession(id) {
        var browser = browserForSession(id)
        return browser ? browser.navigation : null
    }
    function dispatchNavigation(id, action) {
        var entry = entries[id], nav = navigationForSession(id)
        if (!entry || !nav) return false
        if (entry.closePending) {
            entry.pendingNavigation = {action: action}
            if (entry.closeController.cancelForNavigation()) return true
            entry.pendingNavigation = null
            return false
        }
        if (!entry.closeController.prepareNavigation()) return false
        return nav[action]()
    }
    function back(id) { return dispatchNavigation(id, "back") }
    function forward(id) { return dispatchNavigation(id, "forward") }
    function reload(id) { return dispatchNavigation(id, "reload") }
    function replayLoaderNavigation(forEntry) {
        if (!forEntry) return
        Qt.callLater(function() {
            if (deck.entries[forEntry.sessionId] !== forEntry) return
            var pending = forEntry.pendingNavigation
            if (pending) deck.navigateSession(forEntry.sessionId, pending.url,
                pending.position, pending.locator, pending.context)
        })
    }
    function resumePendingNavigation(id) {
        var entry = entries[id]
        if (!entry || entry.retiring || entry.closePending) return
        var pending = entry.pendingNavigation
        entry.pendingNavigation = null
        if (!pending) return
        if (pending.action) dispatchNavigation(id, pending.action)
        else navigateSession(id, pending.url, pending.position, pending.locator, pending.context)
    }
    function hostLeaseCurrent(lease) {
        if (!lease || !lease.spaceId || !ready) return false
        var entry = entries[lease.spaceId], fresh = sessions.browserIdentity(lease.spaceId)
        if (!entry || entry.retiring || !entry.browser) return false
        // A host lease survives navigation; a fresh document identity is read
        // at each dispatch. No queued host event crosses a host/account/page move.
        var keys = ["accountEpoch", "profileId", "spaceId", "pageGeneration", "hostId", "hostGeneration"]
        for (var key of keys) if (lease[key] !== fresh[key]) return false
        return true
    }
    function hostCommand(lease, action, value) {
        if (!hostLeaseCurrent(lease)) return false
        var id = lease.spaceId
        if (action === "navigate") return navigateInput(id, value)
        if (action === "back") return back(id)
        if (action === "forward") return forward(id)
        if (action === "reload") return reload(id)
        if (action === "close") return closeTab(id)
        if (action === "restore") return restoreSession(id)
        if (action === "activate") { sessions.switchTo(id); return true }
        return false
    }
    function stop(id) { var nav = navigationForSession(id); return nav ? nav.stop() : false }
    function historyOffset(id, offset, revision) {
        var nav = navigationForSession(id)
        return nav ? nav.goToOffset(offset, revision) : false
    }
    function navigateInput(id, text, searchEngine) {
        var url = String(policy.omniboxUrl(text, searchEngine || "duckduckgo"))
        return url.length > 0 && navigateSession(id, url, 0, null, null)
    }
    function engineAdmitted(engineId) {
        // Engine selection is explicit and validated before allocation; an
        // unavailable engine is a visible blocked state, never a fallback.
        return engineId !== "webview2" || policy.webView2Available
    }
    function newSpace(sourceId, privateBrowsing, activate) {
        var source = entries[sourceId]
        if (!source || source.retiring || !ready) return ""
        if (!engineAdmitted(source.descriptor.engineId)) {
            message(sourceId, "The WebView2 engine is not available in this build.")
            return ""
        }
        var descriptor = Object.assign({}, source.descriptor)
        var provider = ProviderData.P[descriptor.providerId]
        // Explicit fixture/integration startUrl is validated, never a page URL.
        var start = String(descriptor.startUrl || (provider ? "https://" + provider.d : ""))
        var normalizedStart = String(policy.navigationUrl(start))
        if (!start || !normalizedStart.length) return ""
        descriptor.url = normalizedStart
        descriptor.title = descriptor.providerName || descriptor.providerId
        descriptor.privateBrowsing = Boolean(privateBrowsing)
        delete descriptor.restoreId
        delete descriptor.position; delete descriptor.locator; delete descriptor.titleId
        return sessions.openBrowser(descriptor, true, false, activate !== false)
    }
    // Compatibility facade; new presentation supplies an explicit source ID.
    function newTab(privateBrowsing, url, activate) {
        if (!activeEntry) return ""
        if (!url) return newSpace(activeEntry.sessionId, privateBrowsing, activate)
        if (!ready || !policy.allowsNavigation(url)) return ""
        if (!engineAdmitted(activeEntry.descriptor.engineId)) {
            message(activeEntry.sessionId, "The WebView2 engine is not available in this build.")
            return ""
        }
        var descriptor = Object.assign({}, activeEntry.descriptor)
        descriptor.url = url
        descriptor.privateBrowsing = Boolean(privateBrowsing)
        delete descriptor.restoreId
        delete descriptor.position; delete descriptor.locator; delete descriptor.titleId
        return sessions.openBrowser(descriptor, true, false, activate !== false)
    }
    function closeTab(id) {
        var entry = entries[id]
        return entry && !entry.retiring && entry.browser ? entry.closeController.begin(true) : false
    }
    function answerClose(id, attempt, leave) {
        var entry = entries[id]
        return entry && !entry.retiring ? entry.closeController.answer(attempt, leave) : false
    }
    function savePlacement(entry) {
        if (!entry.savedPlacement) entry.savedPlacement = {parent: entry.parent,
            x: entry.x, y: entry.y, width: entry.width, height: entry.height,
            focused: entry.browser && entry.browser.activeFocus, fill: entry.anchors.fill}
    }
    function disposeHost(entry) {
        var host = entry.hostWindow, full = entry.fullSizeHost
        entry.hostWindow = null; entry.fullSizeHost = null
        if (host) { host.retiring = true; host.hide(); host.destroy() }
        if (full) {
            full.destroy()
        }
    }
    function moveToDesktop(id, geometry, dialogWindow, originatingWindow, originatingScreenName) {
        var entry = entries[id]
        if (!entry || !entry.browser || entry.retiring || !entry.closeController.canTransfer()) return false
        if (entry.hostWindow) { entry.hostWindow.show(); entry.hostWindow.raise(); entry.hostWindow.requestActivate(); return true }
        if (entry.fullSizeHost && !restoreSession(id)) return false
        var host = desktopHostFactory.createObject(deck, {deck: deck, sessionId: id, dialogWindow: !!dialogWindow})
        if (!host) return false
        var origin = originatingWindow || entry.browser.Window.window
        if (origin && origin.screen) host.screen = origin.screen
        else {
            // QQuickView/plain QQuickWindow lacks QML Window.screen. Resolve
            // its attached Screen against the public application screen list.
            var screenName = originatingScreenName || entry.browser.Screen.name
            for (var screen of Qt.application.screens) {
                if (screen.name === screenName) { host.screen = screen; break }
            }
        }
        // Copy the value type while the original stack-backed Qt request lives.
        var rect = geometry ? Qt.rect(Number(geometry.x), Number(geometry.y),
            Number(geometry.width), Number(geometry.height)) : Qt.rect(100, 100, 900, 700)
        if (!host.applyGeometry(rect)) { host.destroy(); return false }
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "desktop:" + (++hostSerial))
        if (!identity.spaceId) { host.destroy(); return false }
        entry.closeController.hostTransferred(identity)
        host.lease = Object.assign({}, identity)
        savePlacement(entry)
        entry.hostWindow = host
        entry.parent = host.viewport
        entry.anchors.fill = host.viewport
        host.show()
        // Frames become measurable after native exposure; apply the same clamp
        // again with actual screen/frame data, without changing page ownership.
        Qt.callLater(function() {
            if (host && deck.entries[id] === entry && entry.hostWindow === host
                && !host.retiring && deck.hostLeaseCurrent(host.lease)) host.applyGeometry(rect)
        })
        host.requestActivate()
        entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function maximizeSession(id, surface) {
        var entry = entries[id]
        if (!entry || !entry.browser || entry.retiring || !entry.closeController.canTransfer() || entry.hostWindow
            || !surface || surface.Window.window !== deck.Window.window) return false
        if (entry.fullSizeHost) return true
        var host = fullSizeHostFactory.createObject(deck, {parent: surface, deck: deck, sessionId: id})
        if (!host) return false
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "app-fullsize:" + (++hostSerial))
        if (!identity.spaceId) { host.destroy(); return false }
        entry.closeController.hostTransferred(identity)
        host.lease = Object.assign({}, identity)
        savePlacement(entry)
        entry.fullSizeHost = host
        surface.visible = Qt.binding(function() {
            return deck.displayed && !deck.suppressed && !!deck.activeEntry
                && !!deck.activeEntry.fullSizeHost && deck.activeEntry.fullSizeHost.parent === surface
        })
        entry.parent = host.viewport
        entry.anchors.fill = host.viewport
        entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function returnToEmbedded(id) {
        var entry = entries[id]
        if (!entry || entry.retiring || !entry.closeController.canTransfer() || entry.restoring || !entry.savedPlacement) return false
        // Capture and guard BEFORE the synchronous native changed signal can
        // re-enter selectActive while switching from maximized A to neighbor B.
        var saved = entry.savedPlacement
        entry.restoring = true
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "embedded")
        if (!identity.spaceId) { entry.restoring = false; return false }
        entry.closeController.hostTransferred(identity)
        entry.savedPlacement = null
        entry.anchors.fill = null
        entry.parent = saved.parent
        entry.x = saved.x; entry.y = saved.y
        entry.width = saved.width; entry.height = saved.height
        entry.anchors.fill = saved.fill
        disposeHost(entry)
        entry.restoring = false
        if (saved.focused && entry.browser && sessions.activeId === id && displayed && !suppressed)
            entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function restoreSession(id) {
        var entry = entries[id]
        return entry && entry.fullSizeHost ? returnToEmbedded(id) : false
    }
    function restoreInactiveMax(id) {
        var entry = entries[id]
        if (entry && entry.fullSizeHost && !entry.restoring && !entry.retiring
            && sessions.activeId !== id && entry.closeController.canTransfer()) restoreSession(id)
    }
    function toggleAppMax(id) {
        var entry = entries[id]
        if (!entry || entry.retiring || entry.hostWindow) return false
        return entry.fullSizeHost ? restoreSession(id) : maximizeSession(id, fullSizeSurface)
    }
    Component { id: desktopHostFactory; FeriaDesktopHost {} }
    Component { id: fullSizeHostFactory; FeriaFullSizeHost {} }
    function openWindow(sourceId, request) {
        var source = entries[sourceId]
        if (!source || source.retiring || !source.browser || !ready || !request.userInitiated) return
        var admission = source.browser.windowAdmission
        if (!admission || admission.consumed || admission.request !== request
            || admission.profile !== source.browser.profile || admission.sessionId !== sourceId
            || admission.documentGeneration !== source.browser.navigationGeneration) return
        admission.consumed = true // before any canonical allocation or host creation
        var sourceIdentity = sessions.browserIdentity(sourceId)
        var nativeDestination = request.destination === WebEngineNewWindowRequest.InNewWindow
            || request.destination === WebEngineNewWindowRequest.InNewDialog
        if (!nativeDestination && request.destination !== WebEngineNewWindowRequest.InNewTab
            && request.destination !== WebEngineNewWindowRequest.InNewBackgroundTab) return
        var originProfile = source.browser.profile
        var originRoot = policy.storageRoot
        var destination = String(request.requestedUrl || "about:blank")
        if (!policy.allowsNavigation(destination)) return
        var descriptor = Object.assign({}, source.descriptor)
        descriptor.url = "about:blank"
        descriptor.title = "New tab"
        // An empty initial URL avoids queueing script callbacks on a temporary
        // blank WebContents that openIn() will replace immediately.
        descriptor.adoptWindowRequest = true
        delete descriptor.restoreId
        delete descriptor.position; delete descriptor.locator; delete descriptor.titleId
        var foreground = request.destination !== WebEngineNewWindowRequest.InNewBackgroundTab
        var id = sessions.openBrowser(descriptor, true, false, foreground)
        var entry = entries[id]
        // Adopt the original request synchronously: preserving opener/form/SSO
        // state requires openIn(), rather than reopening just its URL.
        if (ready && policy.storageRoot === originRoot && entries[sourceId] === source
            && source.closeController.identityMatches(sourceIdentity)
            && entry && entry.browser && entry.browser.profile === originProfile) {
            // Allocate the disposable native host before accepting the original
            // request; failed presentation rolls back the canonical placeholder.
            if (nativeDestination && !moveToDesktop(id, request.requestedGeometry,
                    request.destination === WebEngineNewWindowRequest.InNewDialog, source.browser.Window.window, source.browser.Screen.name)) {
                sessions.close(id)
                message(sourceId, "Could not create the requested window")
                return
            }
            // Track the actual creation-view dependency, not QObject ownership.
            // New scoped close routes block its retirement until children die.
            // No hidden opener shell/lease workaround is enabled here.
            if (!policy.independentAdoptionAvailable(entry.browser)) {
                entry.creationEntry = source
                ++source.liveAdoptedChildren
            }
            entry.beginAdoption()
            request.openIn(entry.browser)
            entry.browser.adoptingWindowRequest = false
            // openIn is void. Only subsequent real view events mark observation;
            // an unobserved placeholder is rolled back, never timed into success.
        }
        else { if (id) sessions.close(id); message(sourceId, "Could not open the new tab") }
    }
    function restoreTabs(provider) {
        if (!ready || !captureRestoreSource()) return
        provider = provider || (activeEntry ? activeEntry.descriptor.providerId : "")
        var saved = restoreSource.filter(function(row) { return !provider || row.providerId === provider })
        var existing = sessions.browsers().slice()
        var used = ({})
        var last = ""
        restoredProfile = true
        for (var descriptor of saved) {
            var candidates = existing.filter(function(record) {
                return !used[record.id] && !record.target.privateBrowsing
                    && record.target.providerId === descriptor.providerId
            })
            var match = candidates.find(function(record) { return record.target.restoreId === descriptor.restoreId })
                || candidates.find(function(record) { return !record.target.restoreId && record.target.url === descriptor.url })
            var id = match ? match.id : sessions.openBrowser(descriptor, true, false, false)
            var entry = entries[id]
            if (!entry || !entry.browser) continue
            last = id; used[id] = true
            entry.restoreOfferId = descriptor.restoreId
            if (entry.mainLoadSucceeded) completeRestore(entry)
            else if (match && !entry.browser.loading) reload(id)
        }
        if (last) sessions.switchTo(last)
    }
    function reopenClosedSpace(provider) {
        if (!ready || !provider) return ""
        var record = sessions.prepareBrowserReopen(provider)
        if (!record.recordId || !record.descriptor || !policy.allowsNavigation(record.descriptor.url)) return ""
        if (!engineAdmitted(record.descriptor.engineId)) {
            message("", "That space was saved for the WebView2 engine, which is not available in this build.")
            return ""
        }
        var id = sessions.openBrowser(record.descriptor, true, false, true)
        var entry = entries[id]
        if (!entry || !entry.browser) {
            if (id) sessions.close(id)
            message("", "Could not create the reopened space. Its closed record is still available.")
            return ""
        }
        entry.reopenRecordId = record.recordId
        // Loader/DOM readiness is not a successful reconstruction. Only the
        // wrapper's actual main-document success event commits this record.
        return id
    }
    function restoreClosedTab() {
        return activeEntry ? reopenClosedSpace(activeEntry.descriptor.providerId) : ""
    }
    function considerRestore() {
        if (!ready || restoredProfile) return
        restoredProfile = true
        // Previous spaces remain parked as offers until an explicit app action.
    }

    Component {
        id: profileFactory
        WebEngineProfile {
            required property string accountRoot
            property bool privateMode: false
            storageName: privateMode ? "" : "feria"
            offTheRecord: privateMode
            persistentStoragePath: privateMode ? "" : accountRoot + "/qtwebengine"
            cachePath: privateMode ? "" : accountRoot + "/qtwebengine-cache"
            persistentCookiesPolicy: privateMode ? WebEngineProfile.NoPersistentCookies : WebEngineProfile.ForcePersistentCookies
        }
    }
    function selectActive(activate) {
        var next = entries[sessions.activeId] || null
        var selectionChanged = activeEntry !== next
        if (activeEntry && activeEntry !== next && activeEntry.fullSizeHost
            && activeEntry.closeController.canTransfer() && !activeEntry.restoring)
            restoreSession(activeEntry.sessionId)
        activeEntry = next
        if (!activate && !selectionChanged) return
        if (activeEntry && activeEntry.hostWindow) {
            if (activeEntry.hostWindow.visibility === Window.Minimized) activeEntry.hostWindow.showNormal()
            else activeEntry.hostWindow.show()
            activeEntry.hostWindow.raise()
            activeEntry.hostWindow.requestActivate()
        } else if (activeEntry && deck.Window.window) {
            deck.Window.window.requestActivate()
        }
    }
    function releasePrivateProfile() {
        if (quiescing || !privateProfileRetiring || !privateProfile) return
        if (policy.runtimeViewsForProfile(privateProfile) > 0) return
        privateDownloads.cancelActive()
        var old = privateProfile
        privateProfile = null
        old.destroy()
    }
    function syncSessions() {
        if (syncing) { syncAgain = true; return }
        syncing = true
        var live = ({})
        var records = sessions.browsers()
        if (privateProfile && !records.some(function(r) { return r.target.privateBrowsing })) {
            privateProfileRetiring = true
            Qt.callLater(releasePrivateProfile)
        }
        for (var record of records) live[record.id] = record
        for (var id of Object.keys(entries)) {
            if (live[id] && !quiescing) continue
            var previous = entries[id]
            delete entries[id]
            accountStore.endSessionVisit(id)
            if (quiescing) pendingRetirements = pendingRetirements.concat([previous])
            else previous.retire() // inherited direct-teardown input hazard is documented
        }
        if (ready) {
            if (!sharedProfile && records.some(function(r) { return !r.target.privateBrowsing })) {
                sharedProfile = profileFactory.createObject(deck, { accountRoot: policy.storageRoot })
                if (sharedProfile) policy.registerRuntimeProfile(sharedProfile)
            }
            if (!privateProfile && !privateProfileRetiring && records.some(function(r) { return r.target.privateBrowsing })) {
                privateProfile = profileFactory.createObject(deck, { accountRoot: policy.storageRoot, privateMode: true })
                if (privateProfile) policy.registerRuntimeProfile(privateProfile)
            }
            for (var current of records) {
                if (entries[current.id]) continue
                var descriptor = Object.assign({}, current.target)
                var adopting = descriptor.adoptWindowRequest === true
                delete descriptor.adoptWindowRequest
                var entry = sessionFactory.createObject(deck, {
                    sessionId: current.id, descriptor: descriptor, initialWindowRequest: adopting
                })
                if (entry) entries[current.id] = entry
            }
        }
        ++presentationRevision
        selectActive()
        syncing = false
        if (syncAgain) { syncAgain = false; Qt.callLater(syncSessions) }
        if (quiescing) { Qt.callLater(drainRetirements); Qt.callLater(advanceQuiescence) }
    }
    function drainRetirements() {
        var waiting = []
        for (var entry of pendingRetirements) {
            if (!entry) continue
            if (entry.liveAdoptedChildren === 0) entry.retire()
            else waiting.push(entry)
        }
        pendingRetirements = waiting
    }
    function startQuiescence(forCleanup) {
        quiescing = true
        snapshotTimer.stop()
        regularDownloads.cancelActive()
        privateDownloads.cancelActive()
        closeDeadline.restart()
        holdForCleanup = forCleanup
        cleanupNotified = false
        sessions.closeBrowserSessions()
        syncSessions()
        advanceQuiescence()
    }
    function finishCleanup() {
        holdForCleanup = false
        advanceQuiescence()
    }
    function advanceQuiescence() {
        if (!quiescing || policy.runtimeViewCount > 0) return
        if (sharedProfile) {
            var profile = sharedProfile
            sharedProfile = null
            profile.destroy()
            return
        }
        if (privateProfile) {
            var privateContext = privateProfile
            privateProfile = null
            privateContext.destroy()
            return
        }
        if (policy.runtimeProfileCount > 0) return
        closeDeadline.stop()
        if (holdForCleanup) {
            if (!cleanupNotified) { cleanupNotified = true; quiesced() }
        } else {
            quiescing = false
            syncSessions()
        }
    }
    function navigateSession(id, url, position, locator, context) {
        var entry = entries[id]
        if (!entry || !policy.allowsNavigation(url) || quiescing) return false
        if (!entry.browser) {
            entry.pendingNavigation = { url: url, position: position || 0, locator: locator || null, context: context }
            return true
        }
        if (entry.closePending) {
            entry.pendingNavigation = {url: url, position: position || 0, locator: locator || null, context: context}
            if (entry.closeController.cancelForNavigation()) return true
            entry.pendingNavigation = null
            return false
        }
        if (!entry.closeController.prepareNavigation()) return false
        entry.pendingNavigation = null
        entry.browser.wakePage()
        if (context) entry.visitContext = context
        entry.token = accountStore.beginSessionVisit(id, {
            pk: entry.descriptor.providerId, resumePending: position > 0,
            privateBrowsing: entry.descriptor.privateBrowsing === true,
            title: entry.visitContext.title || "", cover: entry.visitContext.cover || ""
        })
        entry.browser.accountVisitToken = entry.token
        entry.browser.cancelMediaResume(true)
        entry.browser.resumePosition = position || 0
        entry.browser.resumeLocator = locator || null
        entry.browser.sourceUrl = url
        entry.browser.url = url
        return true
    }
    function savePlace() {
        if (!activeEntry || !activeBrowser) return false
        return accountStore.saveSessionReadingPlace(activeEntry.sessionId, activeEntry.token,
                                                     String(activeBrowser.url), activeBrowser.title)
    }
    Connections {
        target: deck.sessions
        function onChanged() { deck.syncSessions(); deck.scheduleSnapshot() }
        function onActiveChangedProp() { deck.selectActive(true) }
    }
    Connections {
        target: deck.accountStore
        function onProfileChanged() {
            snapshotTimer.stop()
            deck.restoredProfile = false
            deck.captureRestoreSource()
            Qt.callLater(deck.considerRestore)
        }
    }
    Connections {
        target: deck.policy
        function onStorageRootChanged() { deck.startQuiescence(false) }
        function onRuntimeViewCountChanged() {
            Qt.callLater(deck.advanceQuiescence)
            Qt.callLater(deck.releasePrivateProfile)
        }
        function onRuntimeProfileCountChanged() {
            if (deck.privateProfileRetiring && !deck.privateProfile) {
                deck.privateProfileRetiring = false
                Qt.callLater(deck.syncSessions)
            }
            Qt.callLater(deck.advanceQuiescence)
        }
    }
    onReadyChanged: if (ready) Qt.callLater(considerRestore)
    Component.onCompleted: { captureRestoreSource(); syncSessions(); Qt.callLater(considerRestore) }
    Component.onDestruction: {
        persistTabs()
        for (var id of Object.keys(entries)) accountStore.endSessionVisit(id)
    }

    Component {
        id: sessionFactory
        Item {
            id: entry
            required property string sessionId
            readonly property var ownerDeck: deck
            required property var descriptor
            property bool initialWindowRequest: false
            // Explicit, validated engine selection: an admitted webview2
            // preference uses the Windows adapter; anything else keeps the
            // working QtWebEngine path unchanged.
            readonly property bool wantsWebView2: descriptor.engineId === "webview2"
            readonly property bool usesWebView2: wantsWebView2 && deck.policy.webView2Available
            // An explicitly requested engine that is unavailable on this
            // build blocks the space visibly: no silent fallback to another
            // engine, and the descriptor/partition request is preserved.
            readonly property bool engineBlocked: wantsWebView2 && !deck.policy.webView2Available
            property var visitContext: descriptor.visitContext || ({ title: descriptor.title || "", cover: "" })
            property string token: ""
            property bool retiring: false
            readonly property bool immersive: fullSizeHost !== null
            property var hostWindow: null
            property var fullSizeHost: null
            property var savedPlacement: null
            property bool restoring: false
            property var creationEntry: null
            property int liveAdoptedChildren: 0
            property string adoptionState: "none"
            function beginAdoption() { adoptionState = "pending"; adoptionDeadline.restart() }
            function observeAdoption() {
                if (adoptionState !== "pending") return
                adoptionState = "observed"
                adoptionDeadline.stop()
            }
            Timer {
                id: adoptionDeadline
                interval: 10000
                onTriggered: {
                    if (entry.adoptionState !== "pending" || deck.entries[entry.sessionId] !== entry) return
                    entry.adoptionState = "unobserved-failed"
                    deck.sessions.close(entry.sessionId)
                    deck.message(entry.sessionId, "The requested window did not initialize. Its placeholder was removed.")
                }
            }
            property string reopenRecordId: ""
            property string restoreOfferId: ""
            property bool mainLoadSucceeded: false
            property bool loadWasStopped: false
            function rollbackReopen(recordId) {
                if (retiring || !recordId || reopenRecordId !== recordId
                    || deck.entries[sessionId] !== entry) return
                reopenRecordId = ""
                deck.sessions.close(sessionId)
                deck.message(sessionId, "The space could not reopen. Its closed record is still available.")
            }
            function deferReopenFailure(generation, location) {
                if (!reopenRecordId || !browser) return
                var recordId = reopenRecordId
                // Older abort notifications may trail a newer provisional load.
                // Recheck this exact reconstruction after queued load signals;
                // this is rollback arbitration, never a close-consent ACK.
                Qt.callLater(function() {
                    if (!entry || entry.retiring || deck.entries[entry.sessionId] !== entry
                        || entry.reopenRecordId !== recordId || !entry.browser
                        || entry.browser.navigationGeneration !== generation
                        || entry.browser.loading || entry.browser.replacementLoadPending) return
                    if (String(location) !== String(entry.browser.url)
                        && String(location) !== String(entry.browser.sourceUrl)) return
                    entry.rollbackReopen(recordId)
                })
            }
            readonly property alias closeController: closeControllerLoader.item
            readonly property var closeConsent: closeController ? closeController.consent : null
            readonly property bool closePending: closeController ? closeController.pending : false
            Component {
                id: qtCloseFactory
                FeriaCloseController {
                    browser: entry.browser; sessions: deck.sessions; sessionId: entry.sessionId
                    retirementAllowed: entry.liveAdoptedChildren === 0
                    onBlocked: (reason) => { entry.errorText = reason; deck.message(entry.sessionId, reason) }
                    onCancellationSettled: Qt.callLater(deck.resumePendingNavigation, entry.sessionId)
                    onChanged: {
                        ++deck.presentationRevision
                        Qt.callLater(deck.restoreInactiveMax, entry.sessionId)
                    }
                    onFailureChanged: {
                        entry.errorText = failure
                        ++deck.presentationRevision
                        if (failure.length) deck.message(entry.sessionId, failure)
                    }
                }
            }
            Component {
                id: webView2CloseFactory
                FeriaWebView2CloseController {
                    browser: entry.browser; sessions: deck.sessions; sessionId: entry.sessionId
                    retirementAllowed: entry.liveAdoptedChildren === 0
                    onBlocked: (reason) => { entry.errorText = reason; deck.message(entry.sessionId, reason) }
                    onCancellationSettled: Qt.callLater(deck.resumePendingNavigation, entry.sessionId)
                    onChanged: {
                        ++deck.presentationRevision
                        Qt.callLater(deck.restoreInactiveMax, entry.sessionId)
                    }
                    onFailureChanged: {
                        entry.errorText = failure
                        ++deck.presentationRevision
                        if (failure.length) deck.message(entry.sessionId, failure)
                    }
                }
            }
            Loader {
                id: closeControllerLoader
                sourceComponent: entry.usesWebView2 ? webView2CloseFactory : qtCloseFactory
            }
            property string errorText: engineBlocked
                ? "The WebView2 engine is not available in this build. This space stays closed to protect its saved engine partition."
                : ""
            onEngineBlockedChanged: if (engineBlocked) errorText =
                "The WebView2 engine is not available in this build. This space stays closed to protect its saved engine partition."
            property var pendingNavigation: null
            readonly property var browser: browserLoader.item
            anchors.fill: parent
            visible: !retiring && (hostWindow ? hostWindow.visible
                : deck.displayed && deck.sessions.activeId === sessionId && !deck.suppressed)
            function retire() {
                if (retiring) return
                retiring = true
                adoptionDeadline.stop()
                closeController.invalidate(false)
                // Detach from a disposable host before destroying it. QObject
                // ownership remains on the deck throughout this visual move.
                anchors.fill = null
                parent = deck
                deck.disposeHost(entry)
                if (browser) browser.prepareForClose(closeController.engineClosed)
                else destroy()
            }
            Component.onDestruction: {
                if (creationEntry) --creationEntry.liveAdoptedChildren
                Qt.callLater(deck.drainRetirements)
            }
            Component.onCompleted: {
                token = deck.accountStore.beginSessionVisit(sessionId, {
                    pk: descriptor.providerId, resumePending: Number(descriptor.position || 0) > 0,
                    privateBrowsing: descriptor.privateBrowsing === true,
                    title: visitContext.title || "", cover: visitContext.cover || ""
                })
            }
            FeriaCloseConsent {
                id: consentDock
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                deck: entry.ownerDeck
                consent: entry.hostWindow || !entry.ownerDeck.consentPresentation
                    ? entry.closeConsent || (deck.sessions.activeId === entry.sessionId ? deck.embeddedConsent : null) : null
                diagnostic: entry.hostWindow
                    ? (deck.sessions.activeId === entry.sessionId ? deck.embeddedDiagnostic : "")
                    : !entry.ownerDeck.consentPresentation ? closeController.failure : ""
            }
            Component {
                id: qtViewFactory
                FeriaQtWebEngine {
                    sourceUrl: entry.initialWindowRequest ? "" : entry.descriptor.url
                    adoptingWindowRequest: entry.initialWindowRequest
                    managedBeforeUnload: true
                    profilePath: deck.policy.storageRoot
                    sessions: deck.sessions
                    sharedProfile: entry.descriptor.privateBrowsing ? deck.privateProfile : deck.sharedProfile
                    providerId: entry.descriptor.providerId
                    accountVisitToken: entry.token
                    accountSessionId: entry.sessionId
                    observationMode: entry.descriptor.mode || "watch"
                    observationEnabled: deck.accountStore.recording && !entry.descriptor.privateBrowsing
                    resumePosition: Number(entry.descriptor.position || 0)
                    resumeLocator: entry.descriptor.locator || null
                    suppressed: !entry.visible
                    freezeBackground: deck.accountStore.browserPreferences.freezeBackground !== false
                    lifecycleBlocked: entry.closeController ? entry.closeController.busy : false
                    activitySuspended: !deck.displayed
                        || (deck.Window.window && deck.Window.window.visibility === Window.Minimized)
                        || (entry.hostWindow && entry.hostWindow.visibility === Window.Minimized)
                }
            }
            Loader {
                id: browserLoader
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.top: consentDock.bottom
                active: !entry.engineBlocked && entry.token.length > 0
                    && (entry.descriptor.privateBrowsing
                    ? deck.privateProfile !== null && (!deck.privateProfileRetiring || browserLoader.item !== null)
                    : deck.sharedProfile !== null)
                // The WebView2 view loads by URL so Qt-only harnesses never
                // compile the native Colosseum.FeriaHost import: the module
                // is registered where the engine is actually available.
                source: entry.usesWebView2 ? "FeriaWebView2.qml" : ""
                sourceComponent: entry.usesWebView2 ? null : qtViewFactory
                onStatusChanged: if (status === Loader.Error) entry.errorText = "The browser could not start."
                onLoaded: {
                    if (entry.usesWebView2) {
                        // Immutable profile/identity bindings are applied
                        // before the initial URL so the native partition is
                        // configured ahead of the first navigation.
                        item.profilePath = entry.descriptor.privateBrowsing
                            ? deck.policy.storageRoot + "/webview2-private-" + entry.sessionId
                            : deck.policy.storageRoot
                        item.accountSessionId = Qt.binding(function() { return entry.sessionId })
                        item.sessions = Qt.binding(function() { return deck.sessions })
                        item.suppressed = Qt.binding(function() { return !entry.visible })
                        item.sourceUrl = entry.initialWindowRequest ? "" : entry.descriptor.url
                        item.providerId = Qt.binding(function() { return entry.descriptor.providerId })
                        item.accountVisitToken = Qt.binding(function() { return entry.token })
                        item.observationMode = Qt.binding(function() { return entry.descriptor.mode || "watch" })
                        item.observationEnabled = Qt.binding(function() {
                            return deck.accountStore.recording && !entry.descriptor.privateBrowsing })
                        item.resumePosition = Qt.binding(function() { return Number(entry.descriptor.position || 0) })
                        item.resumeLocator = Qt.binding(function() { return entry.descriptor.locator || null })
                    }
                    // A navigation queued while the loader was creating the
                    // view is replayed for either engine once it exists.
                    deck.replayLoaderNavigation(entry)
                }
            }
            Connections {
                target: entry.browser
                // Engine adapters expose different signal subsets (e.g. the
                // WebView2 wrapper has no renderer/safeToDestroy signals).
                ignoreUnknownSignals: true
                function onSafeToDestroy() {
                    if (entry.retiring) Qt.callLater(function() { entry.destroy() })
                }
                function onStarted(location) {
                    entry.observeAdoption()
                    entry.mainLoadSucceeded = false
                    if (!entry.browser.rendererLost) entry.errorText = ""
                    if (!entry.descriptor.privateBrowsing) deck.policy.rememberOrigin(entry.descriptor.providerId, location)
                    deck.sessions.updateBrowser(entry.sessionId, location, "")
                }
                function onStopped(location) {
                    // A cancelled/stopped load leaves the previous page
                    // usable: no failure error for its terminal.
                    entry.loadWasStopped = true
                }
                function onCompleted(location, success) {
                    if (entry.retiring) return
                    entry.observeAdoption()
                    if (entry.browser.rendererLost) entry.errorText = "The browser page stopped responding. Reload to recover; unsaved page state may be lost."
                    else if (!success && !entry.loadWasStopped) entry.errorText = closeController.failure || "Could not open the page. Try Reload."
                    entry.loadWasStopped = false
                    deck.sessions.updateBrowser(entry.sessionId, location,
                                                entry.browser ? entry.browser.title : "")
                }
                function onMainDocumentLoaded(location) {
                    if (entry.retiring || deck.entries[entry.sessionId] !== entry || !entry.browser) return
                    entry.mainLoadSucceeded = true
                    entry.errorText = closeController.failure
                    deck.sessions.updateBrowser(entry.sessionId, location, entry.browser.title)
                    deck.accountStore.recordBrowserVisit(entry.sessionId, entry.token, location, entry.browser.title)
                    deck.completeRestore(entry)
                }
                function onDocumentReplaced(generation) {
                    if (entry.retiring || !entry.reopenRecordId || !entry.browser
                        || entry.browser.navigationGeneration !== generation || entry.browser.loading) return
                    var recordId = entry.reopenRecordId
                    deck.sessions.updateBrowser(entry.sessionId, String(entry.browser.url), entry.browser.title)
                    if (deck.sessions.commitBrowserReopen(recordId, entry.sessionId)) entry.reopenRecordId = ""
                    else entry.rollbackReopen(recordId)
                }
                function onDocumentLoadUnsuccessful(generation, location) {
                    if (entry.reopenRecordId && !entry.retiring)
                        entry.deferReopenFailure(generation, location)
                }
                function onRenderProcessTerminated(status, code) {
                    entry.mainLoadSucceeded = false
                    if (status === WebEngineView.NormalTerminationStatus || entry.retiring) return
                    if (entry.reopenRecordId) entry.rollbackReopen(entry.reopenRecordId)
                }
                function onTabRequested(request) { deck.openWindow(entry.sessionId, request) }
                function onGeometryChangeRequested(geometry) {
                    if (entry.hostWindow) entry.hostWindow.applyGeometry(geometry)
                }
                function onMessage(text) { deck.message(entry.sessionId, text) }
                function onTitleChanged() {
                    if (entry.browser && entry.browser.title.length) entry.observeAdoption()
                    if (entry.browser) deck.sessions.updateBrowser(entry.sessionId,
                        String(entry.browser.url), entry.browser.title)
                }
                function onUrlChanged() {
                    if (entry.browser) deck.sessions.updateBrowser(entry.sessionId,
                        String(entry.browser.url), entry.browser.title)
                }
                function onCaptureProtectionChanged(blocked, visitToken) {
                    deck.accountStore.setMediaCaptureBlocked(blocked, visitToken)
                }
                function onPlaybackObserved(sample) {
                    deck.accountStore.observeSession(entry.sessionId, entry.token, sample)
                }
                function onResumeUnavailable() {
                    deck.message(entry.sessionId,
                        "The saved page is open. Use the provider's controls to restore your position.")
                }
            }
        }
    }
}

