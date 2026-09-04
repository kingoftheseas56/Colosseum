import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as App

TestCase {
    name: "StarWarsGalaxyFocus"
    when: windowShown
    Window { id: testWindow; width: 1280; height: 720; visible: true }
    Component {
        id: galaxyComp
        App.StarWarsGalaxySystem {
            width: 1280; height: 720
            destinations: [
                { id:"high", name:"HIGH REPUBLIC", orbit:170, angle:3.49, size:27, y:5, light:"#eee", mid:"#999", dark:"#222" },
                { id:"fall", name:"FALL OF THE JEDI", orbit:245, angle:2.36, size:32, y:-4, light:"#bbb", mid:"#777", dark:"#222" }
            ]
        }
    }
    property var subject: null
    function cleanup() { if (subject) subject.destroy(); subject = null }
    function test_focusEntry_activates_skywalker_node() {
        subject = galaxyComp.createObject(testWindow.contentItem)
        verify(subject !== null)
        testWindow.requestActivate()
        subject.takeKeyboardFocus()
        var skywalker = findChild(subject, "starWarsGalaxySkywalker")
        verify(skywalker !== null)
        tryCompare(skywalker, "activeFocus", true)
    }
}