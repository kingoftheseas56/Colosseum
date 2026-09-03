import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "KeyboardGuideRegistry"
    when: windowShown

    Window { id: testWindow; width: 900; height: 700; visible: true }

    Component {
        id: pageComp
        Colosseum.KeyboardGuidePage {
            width: 900
            height: 700
        }
    }
    Component { id: registryComp; Colosseum.KeyboardRegistry {} }
    Component {
        id: commandComp
        Colosseum.KeyboardCommand {
            semanticId: "test.open"
            label: "Open test"
            category: "Shortcuts"
            scope: "application"
            sequences: ["Ctrl+O"]
            icon: "file.svg"
        }
    }

    property var page: null
    property var registry: null
    property var openCommand: null
    property var saveCommand: null

    function init() {
        registry = registryComp.createObject(testWindow)
        page = pageComp.createObject(testWindow, { keyboardRegistry: registry, visible: true })
        openCommand = commandComp.createObject(testWindow)
        saveCommand = commandComp.createObject(testWindow, {
            semanticId: "test.save",
            label: "Save test",
            sequences: ["Ctrl+S"],
            icon: "settings.svg"
        })
        verify(registry !== null)
        verify(page !== null)
        verify(openCommand !== null)
        verify(saveCommand !== null)
        wait(40)
    }

    function cleanup() {
        if (page) page.destroy()
        if (openCommand) openCommand.destroy()
        if (saveCommand) saveCommand.destroy()
        if (registry) registry.destroy()
        page = null
        openCommand = null
        saveCommand = null
        registry = null
    }

    function test_rows_follow_registry_metadata_and_revision() {
        compare(page.shortcutRows.length, 0)
        verify(registry.registerCommand(openCommand))
        verify(registry.registerCommand(saveCommand))
        wait(20)

        compare(page.shortcutRows.length, 2)
        compare(page.shortcutRows[0].chord, "Ctrl+O")
        compare(page.shortcutRows[0].tokens.join(""), "Ctrl+O")
        compare(page.shortcutRows[0].action, "Open test")
        compare(page.shortcutRows[1].chord, "Ctrl+S")
        compare(page.shortcutRows[1].action, "Save test")

        verify(registry.unregisterCommand(openCommand))
        wait(20)
        compare(page.shortcutRows.length, 1)
        compare(page.shortcutRows[0].semanticId, "test.save")
        compare(page.shortcutRows[0].chord, "Ctrl+S")
    }
}
