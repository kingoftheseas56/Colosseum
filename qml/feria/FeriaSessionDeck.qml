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
    property var restoreSource: []
    readonly property int restorableTabCount: restoreSource.length
    property string snapshotRoot: ""
    property int presentationRevision: 0
    property int hostSerial: 0
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
    readonly property var downloads: sharedProfile ? regularDownloads : null
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
            return descriptor
        })
    }
    function persistTabs() {
        if (ready && !quiescing) accountStore.saveBrowserTabs(tabDescriptors())
    }
    // Keep the previous session available while the current session keeps saving.
    // This projection is replaced on profile switch and consumed by restore/dismiss.
    function captureRestoreSource() {
        restoreSource = accountStore.browserTabs.map(function(descriptor) {
            return Object.assign({}, descriptor)
        })
    }
    function dismissRestoreTabs() { restoreSource = [] }
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
    function back(id) { var nav = navigationForSession(id); return nav ? nav.back() : false }
    function forward(id) { var nav = navigationForSession(id); return nav ? nav.forward() : false }
    function reload(id) { var nav = navigationForSession(id); return nav ? nav.reload() : false }
    function stop(id) { var nav = navigationForSession(id); return nav ? nav.stop() : false }
    function historyOffset(id, offset, revision) {
        var nav = navigationForSession(id)
        return nav ? nav.goToOffset(offset, revision) : false
    }
    function navigateInput(id, text, searchEngine) {
        var url = String(policy.omniboxUrl(text, searchEngine || "duckduckgo"))
        return url.length > 0 && navigateSession(id, url, 0, null, null)
    }
    function newSpace(sourceId, privateBrowsing, activate) {
        var source = entries[sourceId]
        if (!source || source.retiring || !ready) return ""
        var descriptor = Object.assign({}, source.descriptor)
        var provider = ProviderData.P[descriptor.providerId]
        // Explicit fixture/integration startUrl is validated, never a page URL.
        var start = String(descriptor.startUrl || (provider ? "https://" + provider.d : ""))
        var normalizedStart = String(policy.navigationUrl(start))
        if (!start || !normalizedStart.length) return ""
        descriptor.url = normalizedStart
        descriptor.title = descriptor.providerName || descriptor.providerId
        descriptor.privateBrowsing = Boolean(privateBrowsing)
        delete descriptor.position; delete descriptor.locator; delete descriptor.titleId
        return sessions.openBrowser(descriptor, true, false, activate !== false)
    }
    // Compatibility facade; new presentation supplies an explicit source ID.
    function newTab(privateBrowsing, url, activate) {
        if (!activeEntry) return ""
        if (!url) return newSpace(activeEntry.sessionId, privateBrowsing, activate)
        if (!ready || !policy.allowsNavigation(url)) return ""
        var descriptor = Object.assign({}, activeEntry.descriptor)
        descriptor.url = url
        descriptor.privateBrowsing = Boolean(privateBrowsing)
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
            focused: entry.browser && entry.browser.activeFocus}
    }
    function disposeHost(entry) {
        var host = entry.hostWindow, full = entry.fullSizeHost
        entry.hostWindow = null; entry.fullSizeHost = null
        if (host) { host.retiring = true; host.hide(); host.destroy() }
        if (full) {
            var surface = full.parent
            full.destroy()
            if (surface) surface.visible = false
        }
    }
    function moveToDesktop(id, geometry, dialogWindow) {
        var entry = entries[id]
        if (!entry || !entry.browser || entry.retiring || entry.closePending) return false
        if (entry.hostWindow) { entry.hostWindow.show(); entry.hostWindow.raise(); entry.hostWindow.requestActivate(); return true }
        if (entry.fullSizeHost && !restoreSession(id)) return false
        var host = desktopHostFactory.createObject(deck, {deck: deck, sessionId: id, dialogWindow: !!dialogWindow})
        if (!host) return false
        var rect = geometry || Qt.rect(100, 100, 900, 700)
        if (!host.applyGeometry(rect)) { host.destroy(); return false }
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "desktop:" + (++hostSerial))
        if (!identity.spaceId) { host.destroy(); return false }
        savePlacement(entry)
        entry.hostWindow = host
        entry.parent = host.viewport
        entry.anchors.fill = host.viewport
        host.show()
        // Frames become measurable after native exposure; apply the same clamp
        // again with actual screen/frame data, without changing page ownership.
        Qt.callLater(function() { if (!host.retiring) host.applyGeometry(rect) })
        host.requestActivate()
        entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function maximizeSession(id, surface) {
        var entry = entries[id]
        if (!entry || !entry.browser || entry.retiring || entry.closePending || entry.hostWindow
            || !surface || surface.Window.window !== deck.Window.window) return false
        if (entry.fullSizeHost) return true
        var host = fullSizeHostFactory.createObject(deck, {parent: surface, deck: deck, sessionId: id})
        if (!host) return false
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "app-fullsize:" + (++hostSerial))
        if (!identity.spaceId) { host.destroy(); return false }
        savePlacement(entry)
        entry.fullSizeHost = host
        surface.visible = true
        entry.parent = host.viewport
        entry.anchors.fill = host.viewport
        entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function returnToEmbedded(id) {
        var entry = entries[id]
        if (!entry || entry.retiring || entry.closePending || !entry.savedPlacement) return false
        var identity = sessions.setBrowserHost(sessions.browserIdentity(id), "embedded")
        if (!identity.spaceId) return false
        var saved = entry.savedPlacement
        entry.anchors.fill = null
        entry.parent = saved.parent
        entry.x = saved.x; entry.y = saved.y
        entry.width = saved.width; entry.height = saved.height
        entry.savedPlacement = null
        disposeHost(entry)
        if (saved.focused && entry.browser) entry.browser.forceActiveFocus()
        ++presentationRevision
        return true
    }
    function restoreSession(id) {
        var entry = entries[id]
        return entry && entry.fullSizeHost ? returnToEmbedded(id) : false
    }
    Component { id: desktopHostFactory; FeriaDesktopHost {} }
    Component { id: fullSizeHostFactory; FeriaFullSizeHost {} }
    function openWindow(sourceId, request) {
        var source = entries[sourceId]
        if (!source || source.retiring || !source.browser || !ready || !request.userInitiated) return
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
        delete descriptor.position; delete descriptor.locator; delete descriptor.titleId
        var foreground = request.destination !== WebEngineNewWindowRequest.InNewBackgroundTab
        var id = sessions.openBrowser(descriptor, true, false, foreground)
        var entry = entries[id]
        // Adopt the original request synchronously: preserving opener/form/SSO
        // state requires openIn(), rather than reopening just its URL.
        if (ready && policy.storageRoot === originRoot && entries[sourceId] === source
            && entry && entry.browser && entry.browser.profile === originProfile) {
            // Allocate the disposable native host before accepting the original
            // request; failed presentation rolls back the canonical placeholder.
            if (nativeDestination && !moveToDesktop(id, request.requestedGeometry,
                    request.destination === WebEngineNewWindowRequest.InNewDialog)) {
                sessions.close(id)
                message(sourceId, "Could not create the requested window")
                return
            }
            // Track the actual creation-view dependency, not QObject ownership.
            // New scoped close routes block its retirement until children die.
            // No hidden opener shell/lease workaround is enabled here.
            entry.creationEntry = source
            ++source.liveAdoptedChildren
            request.openIn(entry.browser)
            entry.browser.adoptingWindowRequest = false
        }
        else { if (id) sessions.close(id); message(sourceId, "Could not open the new tab") }
    }
    function restoreTabs() {
        if (!ready) return
        var saved = restoreSource.slice()
        var pending = []
        var existing = sessions.browsers().slice()
        var used = ({})
        var last = ""
        restoredProfile = true
        for (var descriptor of saved) {
            var match = existing.find(function(record) {
                return !used[record.id] && !record.target.privateBrowsing
                    && record.target.providerId === descriptor.providerId && record.target.url === descriptor.url
            })
            last = match ? match.id : sessions.openBrowser(descriptor, true, false, false)
            if (last) used[last] = true
            else pending.push(descriptor)
        }
        restoreSource = pending
        if (last) sessions.switchTo(last)
    }
    function reopenClosedSpace(provider) {
        if (!ready || !provider) return ""
        var record = sessions.prepareBrowserReopen(provider)
        if (!record.recordId || !record.descriptor || !policy.allowsNavigation(record.descriptor.url)) return ""
        var id = sessions.openBrowser(record.descriptor, true, false, true)
        var entry = entries[id]
        if (!entry || !entry.browser) {
            if (id) sessions.close(id)
            message("", "Could not create the reopened space. Its closed record is still available.")
            return ""
        }
        entry.reopenRecordId = record.recordId
        // Loader success is not page success. Completion commits only the exact
        // record; a failed load retires this placeholder without consuming it.
        return id
    }
    function restoreClosedTab() {
        return activeEntry ? reopenClosedSpace(activeEntry.descriptor.providerId) : ""
    }
    function considerRestore() {
        if (!ready || restoredProfile) return
        restoredProfile = true
        if (accountStore.browserPreferences.restoreTabs && sessions.browsers().length === 0) restoreTabs()
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
    function selectActive() {
        var next = entries[sessions.activeId] || null
        if (activeEntry && activeEntry !== next && activeEntry.fullSizeHost && !activeEntry.closePending)
            restoreSession(activeEntry.sessionId)
        activeEntry = next
        if (activeEntry && activeEntry.hostWindow) {
            activeEntry.hostWindow.show()
            activeEntry.hostWindow.raise()
            activeEntry.hostWindow.requestActivate()
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
        entry.pendingNavigation = null
        if (entry.closePending) entry.closeController.invalidate(false)
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
        function onActiveChangedProp() { deck.selectActive() }
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
            property var visitContext: descriptor.visitContext || ({ title: descriptor.title || "", cover: "" })
            property string token: ""
            property bool retiring: false
            property bool immersive: fullSizeHost !== null
            property var hostWindow: null
            property var fullSizeHost: null
            property var savedPlacement: null
            property var creationEntry: null
            property int liveAdoptedChildren: 0
            property string reopenRecordId: ""
            readonly property alias closeController: closeController
            readonly property alias closeConsent: closeController.consent
            readonly property alias closePending: closeController.pending
            FeriaCloseController {
                id: closeController
                browser: entry.browser; sessions: deck.sessions; sessionId: entry.sessionId
                retirementAllowed: entry.liveAdoptedChildren === 0
                onBlocked: (reason) => deck.message(entry.sessionId, reason)
                onChanged: ++deck.presentationRevision
                onFailureChanged: if (failure.length) deck.message(entry.sessionId, failure)
            }
            property string errorText: ""
            property var pendingNavigation: null
            readonly property var browser: browserLoader.item
            anchors.fill: parent
            visible: !retiring && (hostWindow ? hostWindow.visible
                : deck.displayed && deck.sessions.activeId === sessionId && !deck.suppressed)
            function retire() {
                if (retiring) return
                retiring = true
                closeController.invalidate(false)
                // Detach from a disposable host before destroying it. QObject
                // ownership remains on the deck throughout this visual move.
                parent = deck
                deck.disposeHost(entry)
                if (browser) browser.prepareForClose()
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
                consent: entry.hostWindow ? entry.closeConsent : deck.embeddedConsent
            }
            Loader {
                id: browserLoader
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.top: consentDock.bottom
                active: entry.token.length > 0 && (entry.descriptor.privateBrowsing
                    ? deck.privateProfile !== null && (!deck.privateProfileRetiring || browserLoader.item !== null)
                    : deck.sharedProfile !== null)
                sourceComponent: FeriaQtWebEngine {
                    sourceUrl: entry.initialWindowRequest ? "" : entry.descriptor.url
                    adoptingWindowRequest: entry.initialWindowRequest
                    profilePath: deck.policy.storageRoot
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
                }
                onStatusChanged: if (status === Loader.Error) entry.errorText = "The browser could not start."
                onLoaded: {
                    Qt.callLater(function() {
                        if (deck.entries[entry.sessionId] !== entry) return
                        var pending = entry.pendingNavigation
                        if (pending) deck.navigateSession(entry.sessionId, pending.url, pending.position, pending.locator, pending.context)
                    })
                }
            }
            Connections {
                target: entry.browser
                function onSafeToDestroy() {
                    if (entry.retiring) Qt.callLater(function() { entry.destroy() })
                }
                function onStarted(location) {
                    entry.errorText = ""
                    if (!entry.descriptor.privateBrowsing) deck.policy.rememberOrigin(entry.descriptor.providerId, location)
                    deck.sessions.updateBrowser(entry.sessionId, location, "")
                }
                function onCompleted(location, success) {
                    if (entry.retiring) return
                    if (entry.reopenRecordId) {
                        var recordId = entry.reopenRecordId
                        entry.reopenRecordId = ""
                        if (!success || !deck.sessions.commitBrowserReopen(recordId, entry.sessionId)) {
                            deck.sessions.close(entry.sessionId)
                            deck.message(entry.sessionId, "The space could not reopen. Its closed record is still available.")
                            return
                        }
                    }
                    entry.errorText = success ? "" : "Could not open the page. Try Reload."
                    deck.sessions.updateBrowser(entry.sessionId, location,
                                                entry.browser ? entry.browser.title : "")
                    if (success) deck.accountStore.recordBrowserVisit(entry.sessionId, entry.token,
                        location, entry.browser ? entry.browser.title : "")
                }
                function onTabRequested(request) { deck.openWindow(entry.sessionId, request) }
                function onGeometryChangeRequested(geometry) {
                    if (entry.hostWindow) entry.hostWindow.applyGeometry(geometry)
                }
                function onMessage(text) { deck.message(entry.sessionId, text) }
                function onTitleChanged() {
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

