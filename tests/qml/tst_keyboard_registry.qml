import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "KeyboardRegistry"
    when: windowShown

    Window { id: testWindow; width: 320; height: 240; visible: true }

    Component {
        id: commandComp
        Colosseum.KeyboardCommand {
            semanticId: "test.open"
            label: "Open"
            category: "Global"
            scope: "application"
            sequences: ["Ctrl+O"]
        }
    }
    Component { id: registryComp; Colosseum.KeyboardRegistry {} }

    property var command: null
    property var registry: null
    SignalSpy { id: commandSpy; signalName: "triggered" }
    SignalSpy { id: registrySpy; signalName: "commandTriggered" }

    function init() {
        command = commandComp.createObject(testWindow)
        registry = registryComp.createObject(testWindow)
        verify(command !== null)
        verify(registry !== null)
        commandSpy.target = command
        registrySpy.target = registry
        wait(20)
    }

    function cleanup() {
        commandSpy.clear()
        registrySpy.clear()
        commandSpy.target = null
        registrySpy.target = null
        if (command) command.destroy()
        if (registry) registry.destroy()
        command = null
        registry = null
    }

    function test_command_exposes_semantic_metadata() {
        compare(command.semanticId, "test.open")
        compare(command.label, "Open")
        compare(command.category, "Global")
        compare(command.scope, "application")
        compare(command.sequences.length, 1)
        compare(command.sequences[0], "Ctrl+O")
        compare(command.enabled, true)
    }

    function test_enabled_command_invokes_once() {
        command.invoke("test-source")
        compare(commandSpy.count, 1)
        compare(commandSpy.signalArguments[0][0], "test-source")
    }

    function test_disabled_command_is_a_noop() {
        command.enabled = false
        command.invoke("test-source")
        compare(commandSpy.count, 0)
    }

    function test_registry_registers_and_invokes_command() {
        verify(registry.registerCommand(command))
        compare(registry.command("test.open"), command)
        compare(registry.snapshot().length, 1)
        registry.invoke("test.open", "registry-source")
        compare(commandSpy.count, 1)
        compare(registrySpy.count, 1)
        compare(registrySpy.signalArguments[0][0], "test.open")
    }

    function test_registry_rejects_duplicate_semantic_id() {
        verify(registry.registerCommand(command))
        var duplicate = commandComp.createObject(testWindow)
        verify(duplicate !== null)
        verify(!registry.registerCommand(duplicate))
        compare(registry.snapshot().length, 1)
        duplicate.destroy()
    }

    function test_registry_unregisters_command_and_preserves_order() {
        var second = commandComp.createObject(testWindow, {
            semanticId: "test.close",
            label: "Close",
            sequences: ["Esc"]
        })
        verify(second !== null)
        verify(registry.registerCommand(command))
        verify(registry.registerCommand(second))
        compare(registry.snapshot()[0].semanticId, "test.open")
        compare(registry.snapshot()[1].semanticId, "test.close")
        verify(registry.unregisterCommand(command))
        compare(registry.snapshot().length, 1)
        compare(registry.snapshot()[0].semanticId, "test.close")
        second.destroy()
    }
}
