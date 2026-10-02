import QtQuick
import QtQuick.Window
import "../../qml/feria"
Window {
    visible: true; width: 800; height: 600
    FeriaSessionCleaner {
        profilePath: smokeProfileRoot
        engines: [smokeEngine]
        domains: ["127.0.0.1"]
        origins: [smokeUrls[0]]
        onFinished: function(success) {
            browserReporter.report("local-signout", success, "Selected origin cleanup completed")
            browserReporter.finish()
        }
    }
}
