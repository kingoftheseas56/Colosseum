import QtQuick
import QtWebEngine
import "file:///opt/colosseum/qml" as Installed

Installed.Main {
    id: app
    property int proofStage: 0
    property int proofTicks: 0
    property bool profileRetained: false
    property string webResult: ""
    property bool webShown: false
    readonly property string proofPhase: Qt.application.arguments[2]
    readonly property string homeFrame: Qt.application.arguments[3]
    readonly property string webFrame: Qt.application.arguments[4]
    readonly property string proofUrl: Qt.application.arguments[5]

    function findSplash(item) {
        if (!item) return null
        if (item.objectName === "bootSplash") return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = app.findSplash(children[i])
            if (found) return found
        }
        return null
    }
    function fail(message) {
        console.error("RUNTIME_PROOF_ERROR=" + message)
        Qt.exit(20)
    }
    WebEngineView {
        id: browser
        anchors.fill: parent
        visible: app.webShown
        z: 100000
        onLoadingChanged: function(request) {
            if (app.proofStage < 3) return
            if (request.status === WebEngineView.LoadFailedStatus) {
                app.fail("WebEngine load failed: " + request.errorString)
            } else if (request.status === WebEngineView.LoadSucceededStatus) {
                browser.runJavaScript("document.getElementById('proof').textContent", function(result) {
                    if (result !== "Colosseum WebEngine qualification 130") {
                        app.fail("WebEngine DOM mismatch")
                        return
                    }
                    app.webResult = result
                    app.proofStage = 4
                })
            }
        }
    }
    Timer {
        interval: 200
        running: true
        repeat: true
        onTriggered: {
            if (++app.proofTicks > 550) {
                app.fail("runtime proof timeout")
                return
            }
            if (app.proofStage === 0) {
                const splash = app.findSplash(app.contentItem)
                if (!splash || splash.visible || AccountController.busy) return
                if (app.proofPhase === "first") {
                    console.info("RUNTIME_INITIAL_ACCOUNT=" + JSON.stringify({
                        mode: AccountController.mode, onboardingRequired: AccountController.onboardingRequired,
                        errorCategory: AccountController.errorCategory
                    }))
                    if (!AccountController.onboardingRequired) {
                        app.fail("fresh profile already completed onboarding")
                        return
                    }
                    // A minimal desktop can start in a secure-storage error
                    // state; choosing local-only is valid from that state too.
                    AccountController.continueWithoutAccount()
                } else {
                    app.profileRetained = !AccountController.onboardingRequired && AccountController.mode === "localOnly"
                    if (!app.profileRetained) {
                        app.fail("local-only profile was not restored")
                        return
                    }
                }
                app.proofStage = 1
            } else if (app.proofStage === 1) {
                if (AccountController.onboardingRequired || AccountController.mode !== "localOnly") return
                app.proofStage = 2
            } else if (app.proofStage === 2) {
                app.proofStage = -1
                if (!app.contentItem.grabToImage(function(result) {
                    if (!result.saveToFile(app.homeFrame)) { app.fail("Home screenshot failed"); return }
                    app.proofStage = 3
                    app.webShown = true
                    browser.url = app.proofUrl
                })) app.fail("Home grab request failed")
            } else if (app.proofStage === 4) {
                app.proofStage = -1
                if (!browser.grabToImage(function(result) {
                    if (!result.saveToFile(app.webFrame)) { app.fail("WebEngine screenshot failed"); return }
                    console.info("INSTALLED_RUNTIME_PROOF=" + JSON.stringify({
                        phase: app.proofPhase, webResult: app.webResult,
                        profileRetained: app.profileRetained,
                        mode: AccountController.mode,
                        onboardingRequired: AccountController.onboardingRequired
                    }))
                    app.proofStage = 5
                })) app.fail("WebEngine grab request failed")
            }
        }
    }
}
