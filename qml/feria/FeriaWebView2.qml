import QtQuick
import Colosseum.FeriaHost 1.0
import "FeriaPlayback.js" as Playback

FeriaHostItem {
    id: browser
    objectName: "feriaWebView2"
    required property url sourceUrl
    required property string profilePath
    signal playbackObserved(var observation)
    property real resumePosition: 0
    property bool observationEnabled: true
    property int navigationGeneration: 0
    function samplePlayback() { executeScript("feria-account-sample-" + navigationGeneration, Playback.sample) }
    Timer {
        interval: 2000; repeat: true; running: browser.ready && browser.observationEnabled
        onTriggered: browser.samplePlayback()
    }
    onScriptResult: function(label, json) {
        if (label === "feria-account-sample-" + navigationGeneration) {
            try {
                var sample = JSON.parse(json)
                if (!sample || !browser.observationEnabled) return
                if (resumePosition > 0 && sample.duration > 0 && !sample.ad && Playback.sameDestination(sample.href, sourceUrl)) {
                    executeScript("feria-account-resume-" + navigationGeneration, Playback.seek(resumePosition))
                } else playbackObserved(sample)
            } catch (e) {}
        } else if (label === "feria-account-resume-" + navigationGeneration && json === "true") resumePosition = 0
    }
    signal started(string location)
    signal completed(string location, bool success)
    signal failed(string reason)
    signal exitRequested()
    url: sourceUrl
    userDataFolder: profilePath + "/webview2"
    onNavigationStarted: function(location) { ++navigationGeneration; started(location) }
    onDocumentReady: function(location) { completed(location, true) }
    onNavigationCompleted: function(location, success) { completed(location, success) }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
