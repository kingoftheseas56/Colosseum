import QtQuick
import "FeriaPlayback.js" as Playback
import "FeriaResume.js" as Resume
import "FeriaConnectors.js" as Connectors
import "FeriaYouTubeMusic.js" as YouTubeMusic

// Shared production gate, independent of browser transport. Wrappers never emit
// media outside this object. Reading restoration remains owned by each wrapper.
QtObject {
    id: gate
    objectName: "feriaMediaSession"
    property var clock: function() { return Date.now() }
    property string providerId: ""
    property string sourceUrl: ""
    property string mode: "watch"
    property real initialPosition: 0
    property var state: Resume.idle(0, "")
    property int epoch: 0
    property int serial: 0
    property string pendingLabel: ""
    property var pendingRequests: ({})
    property int appliedSerial: 0
    property bool initialized: false
    property bool sawNavigation: false
    property string connectorSource: Connectors.script(YouTubeMusic.source)
    property string lastConnectorError: ""
    signal connectorFailed(string reason)
    signal scriptRequested(string label, string script)
    signal observation(var sample)
    signal protectionChanged(bool blocked)
    signal unavailable()
    signal restored()
    signal seekRequested(real position)
    property bool nativePlayback: false

    onProviderIdChanged: if (initialized) {
        ++epoch; discardRequests()
        state = Resume.idle(epoch, Playback.mediaKey(providerId, sourceUrl))
        protectionChanged(false)
        invalidatePageRequest()
    }
    onSourceUrlChanged: if (initialized) {
        state = Resume.invalidate(state, ++epoch); discardRequests()
        protectionChanged(state.blocked)
        invalidatePageRequest()
    }
    Component.onCompleted: { initialized = true; requestResume(initialPosition, sourceUrl) }
    function requestResume(position, href) {
        ++epoch; discardRequests()
        state = Resume.begin(epoch, Playback.mediaKey(providerId, href), position, clock())
        protectionChanged(state.blocked)
        invalidatePageRequest()
    }
    function cancelResume(freshStart) {
        state = Resume.cancel(state, state.token, freshStart === true)
        epoch = state.token; discardRequests()
        protectionChanged(state.blocked)
        invalidatePageRequest()
    }
    function navigationChanged() {
        ++epoch; discardRequests()
        if (sawNavigation) state = Resume.invalidate(state, epoch)
        else { sawNavigation = true; state.token = epoch }
        protectionChanged(state.blocked)
        invalidatePageRequest()
    }
    function discardRequests() {
        pendingLabel = ""
        pendingRequests = ({})
        appliedSerial = serial
    }
    function invalidatePageRequest() {
        scriptRequested("feria-invalidate-" + epoch, Playback.invalidateMediaRequest(epoch))
    }
    function poll() {
        var now = clock()
        var labels = Object.keys(pendingRequests)
        for (var label of labels) {
            if (pendingRequests[label].expiresAt <= now) delete pendingRequests[label]
        }
        labels = Object.keys(pendingRequests).sort(function(a, b) {
            return pendingRequests[a].serial - pendingRequests[b].serial
        })
        while (labels.length >= 8) delete pendingRequests[labels.shift()]
        pendingLabel = "feria-media-" + epoch + "-" + (++serial)
        pendingRequests[pendingLabel] = {epoch:epoch, serial:serial, expiresAt:now + 10000}
        scriptRequested(pendingLabel, Playback.mediaObservation(mode, providerId, connectorSource, epoch))
        return pendingLabel
    }
    function acceptResult(label, frame) {
        var ticket = pendingRequests[label]
        if (!ticket || ticket.epoch !== epoch || ticket.serial <= appliedSerial) return
        delete pendingRequests[label]
        if (ticket.expiresAt <= clock()) return
        // A newer applied hold/error supersedes older observations too. Merely
        // issuing the next probe must not starve a healthy in-flight result.
        appliedSerial = ticket.serial
        for (var older of Object.keys(pendingRequests)) {
            if (pendingRequests[older].serial <= appliedSerial) delete pendingRequests[older]
        }
        if (label === pendingLabel) pendingLabel = ""
        if (!frame) frame = {outcome:"error",reason:"missing-script-result"}
        if (frame.outcome !== "observation" || !frame.sample) {
            protectionChanged(true)
            if (frame && frame.outcome === "error" && lastConnectorError !== String(frame.reason)) {
                lastConnectorError = String(frame.reason); connectorFailed(lastConnectorError)
            }
            return
        }
        lastConnectorError = ""
        var sample = frame.sample
        if (sample.kind === "book") { observation(sample); return }
        var decision = Resume.tick(state, sample, Playback.mediaKey(providerId, sample.href), clock())
        state = JSON.parse(JSON.stringify(state))
        protectionChanged(state.blocked)
        if (decision.unavailable) unavailable()
        if (decision.seek !== null) {
            if (nativePlayback) seekRequested(decision.seek)
            else scriptRequested("feria-seek-" + epoch + "-" + serial,
                    Playback.seekMedia(decision.seek, sample, mode, providerId, connectorSource, epoch))
        }
        if (decision.allow) {
            sample.captureState = "observation"
            sample.connectorOutcome = "observation"
            observation(sample)
            if (state.phase === "settled") restored()
        }
    }
    function acceptNativeObservation(sample) {
        if (!nativePlayback) return
        var label = "feria-native-" + epoch + "-" + (++serial)
        pendingRequests[label] = {epoch:epoch, serial:serial, expiresAt:clock()+10000}
        acceptResult(label, {outcome:"observation",sample:sample})
    }
    property Timer deadlineTimer: Timer {
        interval: 500; repeat: true; running: gate.state.blocked
        onTriggered: {
            var decision = Resume.tick(gate.state, null, "", gate.clock())
            gate.state = JSON.parse(JSON.stringify(gate.state))
            if (decision.unavailable) gate.unavailable()
        }
    }
}
