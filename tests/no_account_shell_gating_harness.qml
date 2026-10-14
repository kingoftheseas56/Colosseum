// RED/GREEN contract harness for the 1.1.9 no-account edition's QML gates.
// Loads the REAL TopBar.qml and REAL AccountOnboardingHost.qml via
// Qt.createComponent and drives them with fake controllers exposing (or
// omitting) accountFeaturesAvailable — the property AccountController
// publishes and every account entry surface gates on. No network, no live
// app state; mirrors manga_series_catalogue_harness's house convention
// (sentinel + Qt.exit).
import QtQuick

Item {
    id: harness
    width: 1440
    height: 900
    visible: true

    component StubNote: QtObject {
        // Fakes are built with Qt.createQmlObject in makeTopBarWith/makeHostWith
        // so the "property absent" legacy case is a real absent property.
        property int unused: 0
    }

    function ck(condition, label) {
        if (!condition)
            throw new Error(label)
    }

    function runChecks() {
        try {
            // ── Case 1: edition build — TopBar account entry hidden ──
            var barEdition = makeTopBarWith({ mode: "localOnly", accountFeaturesAvailable: false })
            ck(barEdition.accountEntryAvailable === false,
               "case1: TopBar accountEntryAvailable must be false when the edition gate is off")
            ck(findAccountButton(barEdition) !== null
               && findAccountButton(barEdition).visible === false,
               "case1: the topbar account button must be hidden in the no-account edition")

            // ── Case 2: legacy stub controller — button stays visible ──
            var barStub = makeTopBarWith({ mode: "localOnly" })
            ck(barStub.accountEntryAvailable === true,
               "case2: a stub controller without the property keeps the account button")
            ck(findAccountButton(barStub).visible === true,
               "case2: stub controller must not hide the account button")

            // ── Case 3: edition build — onboarding cover never shows ──
            var hostEdition = makeHostWith({ mode: "signedOut", accountFeaturesAvailable: false })
            ck(hostEdition.accountFlowVisible === false,
               "case3: the onboarding cover must stay hidden in the no-account edition")
            ck(hostEdition.visible === false,
               "case3: host visible must follow accountFlowVisible")

            // ── Case 4: stub controller — signed-out still shows the cover ──
            var hostStub = makeHostWith({ mode: "signedOut" })
            ck(hostStub.accountFlowVisible === true,
               "case4: a stub controller keeps the legacy signed-out cover behavior")

            console.log("NO_ACCOUNT_SHELL_GATING_OK")
            Qt.exit(0)
        } catch (e) {
            console.log("NO_ACCOUNT_SHELL_GATING_FAIL: " + e.message)
            Qt.exit(1)
        }
    }

    property var _pendingTopBarArgs: null
    function makeTopBarWith(args) {
        var comp = Qt.createComponent("../qml/TopBar.qml")
        if (comp.status !== Component.Ready)
            throw new Error("TopBar component: " + comp.errorString())
        var fake
        if (args.accountFeaturesAvailable === undefined) {
            fake = Qt.createQmlObject(
                "import QtQuick; QtObject { property string mode: " + JSON.stringify(args.mode) +
                "; property bool signOutSyncWarningPending: false; property string username: \"\" }",
                harness)
        } else {
            fake = Qt.createQmlObject(
                "import QtQuick; QtObject { property string mode: " + JSON.stringify(args.mode) +
                "; property bool signOutSyncWarningPending: false; property string username: \"\"" +
                "; property bool accountFeaturesAvailable: " + args.accountFeaturesAvailable + " }",
                harness)
        }
        var created = comp.createObject(harness, { accountController: fake, backdrop: null })
        if (!created)
            throw new Error("TopBar createObject failed")
        return created
    }

    function makeHostWith(args) {
        var comp = Qt.createComponent("../qml/account/AccountOnboardingHost.qml")
        if (comp.status !== Component.Ready)
            throw new Error("AccountOnboardingHost component: " + comp.errorString())
        var fake
        if (args.accountFeaturesAvailable === undefined) {
            fake = Qt.createQmlObject(
                "import QtQuick; QtObject { property string mode: " + JSON.stringify(args.mode) +
                "; property bool signOutSyncWarningPending: false }",
                harness)
        } else {
            fake = Qt.createQmlObject(
                "import QtQuick; QtObject { property string mode: " + JSON.stringify(args.mode) +
                "; property bool signOutSyncWarningPending: false" +
                "; property bool accountFeaturesAvailable: " + args.accountFeaturesAvailable + " }",
                harness)
        }
        var recovery = Qt.createQmlObject(
            "import QtQuick; QtObject { property bool active: false }",
            harness)
        var created = comp.createObject(harness, { controller: fake, recoveryPresenter: recovery })
        if (!created)
            throw new Error("AccountOnboardingHost createObject failed")
        return created
    }

    function findAccountButton(root) {
        var stack = [root]
        while (stack.length) {
            var item = stack.pop()
            if (item.objectName === "colosseumTopbarAccountButton")
                return item
            var children = item.children
            for (var i = 0; i < children.length; i++)
                stack.push(children[i])
        }
        return null
    }

    Timer { interval: 100; running: true; repeat: false; onTriggered: harness.runChecks() }
    Timer { interval: 8000; running: true
        onTriggered: { console.log("NO_ACCOUNT_SHELL_GATING_FAIL timeout"); Qt.exit(1) } }
}
