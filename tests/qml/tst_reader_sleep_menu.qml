import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3

TestCase {
    id: suite
    name: "ReaderSleepMenu"
    Arc64Fonts { id: fonts }
    function initTestCase() { tryCompare(fonts, "ready", true) }
    when: windowShown

    Window {
        id: testWindow
        width: 480
        height: 360
        visible: true
        color: "#08080a"
    }

    Rectangle { id: frame; parent: testWindow.contentItem; x:20; y:20; color:"#08080a"; width:206; height:240 }
    Rectangle { id: glyphFrame; parent: testWindow.contentItem; x:250; y:20; width:160; height:48; color:"#08080a"
        Row { anchors.centerIn: parent; spacing:12
            Loader { width:28; height:28; source:"../../qml/reader2/AudioGlyph.qml"; onLoaded: { item.kind="speed"; item.ink="white" } }
            Loader { id: moon; width:28; height:28; source:"../../qml/reader2/AudioGlyph.qml"; onLoaded: { item.kind="sleep"; item.ink="white" } }
            Loader { width:28; height:28; source:"../../qml/reader2/AudioGlyph.qml"; onLoaded: { item.kind="playlist"; item.ink="white" } }
        }
    }
    property var subject: null
    property var subjectComponent: null
    property var emitted: []

    function componentForMenu() {
        if (!subjectComponent)
            subjectComponent = Qt.createComponent(
                        Qt.resolvedUrl("../../qml/reader2/SleepTimerMenu.qml"))
        compare(subjectComponent.status, Component.Ready, subjectComponent.errorString())
        return subjectComponent
    }
    function createMenu(mode) {
        emitted = []
        subject = componentForMenu().createObject(frame, {
            "x": 0,
            "y": 0,
            "mode": mode,
            "open": true
        })
        verify(subject !== null)
        subject.chosen.connect(function(value) { emitted.push(value) })
        testWindow.requestActivate()
        wait(0)
        return subject
    }

    function findNamed(node, objectName) {
        if (!node)
            return null
        if (node.objectName === objectName)
            return node
        var kids = node.children || []
        for (var i = 0; i < kids.length; ++i) {
            var hit = findNamed(kids[i], objectName)
            if (hit)
                return hit
        }
        return null
    }

    function cleanup() {
        if (subject)
            subject.destroy()
        subject = null
        emitted = []
    }

    function test_rows_in_order_and_current_mode_is_gold() {
        var menu = createMenu(45)
        compare(menu.choices.length, 6)
        compare(menu.choices[0].label, "Off")
        compare(menu.choices[1].label, "15 min")
        compare(menu.choices[2].label, "30 min")
        compare(menu.choices[3].label, "45 min")
        compare(menu.choices[4].label, "60 min")
        compare(menu.choices[5].label, "End of chapter")

        var mark = findNamed(menu, "sleepTimerMark3")
        var label = findNamed(menu, "sleepTimerLabel3")
        verify(mark !== null)
        verify(label !== null)
        compare(mark.visible, true)
        verify(Qt.colorEqual(mark.color, "#F0C24A"))
        verify(Qt.colorEqual(label.color, "#F0C24A"))
    }

    function test_click_emits_right_mode_once_data() {
        return [
            { tag: "off", row: 0, expected: "off" },
            { tag: "15", row: 1, expected: 15 },
            { tag: "30", row: 2, expected: 30 },
            { tag: "45", row: 3, expected: 45 },
            { tag: "60", row: 4, expected: 60 },
            { tag: "chapter-end", row: 5, expected: "chapterEnd" }
        ]
    }

    function test_click_emits_right_mode_once(data) {
        var menu = createMenu("off")
        var action = findNamed(menu, "sleepTimerAction" + data.row)
        verify(action !== null)
        mouseClick(action, action.width / 2, action.height / 2, Qt.LeftButton)
        compare(emitted.length, 1)
        compare(emitted[0], data.expected)
        compare(menu.open, false)
    }

    function test_arrows_move_and_enter_chooses_once() {
        var menu = createMenu("off")
        menu.forceActiveFocus(Qt.OtherFocusReason)
        tryCompare(menu, "activeFocus", true)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        compare(menu.highlightedIndex, 3)
        keyClick(Qt.Key_Enter)
        compare(emitted.length, 1)
        compare(emitted[0], 45)
        compare(menu.open, false)
    }

    function test_up_arrow_moves_and_clamps() {
        var menu = createMenu(30)
        menu.forceActiveFocus(Qt.OtherFocusReason)
        tryCompare(menu, "activeFocus", true)
        keyClick(Qt.Key_Up)
        compare(menu.highlightedIndex, 1)
        keyClick(Qt.Key_Home)
        keyClick(Qt.Key_Up)
        compare(menu.highlightedIndex, 0)
        keyClick(Qt.Key_End)
        keyClick(Qt.Key_Down)
        compare(menu.highlightedIndex, 5)
    }

    function test_visualEvidence() {
        if (typeof arc64EvidenceOutputDirectory === "undefined" || !arc64EvidenceOutputDirectory)
            skip("Set ARC64_EVIDENCE_DIR and use the optional Arc64 capture runner to save images")
        createMenu(30); waitForRendering(frame); grabImage(frame).save(arc64EvidenceOutputDirectory + "/P3/menu.png")
        tryVerify(function() {return moon.item !== null})
        moon.item.ink="white"; waitForRendering(glyphFrame); grabImage(glyphFrame).save(arc64EvidenceOutputDirectory + "/P3/glyphs-idle.png")
        moon.item.ink="#F0C24A"; waitForRendering(glyphFrame); grabImage(glyphFrame).save(arc64EvidenceOutputDirectory + "/P3/glyphs-gold.png")
    }
    function test_escape_closes_without_emitting() {
        var menu = createMenu(30)
        menu.forceActiveFocus(Qt.OtherFocusReason)
        tryCompare(menu, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        compare(menu.open, false)
        compare(emitted.length, 0)
    }
}
