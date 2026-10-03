import QtQuick
import QtQuick.Window
import "../../qml/feria"

Window {
    id: window
    width: 1280; height: 900; visible: true
    property int phase: 0
    property int minimized: 0
    property int fullscreen: 0
    property int closed: 0
    property int returned: 0
    property bool finished: false
    FeriaWorld {
        id: feria
        anchors.fill: parent
        onMinimizeClicked: window.minimized++
        onFullscreenClicked: window.fullscreen++
        onPowerClicked: window.closed++
        onHomeRequested: window.returned++
    }
    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) {
            var found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    function activate(item) {
        if (!item) return false
        if (typeof item.activate === "function") return item.activate(Qt.OtherFocusReason)
        for (var i = 0; i < item.children.length; ++i)
            if (activate(item.children[i])) return true
        return false
    }
    function finish() {
        if (finished) return
        finished = true; step.stop()
        Qt.callLater(function() { browserReporter.finish() })
    }
    function check(ok, detail) {
        browserReporter.report("feria-chrome", ok, detail)
        if (!ok) finish()
        return ok
    }
    function checkControls(prefix) {
        var names = ["Minimize", "Fullscreen", "Close"]
        for (var i = 0; i < names.length; ++i) {
            var item = find(feria, prefix + names[i])
            if (!check(item && item.visible && item.width >= 44 && item.height >= 44,
                       feria.viewState + " exposes " + names[i] + " with a usable target")) return false
            if (!check(activate(item), "Window action accepts keyboard/accessibility activation")) return false
        }
        return check(minimized === fullscreen && fullscreen === closed && closed > 0,
                     "Window actions reach the corresponding Feria signals")
    }
    Timer {
        id: step
        interval: 400; repeat: true; running: true
        onTriggered: {
            if (window.phase === 0) {
                if (!window.checkControls("feria")) return
                if (!window.check(!window.find(feria, "feriaBack").visible,
                                  "Home does not display a redundant back action")) return
                window.activate(window.find(feria, "feriaReturnToColosseum"))
                if (!window.check(window.returned === 1, "Home action returns to Colosseum")) return
                feria.openTitle("stranger")
            } else if (window.phase === 1) {
                if (!window.checkControls("feria")) return
                var back = window.find(feria, "feriaBack")
                if (!window.check(back.visible && window.activate(back) && feria.viewState === "home",
                                  "Title has working canonical back navigation")) return
                feria.openSearch("")
            } else if (window.phase === 2) {
                if (!window.checkControls("feria")) return
                window.activate(window.find(feria, "feriaBack"))
                if (!window.check(feria.viewState === "home", "Search back returns home")) return
                feria.viewState = "apps"
            } else if (window.phase === 3) {
                if (!window.checkControls("feria")) return
                window.activate(window.find(feria, "feriaBack"))
                if (!window.check(feria.viewState === "home", "Apps back returns home")) return
                feria.openAccount()
            } else if (window.phase === 4) {
                if (!window.checkControls("feriaAccount")) return
                var tabs = window.find(feria, "feriaAccountTabs")
                var controls = window.find(feria, "feriaAccountMinimize").parent
                if (!window.check(Math.abs(tabs.x + tabs.width / 2 - feria.width / 2) < 1
                                  && tabs.x + tabs.width <= controls.x,
                                  "Account tabs stay centered without overlapping window controls")) return
                window.activate(window.find(feria, "account-back"))
                if (!window.check(feria.viewState === "home", "Account back returns home")) return
                feria.hostApp = "youtube"; feria.hostReturnState = "home"; feria.viewState = "host"
            } else {
                if (!window.checkControls("feriaHost")) return
                window.activate(window.find(feria, "feriaBrowserReturn"))
                window.check(feria.viewState === "home", "Provider app back returns to Feria")
                window.finish()
            }
            window.phase++
        }
    }
    Timer { interval: 15000; running: true; onTriggered: { window.check(false, "Chrome checks timed out"); window.finish() } }
}
