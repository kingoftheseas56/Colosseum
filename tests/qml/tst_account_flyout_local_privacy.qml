import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/account" as Account

TestCase {
    name: "AccountFlyoutLocalPrivacy"
    when: windowShown
    visible: true
    width: 800; height: 600
    QtObject {
        id: mockController
        property string mode: "localOnly"
        property string username: "Synthetic"
        property bool syncRetryAvailable: false
        property int pendingOutboxCount: 0
        property string syncState: "inactive"
    }
    QtObject {
        id: accountCenter
        property string opened: ""
        function open(section, invoker) { opened = section }
    }
    Account.AccountFlyout {
        id: flyout
        controller: mockController
        accountSettings: accountCenter
        stremioAvailable: true
        nuvioAvailable: true
    }
    SignalSpy { id: theatreSpy; target: flyout; signalName: "theatreSyncRequested" }

    function init() {
        accountCenter.opened = ""
        mockController.mode = "localOnly"
        theatreSpy.clear()
        flyout.open()
        wait(20)
    }
    function cleanup() { flyout.close(); wait(0) }
    function test_localPrivacyKeyboardRoute() {
        const privacy = findChild(flyout, "accountFlyoutNav_privacy")
        verify(privacy && privacy.visible, "Local-only users need a visible privacy entry")
        verify(!findChild(flyout, "accountFlyoutNav_devices"))
        verify(!findChild(flyout, "accountFlyoutNav_profile"))
        const signIn = findChild(flyout, "accountFlyoutSessionAction")
        compare(signIn.text, "Sign in to Colosseum")
        tryCompare(signIn, "activeFocus", true)
        keyClick(Qt.Key_Down)
        tryCompare(findChild(flyout, "accountFlyoutNav_theatre"), "activeFocus", true)
        keyClick(Qt.Key_Down)
        tryCompare(privacy, "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(accountCenter.opened, "privacy")
        compare(flyout.visible, false)
    }
    function test_theatreSyncHasItsOwnRoute() {
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        compare(theatreSpy.count, 1)
        compare(accountCenter.opened, "")
        compare(mockController.mode, "localOnly")
        compare(flyout.visible, false)
    }

    function test_accountModesUnchanged_data() { return [{tag:"signedIn", mode:"signedIn"}, {tag:"offline", mode:"offline"}] }
    function test_accountModesUnchanged(data) {
        mockController.mode = data.mode; flyout.close(); flyout.open(); wait(20)
        const settings = findChild(flyout, "accountFlyoutNav_profile")
        compare(settings.text, "Account settings")
        verify(findChild(flyout, "accountFlyoutNav_devices").visible)
        verify(findChild(flyout, "accountFlyoutNav_privacy").visible)
        tryCompare(settings, "activeFocus", true)
    }
}
