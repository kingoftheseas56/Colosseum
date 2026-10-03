import QtQuick
import QtQuick.Window
import "../../qml/feria"
Window {
    visible: true; width: 800; height: 600
    // Account cleanup is created after the application window is already shown.
    Timer { interval: 500; running: true; onTriggered: cleaner.active = true }
    Loader {
        id: cleaner
        active: false
        sourceComponent: FeriaSessionCleaner {
            profilePath: smokeProfileRoot
            engines: smokeEngine === "both" ? ["webview2", "qtwebengine"] : [smokeEngine]
            domains: ["127.0.0.1"]
            origins: smokeUrls
            onFinished: function(success) {
                browserReporter.report("local-signout", success, "Selected origin cleanup completed")
                browserReporter.finish()
            }
        }
    }
}
