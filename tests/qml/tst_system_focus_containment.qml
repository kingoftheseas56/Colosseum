import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/SystemFocusContainment.js" as FocusContainment
import "../../qml/account" as Account

TestCase {
    name: "SystemFocusContainment"
    when: windowShown

    Window { id: testWindow; width: 480; height: 320; visible: true }

    Component {
        id: containerComp
        Item {
            width: 320; height: 220
            Item {
                id: decoy
                objectName: "focusDecoy"
                width: 10; height: 10
                visible: true; enabled: true
                activeFocusOnTab: false
            }
            Button {
                id: firstAction
                objectName: "focusFirstAction"
                y: 40; width: 160; height: 40
                text: "First action"
                focusPolicy: Qt.StrongFocus
            }
            Button {
                id: secondAction
                objectName: "focusSecondAction"
                y: 90; width: 160; height: 40
                text: "Second action"
                focusPolicy: Qt.StrongFocus
            }
        }
    }

    Component {
        id: welcomeComp
        Account.AccountWelcome {
            width: 460; height: 300
            controller: null
        }
    }

    property var container: null
    property var welcome: null

    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findByName(children[i], name)
            if (found) return found
        }
        return null
    }

    function init() {
        testWindow.requestActivate()
        container = containerComp.createObject(testWindow.contentItem)
        verify(container !== null)
        wait(20)
    }
    function cleanup() {
        if (welcome) welcome.destroy()
        if (container) container.destroy()
        welcome = null
        container = null
    }

    function test_move_skips_non_tab_focusable_items() {
        var first = findByName(container, "focusFirstAction")
        verify(first !== null)
        verify(FocusContainment.move(testWindow, container, true))
        compare(testWindow.activeFocusItem, first)
    }

    function test_real_account_welcome_enters_first_choice() {
        welcome = welcomeComp.createObject(testWindow.contentItem)
        verify(welcome !== null)
        wait(20)
        var first = findByName(welcome, "accountWelcomeContinueLocal")
        verify(first !== null)
        verify(FocusContainment.move(testWindow, welcome, true))
        compare(testWindow.activeFocusItem, first)
    }

    function test_move_advances_between_real_controls() {
        var first = findByName(container, "focusFirstAction")
        var second = findByName(container, "focusSecondAction")
        verify(first !== null && second !== null)
        first.forceActiveFocus(Qt.TabFocusReason)
        compare(testWindow.activeFocusItem, first)
        verify(FocusContainment.move(testWindow, container, true))
        compare(testWindow.activeFocusItem, second)
    }
}
