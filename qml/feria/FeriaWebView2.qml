import QtQuick
import Colosseum.FeriaHost 1.0

FeriaHostItem {
    id: browser
    objectName: "feriaWebView2"
    required property url sourceUrl
    required property string profilePath
    signal started(string location)
    signal completed(string location, bool success)
    signal failed(string reason)
    signal exitRequested()
    url: sourceUrl
    userDataFolder: profilePath + "/webview2"
    onNavigationStarted: function(location) { started(location) }
    onDocumentReady: function(location) { completed(location, true) }
    onNavigationCompleted: function(location, success) { completed(location, success) }
    onInitializationFailed: function(reason) { failed(reason) }
    onReturnedToQml: exitRequested()
}
