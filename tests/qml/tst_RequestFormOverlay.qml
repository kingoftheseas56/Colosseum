import QtQuick
import QtTest
import "../../qml" as App

Item {
    // The OnePlus reports 2772×1240 physical pixels at about 2.9 pixels per Qt unit.
    width: 956
    height: 428

    QtObject {
        id: simulatedIme
        property bool visible: false
        property rect keyboardRectangle: Qt.rect(0, 0, 0, 0)
        function hide() { visible = false }
    }

    QtObject {
        id: fakeService
        property var lastSubmission
        signal submissionFinished(bool ok, string message)
        signal universeResults(string query, var rows)
        function submit(kind, fields) { lastSubmission = {kind: kind, fields: fields} }
        function searchUniverses(query) { }
        function newReference() { return "test-reference" }
    }

    QtObject {
        id: fakePlatform
        property real dockedKeyboardTop: -1
        function sampleDockedKeyboard() { }
    }

    App.RequestFormOverlay {
        id: overlay
        anchors.fill: parent
        service: fakeService
        inputMethod: simulatedIme
        keyboardPlatform: fakePlatform
        touchInput: true
    }

    TestCase {
        name: "RequestFormOverlay"
        when: windowShown

        function init() {
            simulatedIme.hide()
            fakePlatform.dockedKeyboardTop = -1
            overlay.touchInput = true
            overlay.kind = "bug"
            tryVerify(function() { return !!findChild(overlay, "requestField_message") })
        }

        function test_desktopFooterLeavesRoomForFields() {
            overlay.touchInput = false
            const scroll = findChild(overlay, "requestFormScroll")
            const cancel = findChild(overlay, "requestFormCancel")
            const send = findChild(overlay, "requestFormSend")
            tryVerify(function() {
                return cancel.visible && cancel.height === send.height && scroll.height >= 140
            })
            compare(cancel.background.color, "#0affffff")
        }

        function test_dockedKeyboardKeepsFieldAndSendVisible_data() {
            return [
                {tag: "bug", kind: "bug", field: "message"},
                {tag: "feature", kind: "feature", field: "benefit"},
                {tag: "universe", kind: "universe", field: "title"}
            ]
        }

        function test_dockedKeyboardKeepsFieldAndSendVisible(row) {
            overlay.kind = row.kind
            tryVerify(function() { return !!findChild(overlay, "requestField_" + row.field) })
            const editor = findChild(overlay, "requestField_" + row.field)
            const send = findChild(overlay, "requestFormSend")
            const scroll = findChild(overlay, "requestFormScroll")
            editor.forceActiveFocus()
            fakePlatform.dockedKeyboardTop = 167
            // Reproduce the stale Qt geometry observed after changing fields.
            simulatedIme.keyboardRectangle = Qt.rect(0, 428, 956, 1)
            simulatedIme.visible = true
            tryVerify(function() {
                const editorTop = editor.mapToItem(overlay, 0, 0).y
                const editorBottom = editor.mapToItem(overlay, 0, editor.height).y
                return editorTop >= scroll.mapToItem(overlay, 0, 0).y - 1
                    && editorBottom <= scroll.mapToItem(overlay, 0, scroll.height).y + 1
                    && send.mapToItem(overlay, 0, send.height).y <= fakePlatform.dockedKeyboardTop
            })
        }

        function typeText(text) {
            for (let i = 0; i < text.length; ++i)
                keyClick(text.charAt(i))
        }

        function test_focusedMultilineAndSendStayAboveLandscapeKeyboard() {
            const panel = findChild(overlay, "requestFormPanel")
            const description = findChild(overlay, "requestField_message")
            const send = findChild(overlay, "requestFormSend")
            const close = findChild(overlay, "requestFormClose")
            const scroll = findChild(overlay, "requestFormScroll")
            verify(panel && description && send && close && scroll)
            const normalHeight = panel.height
            const normalHeaderHeight = close.height
            description.forceActiveFocus()
            tryCompare(description, "activeFocus", true)
            simulatedIme.keyboardRectangle = Qt.rect(0, 152, 900, 288)
            simulatedIme.visible = true
            tryVerify(function() { return panel.height < normalHeight && close.height < normalHeaderHeight })
            const viewport = findChild(overlay, "requestViewport_message")
            tryVerify(function() {
                return Math.abs(viewport.mapToItem(overlay, 0, viewport.height).y
                    - scroll.mapToItem(overlay, 0, scroll.height).y) <= 1
            })
            const firstTop = viewport.mapToItem(overlay, 0, 0).y
            const firstHeight = viewport.height
            typeText("A useful description")
            keyClick(Qt.Key_Return)
            typeText("Second line")
            tryVerify(function() { return viewport.height > firstHeight })
            tryVerify(function() {
                const top = viewport.mapToItem(overlay, 0, 0).y
                const scrollTop = scroll.mapToItem(overlay, 0, 0).y
                const sendBottom = send.mapToItem(overlay, 0, send.height).y
                return top < firstTop
                    && top >= scrollTop - 1
                    && top + viewport.height <= scrollTop + scroll.height + 1
                    && sendBottom <= simulatedIme.keyboardRectangle.y
            })
            simulatedIme.hide()
            tryCompare(panel, "height", normalHeight)
            tryCompare(close, "height", normalHeaderHeight)
            compare(description.text, "A useful description\nSecond line")
        }

        function test_existingUniverseSuggestionsAreFilteredAndManualNameCanSend() {
            overlay.kind = "universe"
            overlay.availableUniverses = ["Pokémon"]
            tryVerify(function() { return findChild(overlay, "requestField_title") !== null })
            const search = findChild(overlay, "requestField_title")
            search.forceActiveFocus()
            typeText("Godzilla")
            compare(overlay.query, "Godzilla")
            fakeService.universeResults("Godzilla", [
                {title: "Pokémon (franchise)", pageid: 1},
                {title: "Godzilla (franchise)", pageid: 2}
            ])
            compare(overlay.suggestions.length, 1)
            compare(overlay.suggestions[0].pageid, 2)
            verify(!overlay.canSend)
            overlay.manualName = true
            verify(overlay.canSend)
            overlay.send()
            compare(fakeService.lastSubmission.kind, "universe")
            compare(fakeService.lastSubmission.fields.title, "Godzilla")
            compare(fakeService.lastSubmission.fields.message, "Godzilla")
            compare(fakeService.lastSubmission.fields.reference, "test-reference")
            fakeService.submissionFinished(false, "Try again.")
            overlay.send()
            compare(fakeService.lastSubmission.fields.reference, "test-reference")
            fakeService.submissionFinished(true, "Sent.")
            verify(overlay.sent)
        }
    }
}
