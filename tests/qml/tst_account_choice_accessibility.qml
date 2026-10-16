import QtQuick
import QtTest
import "../../qml/account" as Account

TestCase {
    name: "AccountChoiceAccessibility"
    Component { id: standard; Account.AccountChoice { title: "Continue without account" } }
    Component { id: welcome; Account.AccountWelcomeChoice { title: "Continue without account" } }
    function test_accessible_press_data() {
        return [{ tag: "standard", component: standard }, { tag: "welcome", component: welcome }]
    }
    function test_accessible_press(data) {
        var choice = createTemporaryObject(data.component, this)
        verify(choice !== null)
        compare(choice.Accessible.role, Accessible.Button)
        compare(choice.Accessible.name, "Continue without account")
        var count = 0
        choice.chosen.connect(function() { count++ })
        choice.Accessible.pressAction()
        compare(count, 1)
        choice.enabled = false
        choice.Accessible.pressAction()
        compare(count, 1)
    }
}
