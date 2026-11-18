import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "AudiobookSourcePicker"
    width: 800
    height: 600
    visible: true
    when: windowShown

    property var picker
    property var picks: []

    function openPicker(rows) {
        const component = Qt.createComponent("../../qml/audiobook/AudiobookSourcePicker.qml")
        compare(component.status, Component.Ready, component.errorString())
        picker = createTemporaryObject(component, testCase, {rows: rows, workTitle: "Dune"})
        verify(picker)
        picker.sourcePicked.connect(row => picks.push(row))
        picker.open()
        tryCompare(picker, "opened", true)
        if (rows.length > 0)
            tryVerify(() => item("audiobookSourceOptions").height > 0)
    }

    function item(name) {
        const child = findChild(picker.contentItem, name)
        verify(child, name)
        return child
    }

    function init() { picks = [] }

    function test_sourceEvidenceAndExplicitChoice() {
        openPicker([
            { title: "Dune", format: "M4B", fileCount: 1, size: "1.5 GB", narrators: ["Scott Brick"],
                verdict: "exact", recommended: true, request: {infoHash: "selected"} },
            { title: "Dune collection", bundle: true, bundleCount: 3, reason: "Different narration",
                verdict: "none", request: {infoHash: "other"} },
            { title: "Dune collection", bundle: true, reason: "Unconfirmed", verdict: "none" }
        ])
        compare(item("audiobookSourceWorkTitle").text, "Dune")
        compare(item("audiobookSourceSummary0").text, "M4B · 1 file · 1.5 GB · narrated by Scott Brick")
        compare(item("audiobookSourceRecommendation_0").text, "Recommended")
        verify(item("audiobookSourceRecommendation_0").visible)
        compare(item("audiobookSourceBundle1").text, "Bundle · 3 books")
        compare(item("audiobookSourceBundle2").text, "Bundle")
        compare(item("audiobookSourceReason1").text, "Different narration")
        compare(picks.length, 0)
        mouseClick(item("audiobookSourceOption_2"))
        compare(picks.length, 0)
        compare(picker.opened, true)
        mouseClick(item("audiobookSourceOption_1"))
        compare(picks.length, 1)
        compare(picks[0].request.infoHash, "other")
        tryCompare(picker, "opened", false)
    }

    function test_keyboardChoice() {
        openPicker([{title: "Unavailable", reason: "Unconfirmed"}, {title: "Dune", request: {infoHash: "selected"}}])
        tryCompare(item("audiobookSourceClose"), "activeFocus", true)
        keyClick(Qt.Key_Tab)
        tryCompare(item("audiobookSourceOption_1"), "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(picks.length, 1)
        compare(picks[0].request.infoHash, "selected")
        tryCompare(picker, "opened", false)
    }

    function test_tabScrollsLongList() {
        const rows = Array.from({length: 12}, (_, index) => ({title: "Upload " + index, request: index + 1}))
        openPicker(rows)
        const list = item("audiobookSourceOptions")
        verify(list.contentHeight > list.height)
        compare(list.contentY, 0)
        for (let index = 0; index < rows.length; ++index) {
            keyClick(Qt.Key_Tab)
            tryCompare(item("audiobookSourceOption_" + index), "activeFocus", true)
        }
        verify(list.contentY > 0)
        keyClick(Qt.Key_Space)
        compare(picks.length, 1)
        compare(picks[0].request, 12)
        tryCompare(picker, "opened", false)
    }

    function test_emptyAndDismiss() {
        openPicker([])
        verify(item("audiobookSourceEmpty").visible)
        keyClick(Qt.Key_Escape)
        tryCompare(picker, "opened", false)
        picker.open()
        tryCompare(picker, "opened", true)
        keyClick(Qt.Key_Back)
        tryCompare(picker, "opened", false)
        picker.open()
        tryCompare(picker, "opened", true)
        mouseClick(item("audiobookSourceClose"))
        tryCompare(picker, "opened", false)
        compare(picks.length, 0)
    }
}
