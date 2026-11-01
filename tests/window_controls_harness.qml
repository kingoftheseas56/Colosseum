// Focused behavioral proof of the shared desktop chrome; no application launch.
import QtQuick
import QtTest
import "../qml"

Item {
    id: harness
    width: 460; height: 120
    property int minimized: 0
    property int fullscreen: 0
    property int quit: 0

    HouseWindowControls {
        id: chrome
        x: 35; y: 25
        objectPrefix: "testWindow"
        onMinimizeRequested: harness.minimized++
        onFullscreenRequested: harness.fullscreen++
        onQuitRequested: harness.quit++
    }

    TestCase {
        name: "HouseWindowControls"
        when: windowShown
        function init() {
            harness.minimized = 0
            harness.fullscreen = 0
            harness.quit = 0
        }
        function test_0_three_accessible_actions() {
            compare(chrome.children.length, 3)
            compare(chrome.children[0].objectName, "testWindowMinimize")
            compare(chrome.children[1].objectName, "testWindowFullscreen")
            compare(chrome.children[2].objectName, "testWindowQuit")
            verify(chrome.visible)
            verify(chrome.implicitWidth >= 100)
            for (let i = 0; i < 3; ++i) {
                verify(chrome.children[i].children[1].activeFocusOnTab)
                tryCompare(chrome.children[i].children[0], "status", Image.Ready, 3000)
            }
        }
        function test_1_pointer_routes() {
            mouseClick(chrome.children[0], 11, 11)
            mouseClick(chrome.children[1], 11, 11)
            mouseClick(chrome.children[2], 11, 11)
            compare(harness.minimized, 1)
            compare(harness.fullscreen, 1)
            compare(harness.quit, 1)
        }
        function test_2_keyboard_routes() {
            for (let i = 0; i < 3; ++i) {
                const input = chrome.children[i].children[1]
                input.forceActiveFocus(Qt.TabFocusReason)
                verify(input.activeFocus, "Expected keyboard focus on action " + i)
                keyClick(Qt.Key_Return)
            }
            compare(harness.minimized, 1)
            compare(harness.fullscreen, 1)
            compare(harness.quit, 1)
        }
    }
}
