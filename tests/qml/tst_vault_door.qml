import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault execution Slice 15 — the alive door's state machine. Drives the PRODUCTION
// VaultDoor component with SEEDED inputs (the facts Taskbar wires from VaultLibrary in the
// app) and asserts the planned doorState sequence: idle → scanning → arrival-pulse → idle.
// The arrival pulse is time-boxed (≥ 2s) — long enough to be observable at 50ms Lanista
// polls, and asserted here with real waits. No counts on the door (spec §3) — the dot and
// the glow are the whole vocabulary.
TestCase {
    name: "VaultDoor"
    when: windowShown

    Window { id: testWindow; width: 300; height: 200; visible: true }

    Component { id: doorComp; Colosseum.VaultDoor {} }
    property var door: null
    property var doorSpy: null

    SignalSpy { id: clickedSpy; signalName: "clicked" }

    function init() {
        // The production door is sized by Taskbar's Layout; a bare instance is 0x0, so the
        // test sizes it like Taskbar does (46x46) before clicking.
        door = doorComp.createObject(testWindow, { width: 46, height: 46 })
        verify(door !== null)
        clickedSpy.target = door
        wait(30)
    }
    function cleanup() {
        clickedSpy.clear()
        clickedSpy.target = null
        if (door) door.destroy()
        door = null
    }

    function test_idle_at_boot() {
        compare(door.doorState, "idle")
        compare(door.arrivalPulse, false)
        verify(findChild(door, "vaultDoorScanDot") !== null)
    }

    function test_scanning_raises_quiet_dot() {
        door.scanning = true
        compare(door.doorState, "scanning")
        verify(findChild(door, "vaultDoorScanDot").visible === true)
        door.scanning = false
        compare(door.doorState, "idle")
    }

    function test_door_state_sequence_idle_scanning_arrival_idle() {
        // The plan's sequence, seeded: idle → scanning → arrival-pulse → idle.
        compare(door.doorState, "idle")
        door.scanning = true
        compare(door.doorState, "scanning")
        door.arrivalTick = 1                      // a live-shelf landing
        compare(door.doorState, "arrival")        // the pulse wins over the dot
        verify(findChild(door, "vaultDoorScanDot").visible === true) // dot still armed beneath
        wait(2900)                                // > the 2400ms pulse window
        compare(door.doorState, "scanning")       // pulse over → back to the quiet dot
        door.scanning = false
        compare(door.doorState, "idle")
    }

    function test_landing_without_scan_pulses_and_settles_idle() {
        door.arrivalTick = 1
        compare(door.doorState, "arrival")
        wait(2900)
        compare(door.doorState, "idle")
    }

    function test_repeated_landings_restart_the_pulse() {
        door.arrivalTick = 1
        compare(door.doorState, "arrival")
        wait(1500)
        compare(door.doorState, "arrival")        // still inside the first window
        door.arrivalTick = 2                      // a second landing restarts the clock
        wait(1500)
        compare(door.doorState, "arrival")        // still pulsing 1.5s after the restart
        wait(1400)                                // 2.9s since the restart — beyond 2.4s
        compare(door.doorState, "idle")
    }

    function test_click_emits_clicked_once() {
        mouseClick(door)
        compare(clickedSpy.count, 1)
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
