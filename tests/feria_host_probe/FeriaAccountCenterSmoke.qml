import QtQuick
import QtQuick.Window
import "../../qml/feria" as Feria
import "../../qml/account" as Account

Window {
    id: window
    width: 1440; height: 900; visible: true
    property int step: 0
    property bool feriaVisible: true
    property string originalRegion: ""
    function check(ok, detail) { browserReporter.report("account-integration", ok, detail) }
    function capture(item, name, next) {
        stepsTimer.stop()
        item.grabToImage(function(result) {
            window.check(result.saveToFile(smokeProfileRoot + "/" + name + ".png"), "Capture " + name)
            if (next) next()
            stepsTimer.start()
        })
    }
    function openActivity(section) {
        center.close(); feria.accountTab = section; activity.active = true
        if (activity.item) activity.item.takeKeyboardFocus()
    }
    function closeActivity() { activity.active = false; feria.keyboardTarget.forceActiveFocus() }
    QtObject {
        id: nativeHistory
        property int clearCount: 0
        function clearAll() { clearCount++; return true }
    }
    Feria.FeriaWorld {
        id: feria
        anchors.fill: parent
        sharedAccount: true
        lifecycleActive: window.feriaVisible
        visible: window.feriaVisible
        browserSuppressed: center.visible || activity.active
        onAccountRequested: {
            window.closeActivity()
            center.open("feria", feria.keyboardTarget, "feria")
        }
    }
    Loader {
        id: activity
        anchors.fill: parent
        active: false
        sourceComponent: Feria.YourFeriaPage { controller: feria }
        onLoaded: {
            item.backRequested.connect(window.closeActivity)
            item.settingsRequested.connect(feria.openApps)
            item.takeKeyboardFocus()
        }
    }
    Account.AccountCenter {
        id: center
        feriaController: feria
        historyCoordinator: nativeHistory
        onFeriaActivityRequested: function(section) { window.openActivity(section) }
    }
    Timer {
        id: stepsTimer
        interval: 1000; running: true; repeat: true
        onTriggered: {
            try {
                if (window.step === 0) {
                    window.originalRegion = feria.region
                    FeriaAccount.clearHistory()
                    FeriaAccount.beginVisit({pk:"youtube"})
                    FeriaAccount.observe({href:"https://youtube.com/watch?v=account-smoke", title:"Account smoke video",
                        position:10, duration:600, paused:false, kind:"video"})
                } else if (window.step === 1) {
                    FeriaAccount.observe({href:"https://youtube.com/watch?v=account-smoke", title:"Account smoke video",
                        position:11, duration:600, paused:false, kind:"video"})
                    FeriaAccount.beginVisit({pk:"kindle"})
                    FeriaAccount.observe({href:"https://read.amazon.com/book-smoke", title:"Account smoke book",
                        kind:"book", paused:false, locator:{type:"scroll",fraction:0.4,selector:"#reader"}})
                    feria.accountTab = "stats"
                    feria.openAccount()
                } else if (window.step === 2) {
                    window.check(center.visible && center.feriaOnly && center.activeSection === "feria", "Feria avatar opens Feria-only settings even after Stats")
                    window.check(center.railSections.length === 1 && center.railSections[0].id === "feria", "Feria settings exclude global account sections")
                    window.check(feria.viewState === "home" && feria.sharedAccountView.servicesOnly, "Settings reuse service management without activity tabs")
                    window.check(feria.browserSuppressed, "Settings suppress the provider surface")
                    const hadNetflix = feria.activeApps.indexOf("netflix") >= 0
                    feria.toggleApp("netflix")
                    window.check((feria.activeApps.indexOf("netflix") >= 0) !== hadNetflix, "Service removal still works")
                    feria.toggleApp("netflix")
                    feria.setRegion("India")
                    window.capture(center, "feria-settings", function() {
                        center.close(); window.feriaVisible = false; center.open("feria", null)
                    })
                } else if (window.step === 3) {
                    window.check(!center.feriaOnly && center.railSections.some(function(row) { return row.id === "profile" }), "Other-world Account Centre retains normal sections")
                    window.check(feria.region === "India" && feria.sharedAccountView.servicesOnly, "Both entry points share Feria settings")
                    window.check(!center.railSections.some(function(row) { return row.id === "colosseum" }), "Activity is absent from Account Centre")
                    window.capture(center, "main-feria-settings", function() {
                        feria.setRegion(window.originalRegion); center.close(); window.feriaVisible = true
                        window.openActivity("history")
                    })
                } else if (window.step === 4) {
                    window.check(activity.item && activity.item.activityOnly && feria.sharedAccountView === activity.item, "Your Feria owns the existing activity content")
                    window.check(feria.viewState === "home" && !center.visible, "Activity preserves Feria home behind it")
                    activity.item.handleKey(Qt.Key_Down)
                    window.check(feria.accountHistoryFocusIndex >= 0, "Keyboard enters History")
                    feria.accountTab = "highlights"; activity.item.takeKeyboardFocus(); activity.item.handleKey(Qt.Key_Down)
                    window.check(feria.accountContentFocusActive, "Keyboard enters Highlights")
                    feria.accountTab = "stats"; activity.item.takeKeyboardFocus(); activity.item.handleKey(Qt.Key_Down)
                    window.check(activity.item.statsContentActive, "Keyboard enters Stats")
                    window.check(FeriaAccount.sessions.length === 2 && feria.totalMins(feria.allSessions()) > 0, "Your Feria sees recorded playback and reading activity")
                    window.capture(activity.item, "your-feria", function() { window.width = 1024 })
                } else if (window.step === 5) {
                    window.check(activity.item.width === 1024, "Your Feria adapts to a narrow window")
                    window.capture(activity.item, "your-feria-narrow", function() { activity.item.backRequested() })
                } else if (window.step === 6) {
                    window.check(!feria.sharedAccountActive && feria.sharedAccountView === null && feria.keyboardTarget.activeFocus, "Back releases activity ownership and restores Feria focus")
                    center.open("privacy", feria.keyboardTarget)
                    const count = FeriaAccount.sessions.length
                    center.privacyClearActivityHistoryRequested()
                    window.check(nativeHistory.clearCount === 1 && FeriaAccount.sessions.length === count, "Main activity clear leaves Feria untouched")
                    center.close(); feria.clearHistory(); feria.clearHistory()
                    window.check(FeriaAccount.sessions.length === 0 && FeriaAccount.continueItems.length === 2, "Feria clear retains playback and reading Continue")
                    window.check(nativeHistory.clearCount === 1, "Feria clear leaves native activity untouched")
                    window.openActivity("history")
                } else if (window.step === 7) {
                    window.check(feria.allSessions().length === 0 && feria.totalMins(feria.allSessions()) === 0, "Activity and Stats refresh after clear")
                    feria.openApps()
                } else if (window.step === 8) {
                    window.check(!activity.active && center.visible && center.feriaOnly && feria.accountTab === "apps", "Services action exits Your Feria and opens contextual settings")
                    center.close()
                } else if (window.step === 9) {
                    window.check(!feria.sharedAccountActive && feria.sharedAccountView === null, "Settings release their shared view on close")
                    stop(); browserReporter.finish()
                }
                window.step++
            } catch (error) { window.check(false, String(error)); stop(); browserReporter.finish() }
        }
    }
}
