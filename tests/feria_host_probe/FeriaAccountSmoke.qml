import QtQuick
import QtQuick.Window
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property bool finished: false
    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    function finish(success, detail) {
        if (finished) return
        finished = true
        deadline.stop()
        feria.back()
        browserReporter.report(smokeUrls[0], success, detail)
        Qt.callLater(function() { browserReporter.finish() })
    }
    FeriaWorld { id: feria; anchors.fill: parent; lifecycleActive: true }
    Component.onCompleted: {
        FeriaAccount.clearHistory()
        var host = find(feria, "feriaBrowserHost")
        if (!host) { finish(false, "Browser host missing"); return }
        host.enginePreferences = {youtube:smokeEngine}
        feria.openHost("youtube", "", "resume", smokeUrls[0], 10)
        Qt.callLater(function() { host.engineOverride = smokeEngine })
        deadline.start()
    }
    Connections {
        target: FeriaAccount
        function onChanged() {
            if (finished || FeriaAccount.sessions.length === 0) return
            var row = FeriaAccount.sessions[0]
            if (row.position < 10 || row.mins <= 0) return
            Qt.callLater(function() {
                feria.back()
                var ok = feria.continueItems.length === 1 && feria.allSessions().length === 1
                    && feria.titleObj(row.id).t === row.title
                feria.openAccount()
                feria.accountTab = "stats"
                ok = ok && feria.totalMins(feria.allSessions()) > 0
                    && find(feria, "feriaBrowserHost").engine === smokeEngine
                finish(ok, "Production browser → account store → Continue, history and stats")
            })
        }
    }
    Timer { id: deadline; interval: 35000; onTriggered: window.finish(false, "No persisted playback reached Feria") }
}
