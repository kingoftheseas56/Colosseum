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
    Account.AccountFlyout { id: flyout; controller: mockController }
    function textItem(item, value) {
        if (item.text !== undefined && item.text === value) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = textItem(children[i], value)
            if (found) return found
        }
        return null
    }
    function init() { accountCenter.opened = ""; mockController.mode = "localOnly"; flyout.open(); wait(20) }
    function cleanup() { flyout.close(); wait(0) }
    function test_localPrivacyKeyboardRoute() {
        var label = textItem(flyout, "Data & privacy")
        verify(label !== null, "Local-only users need a visible privacy entry")
        verify(label.visible)
        verify(!textItem(flyout, "Devices ›"))
        verify(!textItem(flyout, "Account ›"))
        var row = label.parent
        tryCompare(row, "activeFocus", true)
        keyClick(Qt.Key_Down)
        tryCompare(findChild(flyout, "accountFlyoutSessionAction"), "activeFocus", true)
        keyClick(Qt.Key_Up)
        tryCompare(row, "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(accountCenter.opened, "privacy")
        compare(flyout.visible, false)
    }
    function test_accountModesUnchanged_data() { return [{tag:"signedIn", mode:"signedIn"}, {tag:"offline", mode:"offline"}] }
    function test_accountModesUnchanged(data) {
        mockController.mode = data.mode; flyout.close(); flyout.open(); wait(20)
        verify(textItem(flyout, "Account ›").visible)
        verify(textItem(flyout, "Devices ›").visible)
        verify(!textItem(flyout, "Data & privacy"))
        tryCompare(textItem(flyout, "Account ›").parent, "activeFocus", true)
    }
}
