import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: suite
    name: "AudiobookOfferButton"
    when: windowShown

    Arc64Fonts { id: fonts }
    function initTestCase() { tryCompare(fonts, "ready", true) }

    App.Theme { id: testTheme }

    Window {
        id: stage
        width: 640
        height: 320
        visible: true
        color: "#0a0a0b"

        Rectangle {
            id: buttonFrame
            x: 40; y: 40
            width: 268; height: 50
            color: "#0a0a0b"

            App.AudiobookOfferButton {
                id: button
                anchors.fill: parent
                theme: testTheme
            }
        }

        Rectangle {
            id: coverFrame
            x: 360; y: 40
            width: 132; height: 198
            color: "#16303f"

            App.AudiobookCoverMark {
                id: coverMark
                downloaded: false
            }
        }
    }

    SignalSpy {
        id: activatedSpy
        target: button
        signalName: "activated"
    }

    function init() {
        activatedSpy.clear()
        button.offerState = "available"
        button.label = "Get audiobook"
        button.meta = "M4B · English · 412 MB"
        button.progress = 0
        coverMark.downloaded = false
        wait(0)
    }

    function stateRows() {
        return [
            { tag: "checking", state: "checking", label: "Looking for an audiobook…",
              meta: "", shown: true, active: false },
            { tag: "none", state: "none", label: "No audiobook found",
              meta: "", shown: true, active: false },
            { tag: "unknown", state: "unknown", label: "Could not check",
              meta: "Network unavailable", shown: false, active: false },
            { tag: "available", state: "available", label: "Get audiobook",
              meta: "M4B · English · 412 MB", shown: true, active: true },
            { tag: "choose", state: "choose", label: "Choose audiobook",
              meta: "4 possible matches · none certain", shown: true, active: true },
            { tag: "getting", state: "getting", label: "Getting audiobook · 37%",
              meta: "M4B · English · 412 MB", shown: true, active: false },
            { tag: "failed", state: "failed", label: "Couldn't get the audiobook · Try again",
              meta: "", shown: true, active: true },
            { tag: "ready", state: "ready", label: "Listen",
              meta: "M4B · 412 MB", shown: true, active: true }
        ]
    }

    function test_states_data() { return stateRows() }

    function test_states(data) {
        button.offerState = data.state
        button.label = data.label
        button.meta = data.meta
        button.progress = data.state === "getting" ? 0.37 : 0
        stage.requestActivate()
        wait(0)

        compare(button.visible, data.shown)
        compare(button.width, 268)
        compare(button.height, 50)

        if (data.shown) {
            compare(findChild(button, "audiobookOfferLabel").text, data.label)
            compare(findChild(button, "audiobookOfferMeta").text, data.meta)
        }

        compare(findChild(button, "audiobookOfferAction").enabled, data.active)
        if (data.state === "getting")
            fuzzyCompare(findChild(button, "audiobookOfferProgressFill").width,
                         268 * 0.37, 0.1)
    }

    function test_pointerActivation_data() { return stateRows() }

    function test_pointerActivation(data) {
        button.offerState = data.state
        button.label = data.label
        button.meta = data.meta
        wait(0)

        if (button.visible)
            mouseClick(button, button.width / 2, button.height / 2)
        compare(activatedSpy.count, data.active ? 1 : 0)
    }

    function test_enterActivates() {
        const states = ["available", "choose", "failed", "ready"]
        const action = findChild(button, "audiobookOfferAction")
        for (let i = 0; i < states.length; ++i) {
            activatedSpy.clear()
            button.offerState = states[i]
            stage.requestActivate()
            action.forceActiveFocus()
            tryVerify(function() { return action.activeFocus })
            keyClick(Qt.Key_Return)
            compare(activatedSpy.count, 1, states[i] + " activates with Enter")
        }
    }

    function test_coverMark() {
        compare(coverMark.width, 26)
        compare(coverMark.height, 26)
        compare(coverMark.x, coverFrame.width - 7 - 26)
        compare(coverMark.y, 7)
        verify(!coverMark.visible)
        coverMark.downloaded = true
        verify(coverMark.visible)
    }

    function saveEvidence(item, fileName) {
        waitForRendering(item)
        grabImage(item).save(arc64EvidenceOutputDirectory + "/P1/" + fileName)
    }

    function test_visualEvidence() {
        if (typeof arc64EvidenceOutputDirectory === "undefined" || !arc64EvidenceOutputDirectory)
            skip("Set ARC64_EVIDENCE_DIR and use the optional Arc64 capture runner to save images")
        stage.contentItem.forceActiveFocus()
        const rows = stateRows()
        for (let i = 0; i < rows.length; ++i) {
            button.offerState = rows[i].state
            button.label = rows[i].label
            button.meta = rows[i].meta
            button.progress = rows[i].state === "getting" ? 0.37 : 0
            wait(0)
            saveEvidence(buttonFrame, "button-" + rows[i].state + ".png")
        }

        coverMark.downloaded = true
        wait(0)
        saveEvidence(coverFrame, "cover-downloaded.png")
    }
}
