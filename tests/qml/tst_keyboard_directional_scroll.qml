import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import QtQuick.Controls 2.15
import "../../qml" as Colosseum

// Arc 41 directional continuation contract.  The fixtures use real keyClick()
// delivery: a focused KeyboardAction bubbles its unaccepted arrow to the owning
// parent, which gives spatial navigation first refusal and then gives the same
// event to the Flickable's KeyboardScrollController.
TestCase {
    id: testCase
    name: "KeyboardDirectionalScroll"
    when: windowShown

    Window {
        id: testWindow
        width: 700
        height: 520
        visible: true

        Item {
            id: scrollRegion
            objectName: "scrollRegion"
            width: 390
            height: 500
            focus: true

            Flickable {
                id: mainFlick
                objectName: "mainFlick"
                x: 0
                y: 0
                width: 300
                height: 180
                clip: true
                contentWidth: width
                contentHeight: mainContent.height

                Item {
                    id: mainContent
                    width: mainFlick.width
                    height: 12 * 58

                    Repeater {
                        id: rows
                        model: 12
                        delegate: Colosseum.KeyboardAction {
                            objectName: "row" + index
                            x: 12
                            y: index * 58
                            width: 250
                            height: 42
                            pointerEnabled: false
                            accessibleName: objectName
                        }
                    }

                    // This action is inside the owning content but is clipped by
                    // the Flickable; it must never become a directional stop.
                    Item {
                        id: clippedHost
                        x: 12
                        y: 95
                        width: 250
                        height: 20
                        clip: true

                        Colosseum.KeyboardAction {
                            id: clippedAction
                            objectName: "clippedAction"
                            x: 0
                            y: 30
                            width: 250
                            height: 42
                            pointerEnabled: false
                        }
                    }

                    Colosseum.KeyboardAction {
                        id: hiddenAction
                        objectName: "hiddenAction"
                        x: 12
                        y: 125
                        width: 250
                        height: 42
                        visible: false
                        pointerEnabled: false
                    }
                    Colosseum.KeyboardAction {
                        id: disabledAction
                        objectName: "disabledAction"
                        x: 12
                        y: 155
                        width: 250
                        height: 42
                        enabled: false
                        pointerEnabled: false
                    }
                }

                Keys.onPressed: function(event) {
                    if (!event.accepted)
                        scrollKeys.handle(event)
                }
            }

            // The unrelated chrome is deliberately placed where a naive global
            // spatial search would win when the next collection row is clipped.
            Colosseum.KeyboardAction {
                id: unrelatedChrome
                objectName: "unrelatedChrome"
                x: 12
                y: 238
                width: 250
                height: 42
                pointerEnabled: false
            }

            TextInput {
                id: editable
                objectName: "editable"
                x: 12
                y: 300
                width: 250
                height: 42
                text: "editable"
                focusPolicy: Qt.TabFocus
            }

            TextEdit {
                id: textEditor
                objectName: "textEditor"
                x: 280
                y: 360
                width: 100
                height: 42
                text: "editable text"
                focus: false
            }

            Slider {
                id: slider
                objectName: "slider"
                x: 280
                y: 300
                width: 100
                from: 0
                to: 10
                value: 5
                focusPolicy: Qt.TabFocus
            }

            Item {
                id: scopedOwner
                objectName: "scopedOwner"
                x: 12
                y: 360
                width: 250
                height: 42
                focus: true
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Down) {
                        scrollRegion.scopedDown += 1
                        event.accepted = true
                    }
                }
            }

            Colosseum.KeyboardSpatialNavigator {
                id: spatial
                root: scrollRegion
            }

            Colosseum.KeyboardScrollController {
                id: scrollKeys
                flick: mainFlick
                lineStep: 58
                pageFraction: 0.85
            }

            Keys.onPressed: function(event) {
                if (!event.accepted)
                    spatial.handle(event)
                if (!event.accepted)
                    scrollKeys.handle(event)
            }

            property int scopedDown: 0
        }

        Item {
            id: readerOwner
            objectName: "readerOwner"
            x: 430
            y: 0
            width: 240
            height: 100
            focus: true
            property int pageTurns: 0
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    pageTurns += 1
                    event.accepted = true
                }
            }
        }

        Item {
            id: pureScrollRegion
            objectName: "pureScrollRegion"
            x: 430
            width: 240
            height: 250
            focus: true

            Flickable {
                id: pureFlick
                objectName: "pureFlick"
                width: parent.width
                height: 150
                clip: true
                contentWidth: width
                contentHeight: 900
                focus: true

                Rectangle {
                    width: pureFlick.width
                    height: pureFlick.contentHeight
                    color: "#18202b"
                }

                Keys.onPressed: function(event) {
                    if (!event.accepted)
                        pureScrollKeys.handle(event)
                }
            }

            Colosseum.KeyboardSpatialNavigator {
                id: pureSpatial
                root: pureScrollRegion
            }
            Colosseum.KeyboardScrollController {
                id: pureScrollKeys
                flick: pureFlick
                lineStep: 40
            }
            Keys.onPressed: function(event) {
                if (!event.accepted)
                    pureSpatial.handle(event)
                if (!event.accepted)
                    pureScrollKeys.handle(event)
            }
        }

        ListView {
            id: virtualizedList
            objectName: "virtualizedList"
            x: 430
            y: 280
            width: 240
            height: 150
            clip: true
            focus: true
            model: 24
            delegate: Colosseum.KeyboardAction {
                objectName: "virtualRow" + index
                width: virtualizedList.width
                height: 42
                pointerEnabled: false
            }
            Keys.onPressed: function(event) {
                if (!event.accepted)
                    collectionKeys.handle(event)
            }
            Colosseum.KeyboardCollectionController {
                id: collectionKeys
                view: virtualizedList
                orientation: "vertical"
                count: virtualizedList.count
                currentIndex: virtualizedList.currentIndex
            }
        }

        GridView {
            id: virtualizedGrid
            objectName: "virtualizedGrid"
            x: 430
            y: 440
            width: 240
            height: 70
            cellWidth: 80
            cellHeight: 40
            clip: true
            focus: true
            model: 24
            delegate: Colosseum.KeyboardAction {
                objectName: "virtualCell" + index
                width: virtualizedGrid.cellWidth
                height: virtualizedGrid.cellHeight
                pointerEnabled: false
            }
            Keys.onPressed: function(event) {
                if (!event.accepted)
                    gridKeys.handle(event)
            }
            Colosseum.KeyboardCollectionController {
                id: gridKeys
                view: virtualizedGrid
                orientation: "grid"
                columns: 3
                count: virtualizedGrid.count
                currentIndex: virtualizedGrid.currentIndex
            }
        }
    }

    SignalSpy { id: boundarySpy; target: spatial; signalName: "boundaryRequested" }

    function row(index) {
        return rows.itemAt(index)
    }

    function resetMain() {
        mainFlick.contentY = 0
        unrelatedChrome.visible = true
        editable.forceActiveFocus(Qt.OtherFocusReason)
        row(0).forceActiveFocus(Qt.OtherFocusReason)
        wait(20)
        verify(row(0).activeFocus)
    }

    function init() {
        testWindow.requestActivate()
        boundarySpy.clear()
        scrollRegion.scopedDown = 0
        resetMain()
        pureFlick.contentY = 0
        virtualizedList.currentIndex = 0
        virtualizedList.positionViewAtBeginning()
        wait(20)
    }

    function test_visible_target_moves_without_scroll() {
        var before = mainFlick.contentY
        keyClick(Qt.Key_Down)
        verify(row(1).activeFocus)
        compare(mainFlick.contentY, before)
    }

    function test_offscreen_next_reveals_and_receives_focus() {
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        var before = mainFlick.contentY
        keyClick(Qt.Key_Down)
        wait(20)
        verify(mainFlick.contentY > before)
        verify(row(3).activeFocus)
        verify(!unrelatedChrome.activeFocus)
    }

    function test_repeated_down_and_symmetric_up_cross_viewports() {
        for (var i = 1; i < 10; ++i) {
            var beforeDown = mainFlick.contentY
            keyClick(Qt.Key_Down)
            wait(5)
            if (!row(i).activeFocus) {
                // An over-budget target consumes this key as a bounded reveal;
                // the next key may land once the target is visible.
                verify(row(i - 1).activeFocus || mainFlick.activeFocus)
                keyClick(Qt.Key_Down)
                wait(5)
            }
            verify(row(i).activeFocus)
            if (i >= 3)
                verify(mainFlick.contentY >= beforeDown)
        }
        for (var j = 8; j >= 0; --j) {
            var beforeUp = mainFlick.contentY
            keyClick(Qt.Key_Up)
            wait(5)
            if (!row(j).activeFocus) {
                verify(row(j + 1).activeFocus || mainFlick.activeFocus)
                keyClick(Qt.Key_Up)
                wait(5)
            }
            verify(row(j).activeFocus)
            if (j <= 6)
                verify(mainFlick.contentY <= beforeUp)
        }
        compare(mainFlick.contentY, 0)
    }

    function test_pure_scroll_without_targets_keeps_viewport_focus() {
        pureFlick.forceActiveFocus(Qt.OtherFocusReason)
        verify(pureFlick.activeFocus)
        keyClick(Qt.Key_Down)
        compare(pureFlick.contentY, 37.5)
        verify(pureFlick.activeFocus)
        keyClick(Qt.Key_PageDown)
        verify(pureFlick.contentY > 37.5)
        verify(pureFlick.activeFocus)
    }

    function test_arrow_scrolling_opt_out_and_modifiers_preserve_owner() {
        pureScrollKeys.arrowScrolling = false
        pureFlick.forceActiveFocus(Qt.OtherFocusReason)
        verify(pureFlick.activeFocus)
        keyClick(Qt.Key_Down)
        compare(pureFlick.contentY, 0)
        verify(pureFlick.activeFocus)
        pureScrollKeys.arrowScrolling = true
        keyClick(Qt.Key_Down, Qt.ControlModifier)
        compare(pureFlick.contentY, 0)
        verify(pureFlick.activeFocus)
    }

    function test_true_boundary_leaves_key_unaccepted_at_scroll_end() {
        unrelatedChrome.visible = false
        editable.visible = false
        textEditor.visible = false
        slider.visible = false
        scopedOwner.visible = false
        row(11).forceActiveFocus(Qt.OtherFocusReason)
        mainFlick.contentY = mainFlick.contentHeight - mainFlick.height
        wait(20)
        verify(row(11).activeFocus)
        keyClick(Qt.Key_Down)
        verify(row(11).activeFocus)
        compare(boundarySpy.count, 1)
        compare(boundarySpy.signalArguments[0][0], Qt.Key_Down)
    }

    function test_hidden_disabled_and_clipped_targets_are_excluded() {
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        wait(20)
        verify(row(3).activeFocus)
        verify(!hiddenAction.activeFocus)
        verify(!disabledAction.activeFocus)
        verify(!clippedAction.activeFocus)
    }

    function test_editable_and_scoped_child_handlers_precede_parent() {
        editable.forceActiveFocus(Qt.OtherFocusReason)
        var before = mainFlick.contentY
        keyClick(Qt.Key_Down)
        verify(editable.activeFocus)
        compare(mainFlick.contentY, before)

        scopedOwner.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(scrollRegion.scopedDown, 1)
        verify(scopedOwner.activeFocus)
        compare(mainFlick.contentY, before)
    }

    function test_native_text_edit_and_slider_keep_scoped_keys() {
        textEditor.forceActiveFocus(Qt.OtherFocusReason)
        var before = mainFlick.contentY
        keyClick(Qt.Key_Down)
        verify(textEditor.activeFocus)
        compare(mainFlick.contentY, before)

        slider.value = 5
        slider.forceActiveFocus(Qt.OtherFocusReason)
        var oldValue = slider.value
        keyClick(Qt.Key_Right)
        verify(slider.activeFocus)
        verify(slider.value > oldValue)
        compare(mainFlick.contentY, before)
    }

    function test_reader_owner_retains_page_turn_precedence() {
        readerOwner.pageTurns = 0
        readerOwner.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(readerOwner.pageTurns, 1)
        verify(readerOwner.activeFocus)
        keyClick(Qt.Key_Up)
        compare(readerOwner.pageTurns, 2)
        verify(readerOwner.activeFocus)
    }

    function test_virtualized_collection_route_realizes_and_focuses_item() {
        virtualizedList.forceActiveFocus(Qt.OtherFocusReason)
        for (var i = 0; i < 8; ++i)
            keyClick(Qt.Key_Down)
        wait(20)
        compare(virtualizedList.currentIndex, 8)
        verify(virtualizedList.activeFocus)
        verify(virtualizedList.itemAtIndex(8) !== null)
        verify(virtualizedList.itemAtIndex(8).objectName === "virtualRow8")
    }

    function test_virtualized_grid_route_realizes_and_focuses_item() {
        virtualizedGrid.currentIndex = 0
        virtualizedGrid.positionViewAtBeginning()
        virtualizedGrid.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(virtualizedGrid.currentIndex, 3)
        verify(virtualizedGrid.activeFocus)
        verify(virtualizedGrid.itemAtIndex(3) !== null)
        verify(virtualizedGrid.itemAtIndex(3).objectName === "virtualCell3")
    }

    // Astra repair regressions. These keep the review-characterized defects
    // executable while the implementation is repaired.
    function test_external_entry_has_one_step_reveal_budget() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item {'
            + 'width: 300; height: 300; Item { id: outside; width: 20; height: 20 }'
            + 'Flickable { id: fl; width: 300; height: 300; contentWidth: 300; contentHeight: 2200; clip: true;'
            + 'Item { width: 300; height: 2200; C.KeyboardAction { id: target; y: 1800; width: 100; height: 40; pointerEnabled: false } } }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: fl } property alias outside: outside;'
            + 'property alias target: target; property alias flick: fl; property alias navItem: nav }', scrollRegion)
        fixture.outside.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.navItem.moveFrom(fixture.outside, Qt.Key_Down))
        verify(!fixture.target.activeFocus)
        verify(fixture.flick.contentY >= 0 && fixture.flick.contentY <= 72)
        fixture.destroy()
    }

    function test_two_stage_reveal_never_spends_two_directional_budgets() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item {'
            + 'width: 300; height: 300; Flickable { id: fl; width: 300; height: 300; contentWidth: 300; contentHeight: 500; clip: true;'
            + 'Item { width: 300; height: 500; C.KeyboardAction { id: src; y: 20; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: target; y: 350; width: 100; height: 40; pointerEnabled: false } } }'
            + 'C.KeyboardScrollController { id: scroll; flick: fl; lineStep: 72 }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: fl } property alias src: src; property alias target: target;'
            + 'property alias flick: fl; property alias navItem: nav }', scrollRegion)
        fixture.src.forceActiveFocus(Qt.OtherFocusReason)
        verify(fixture.navItem.moveFrom(fixture.src, Qt.Key_Down))
        compare(fixture.flick.contentY, 72)
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }

    function test_arrow_scrolling_false_blocks_fallback_reveal_and_focus() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item {'
            + 'width: 300; height: 300; Flickable { id: fl; width: 300; height: 300; contentWidth: 300; contentHeight: 500; clip: true;'
            + 'Item { width: 300; height: 500; C.KeyboardAction { id: src; y: 20; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: target; y: 350; width: 100; height: 40; pointerEnabled: false } } }'
            + 'C.KeyboardScrollController { id: scroll; flick: fl; arrowScrolling: false }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: fl } property alias src: src; property alias target: target;'
            + 'property alias flick: fl; property alias navItem: nav }', scrollRegion)
        fixture.src.forceActiveFocus(Qt.OtherFocusReason)
        verify(!fixture.navItem.moveFrom(fixture.src, Qt.Key_Down))
        compare(fixture.flick.contentY, 0)
        verify(!fixture.target.activeFocus)
        fixture.destroy()
    }

    function test_bottom_content_does_not_export_down_to_visible_chrome() {
        mainFlick.contentY = mainFlick.contentHeight - mainFlick.height
        row(11).forceActiveFocus(Qt.OtherFocusReason)
        wait(20)
        verify(row(11).activeFocus)
        var before = mainFlick.contentY
        keyClick(Qt.Key_Down)
        verify(row(11).activeFocus)
        verify(!unrelatedChrome.activeFocus)
        compare(mainFlick.contentY, before)
    }

    function test_nested_optout_cannot_be_revealed_by_outer_owner() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Flickable {'
            + 'id: outer; width: 300; height: 300; contentWidth: 300; contentHeight: 700; clip: true;'
            + 'property alias innerFlick: inner; property alias source: source;'
            + 'Flickable { id: inner; width: 250; height: 200; contentWidth: 250; contentHeight: 500; clip: true;'
            + 'C.KeyboardAction { id: source; y: 20; width: 100; height: 30; pointerEnabled: false }'
            + 'C.KeyboardAction { y: 220; width: 100; height: 30; pointerEnabled: false } }'
            + 'C.KeyboardScrollController { flick: inner; arrowScrolling: false }'
            + 'C.KeyboardScrollController { flick: outer }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: outer } property alias navigator: nav }', testWindow.contentItem)
        testWindow.requestActivate()
        fixture.source.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        verify(fixture.source.activeFocus)
        fixture.navigator.moveFrom(fixture.source, Qt.Key_Down)
        compare(fixture.innerFlick.contentY, 0)
        fixture.destroy()
    }

    function test_nested_optout_allows_outer_only_continuation() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Flickable {'
            + 'id: outer; width: 300; height: 200; contentWidth: 300; contentHeight: 600; clip: true;'
            + 'property alias innerFlick: inner; property alias source: source; property alias outerTarget: outerTarget;'
            + 'Flickable { id: inner; width: 250; height: 120; contentWidth: 250; contentHeight: 500; clip: true;'
            + 'C.KeyboardAction { id: source; y: 20; width: 100; height: 30; pointerEnabled: false } }'
            + 'C.KeyboardAction { id: outerTarget; y: 340; width: 100; height: 30; pointerEnabled: false }'
            + 'C.KeyboardScrollController { flick: inner; arrowScrolling: false }'
            + 'C.KeyboardScrollController { flick: outer; lineStep: 50 }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: outer } property alias navigator: nav }', testWindow.contentItem)
        testWindow.requestActivate()
        fixture.source.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        verify(fixture.source.activeFocus)
        verify(fixture.navigator.moveFrom(fixture.source, Qt.Key_Down))
        verify(fixture.innerFlick.contentY === 0)
        verify(fixture.contentY > 0)
        fixture.destroy()
    }

    function test_scale_uses_same_units_as_owner_budget() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 600; height: 450;'
            + 'Flickable { id: fl; x: 80; y: 20; width: 300; height: 300; scale: 0.5; contentWidth: 300; contentHeight: 700; clip: true;'
            + 'C.KeyboardAction { id: source; x: 20; y: 20; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: target; x: 20; y: 350; width: 100; height: 40; pointerEnabled: false } }'
            + 'C.KeyboardScrollController { flick: fl; lineStep: 72 }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } property alias source: source; property alias flick: fl; property alias navigator: nav }', testWindow.contentItem)
        testWindow.requestActivate()
        fixture.source.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        verify(fixture.source.activeFocus)
        fixture.navigator.moveFrom(fixture.source, Qt.Key_Down)
        verify(fixture.flick.contentY <= 72)
        fixture.destroy()
    }

    function test_rotated_owner_rejection_is_without_writes() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 600; height: 450;'
            + 'Flickable { id: fl; x: 80; y: 20; width: 300; height: 300; rotation: 10; contentWidth: 300; contentHeight: 700; clip: true;'
            + 'C.KeyboardAction { id: source; x: 20; y: 20; width: 100; height: 40; pointerEnabled: false }'
            + 'C.KeyboardAction { id: target; x: 20; y: 350; width: 100; height: 40; pointerEnabled: false } }'
            + 'C.KeyboardScrollController { flick: fl; lineStep: 72 }'
            + 'C.KeyboardSpatialNavigator { id: nav; root: parent } property alias source: source; property alias flick: fl; property alias navigator: nav }', testWindow.contentItem)
        testWindow.requestActivate()
        fixture.source.forceActiveFocus(Qt.OtherFocusReason)
        wait(10)
        verify(fixture.source.activeFocus)
        fixture.navigator.moveFrom(fixture.source, Qt.Key_Down)
        compare(fixture.flick.contentY, 0)
        fixture.destroy()
    }
}
