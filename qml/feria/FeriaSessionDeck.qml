import QtQuick
import QtWebEngine

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

    Component {
        id: profileFactory
        WebEngineProfile {
            required property string accountRoot
            storageName: "feria"
            offTheRecord: false
            persistentStoragePath: accountRoot + "/qtwebengine"
            cachePath: accountRoot + "/qtwebengine-cache"
            persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
        }
    }
    function selectActive() {
        activeEntry = entries[sessions.activeId] || null
    }
    function syncSessions() {
        if (syncing) { syncAgain = true; return }
        syncing = true
        var live = ({})
        var records = sessions.browsers()
        for (var record of records) live[record.id] = record
        for (var id of Object.keys(entries)) {
            if (live[id] && !quiescing) continue
            var previous = entries[id]
            delete entries[id]
            accountStore.endSessionVisit(id)
            previous.retire()
        }
        if (ready && records.length > 0 && !sharedProfile && policy.runtimeProfileCount === 0) {
            sharedProfile = profileFactory.createObject(deck, { accountRoot: policy.storageRoot })
            if (sharedProfile) policy.registerRuntimeProfile(sharedProfile)
        }
        if (ready && sharedProfile) {
            for (var current of records) {
                if (entries[current.id]) continue
                var entry = sessionFactory.createObject(deck, {
                    sessionId: current.id, descriptor: current.target
                })
                if (entry) entries[current.id] = entry
            }
        }
        selectActive()
        syncing = false
        if (syncAgain) { syncAgain = false; Qt.callLater(syncSessions) }
        if (quiescing) Qt.callLater(advanceQuiescence)
    }
    function startQuiescence(forCleanup) {
        quiescing = true
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
        if (context) entry.visitContext = context
        entry.token = accountStore.beginSessionVisit(id, {
            pk: entry.descriptor.providerId, resumePending: position > 0,
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
        function onChanged() { deck.syncSessions() }
        function onActiveChangedProp() { deck.selectActive() }
    }
    Connections {
        target: deck.policy
        function onStorageRootChanged() { deck.startQuiescence(false) }
        function onRuntimeViewCountChanged() { Qt.callLater(deck.advanceQuiescence) }
        function onRuntimeProfileCountChanged() { Qt.callLater(deck.advanceQuiescence) }
    }
    Component.onCompleted: syncSessions()
    Component.onDestruction: {
        for (var id of Object.keys(entries)) accountStore.endSessionVisit(id)
    }

    Component {
        id: sessionFactory
        Item {
            id: entry
            required property string sessionId
            required property var descriptor
            property var visitContext: descriptor.visitContext || ({ title: descriptor.title || "", cover: "" })
            property string token: ""
            property bool retiring: false
            property bool immersive: false
            property string errorText: ""
            property var pendingNavigation: null
            readonly property var browser: browserLoader.item
            anchors.fill: parent
            visible: !retiring && deck.displayed && deck.sessions.activeId === sessionId && !deck.suppressed
            function retire() {
                if (retiring) return
                retiring = true
                if (browser) browser.prepareForClose()
                else destroy()
            }
            Component.onCompleted: {
                token = deck.accountStore.beginSessionVisit(sessionId, {
                    pk: descriptor.providerId, resumePending: Number(descriptor.position || 0) > 0,
                    title: visitContext.title || "", cover: visitContext.cover || ""
                })
            }
            Loader {
                id: browserLoader
                anchors.fill: parent
                active: entry.token.length > 0 && deck.sharedProfile !== null
                sourceComponent: FeriaQtWebEngine {
                    sourceUrl: entry.descriptor.url
                    profilePath: deck.policy.storageRoot
                    sharedProfile: deck.sharedProfile
                    providerId: entry.descriptor.providerId
                    accountVisitToken: entry.token
                    observationMode: entry.descriptor.mode || "watch"
                    observationEnabled: deck.accountStore.recording
                    resumePosition: Number(entry.descriptor.position || 0)
                    resumeLocator: entry.descriptor.locator || null
                    suppressed: !entry.visible
                    // Keep hidden pages eligible for background audio. This slice
                    // deliberately adds no blanket freezing/discard policy.
                    lifecycleState: WebEngineView.LifecycleState.Active
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
                    deck.policy.rememberOrigin(entry.descriptor.providerId, location)
                    deck.sessions.updateBrowser(entry.sessionId, location, "")
                }
                function onCompleted(location, success) {
                    entry.errorText = success ? "" : "Could not open the page. Try Reload."
                    deck.sessions.updateBrowser(entry.sessionId, location,
                                                entry.browser ? entry.browser.title : "")
                }
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
