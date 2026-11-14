import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../qml" as App
import "../../qml/reader2" as Reader2
import "../../qml/collections" as CollectionsUi

// Android has no hover: KeyboardAction shows its existing tooltip on press-and-hold.
// Desktop keeps the hover tooltip. No layout or label changes on either platform.
TestCase {
    id: testCase
    name: "AndroidLongPressTooltip"
    property int mouseOwnerClicks: 0
    property int scrollOwnerClicks: 0
    property int desktopHelperClicks: 0
    property int desktopBaselineClicks: 0
    property int buttonClicks: 0
    property int buttonActionTriggers: 0
    property int buttonActionToggles: 0
    property int buttonPressSignals: 0
    property int buttonReleaseSignals: 0
    property int scrollButtonClicks: 0
    property int portalArrowTriggers: 0
    property int desktopButtonClicks: 0
    property int desktopBaselineButtonClicks: 0
    property int watchedTileDetails: 0
    when: windowShown
    Window {
        id: testWindow
        width: 1280; height: 720; visible: true
        App.TooltipStyle {
            id: tooltipStyle
            backdrop: testWindow.contentItem
        }
        Reader2.ReaderChrome {
            id: readerChrome
            anchors.fill: parent
            title: "Test book"
            author: "Test author"
            chapterLabel: "Chapter 1"
            shellWindowed: true
        }
        App.KeyboardAction {
            id: action
            x: 18; y: 500
            width: 140; height: 40
            accessibleName: "Open details"
            tooltipText: "Open details"
            longPressTooltipEnabled: true
        }
        QtObject {
            id: sourcePicker
            property string smartPlayQuality: "off"
            function setSmartPickEnabled(enabled) {}
        }
        App.SmartPickToggle {
            id: smartPick
            x: 216; y: 180
            visible: true
            sourcePicker: sourcePicker
        }
        Action {
            id: buttonTooltipAction
            checkable: true
            onTriggered: testCase.buttonActionTriggers += 1
            onToggled: testCase.buttonActionToggles += 1
        }
        Button {
            id: buttonTooltipControl
            x: 600; y: 500; width: 160; height: 48
            text: qsTr("Button action test")
            action: buttonTooltipAction
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            ToolTip.visible: buttonTooltipHelper.longPressTooltipEnabled
                ? buttonTooltipHelper.active : buttonTooltipControl.hovered
            ToolTip.delay: buttonTooltipHelper.longPressTooltipEnabled ? 0 : 350
            ToolTip.text: qsTr("Button action help")
            onClicked: testCase.buttonClicks += 1
            onPressed: testCase.buttonPressSignals += 1
            onReleased: testCase.buttonReleaseSignals += 1
        }
        App.TouchTooltip {
            id: buttonTooltipHelper
            buttonTarget: buttonTooltipControl
        }
        Flickable {
            id: buttonTouchScrollFixture
            x: 900; y: 500; width: 190; height: 180
            contentWidth: width; contentHeight: 600
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Button {
                id: scrollButtonControl
                x: 12; y: 90; width: 150; height: 54
                text: qsTr("Scroll button")
                onClicked: testCase.scrollButtonClicks += 1
            }
        }
        App.TouchTooltip {
            id: scrollButtonTooltipHelper
            buttonTarget: scrollButtonControl
        }
        Flickable {
            id: passiveLabelScrollFixture
            x: 900; y: 250; width: 190; height: 120
            contentWidth: width; contentHeight: 600
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: passiveLabelOwner
                x: 12; y: 70; width: 160; height: 44
                text: qsTr("Informational status")
                color: "white"
                verticalAlignment: Text.AlignVCenter
                ToolTip.text: qsTr("Informational help")
                ToolTip.delay: passiveLabelTooltip.longPressTooltipEnabled ? 0 : 350
                ToolTip.visible: passiveLabelTooltip.longPressTooltipEnabled
                    ? passiveLabelTooltip.active : passiveLabelHover.hovered
                HoverHandler { id: passiveLabelHover }
            }
        }
        App.TouchTooltip {
            id: passiveLabelTooltip
            owner: passiveLabelOwner
            tapOwner: true
            longPressTooltipEnabled: true
        }
        App.DCAUPortalArrow {
            id: portalArrow
            x: 420; y: 500
            tooltipText: qsTr("Next portal")
            onTriggered: testCase.portalArrowTriggers += 1
        }
        Flickable {
            id: watchedTileScrollFixture
            x: 1100; y: 400; width: 170; height: 270
            contentWidth: width; contentHeight: 650
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            App.ContinueTile {
                id: watchedTile
                x: 5; y: 20
                entry: ({ id: "tooltip-test", kind: "video", title: "Test title", watched: true })
                onDetailRequested: testCase.watchedTileDetails += 1
            }
        }
        Button {
            id: desktopButtonControl
            x: 600; y: 620; width: 150; height: 44
            text: qsTr("Desktop helper")
            focusPolicy: Qt.StrongFocus
            onClicked: testCase.desktopButtonClicks += 1
        }
        App.TouchTooltip {
            id: desktopButtonHelper
            buttonTarget: desktopButtonControl
            longPressTooltipEnabled: false
        }
        Button {
            id: desktopBaselineButton
            x: 800; y: 620; width: 150; height: 44
            text: qsTr("Desktop baseline")
            focusPolicy: Qt.StrongFocus
            onClicked: testCase.desktopBaselineButtonClicks += 1
        }
        SignalSpy { id: actionTriggers; target: action; signalName: "triggered" }
        MouseArea {
            id: mouseTooltipOwner
            x: 28; y: 240; width: 140; height: 42
            hoverEnabled: true
            pressAndHoldInterval: 720
            ToolTip.text: qsTr("Mouse owner hint")
            ToolTip.delay: mouseTooltipHelper.longPressTooltipEnabled ? 0 : 350
            ToolTip.visible: (!mouseTooltipHelper.longPressTooltipEnabled && containsMouse)
                || mouseTooltipHelper.active
            onClicked: testCase.mouseOwnerClicks += 1
        }
        App.TouchTooltip {
            id: mouseTooltipHelper
            target: mouseTooltipOwner
            longPressTooltipEnabled: true
        }

        Flickable {
            id: touchScrollFixture
            x: 230; y: 240; width: 180; height: 100
            contentWidth: width; contentHeight: 500
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            MouseArea {
                id: scrollOwner
                width: touchScrollFixture.width
                height: touchScrollFixture.contentHeight
                onClicked: testCase.scrollOwnerClicks += 1
            }
        }
        App.TouchTooltip {
            id: scrollTooltipHelper
            target: scrollOwner
            longPressTooltipEnabled: true
        }

        MouseArea {
            id: desktopHelperOwner
            x: 20; y: 620; width: 140; height: 42
            pressAndHoldInterval: 720
            onClicked: testCase.desktopHelperClicks += 1
        }
        App.TouchTooltip {
            id: desktopTooltipHelper
            target: desktopHelperOwner
            longPressTooltipEnabled: false
        }
        CollectionsUi.AddToCollectionsButton {
            id: unavailableCollectionsButton
            x: 440; y: 620; width: 100; height: 42
            iconOnly: true
        }
        MouseArea {
            id: desktopBaselineOwner
            x: 220; y: 620; width: 140; height: 42
            pressAndHoldInterval: 720
            onClicked: testCase.desktopBaselineClicks += 1
        }
    }
    function init() {
        testWindow.requestActivate()
        mouseMove(testWindow.contentItem, 5, 300)
        actionTriggers.clear()
        action._longPressTooltip = false
        action.longPressTooltipEnabled = true
        action.tooltipHovered = false
        readerChrome.closeAnyPanel()
        readerChrome.revealState = ({ shown: false, lastActive: 0, pinned: false, frozen: false })
        readerChrome.awake = false
        mouseOwnerClicks = 0
        scrollOwnerClicks = 0
        desktopHelperClicks = 0
        desktopBaselineClicks = 0
        buttonClicks = 0
        buttonActionTriggers = 0
        buttonActionToggles = 0
        buttonPressSignals = 0
        buttonReleaseSignals = 0
        scrollButtonClicks = 0
        portalArrowTriggers = 0
        desktopButtonClicks = 0
        desktopBaselineButtonClicks = 0
        watchedTileDetails = 0
        watchedTileScrollFixture.contentY = 0
        const watchedHelper = findChild(watchedTile, "continueWatchedTooltip")
        watchedHelper.longPressTooltipEnabled = true
        watchedHelper.reset()
        buttonTooltipControl.enabled = true
        buttonTooltipControl.visible = true
        buttonTooltipAction.checked = false
        buttonTouchScrollFixture.contentY = 0
        passiveLabelScrollFixture.contentY = 0
        passiveLabelTooltip.reset()
        mouseTooltipHelper.longPressTooltipEnabled = false
        mouseTooltipHelper.longPressTooltipEnabled = true
        scrollTooltipHelper.longPressTooltipEnabled = false
        scrollTooltipHelper.longPressTooltipEnabled = true
        desktopTooltipHelper.longPressTooltipEnabled = false
        buttonTooltipHelper.longPressTooltipEnabled = false
        buttonTooltipHelper.longPressTooltipEnabled = true
        scrollButtonTooltipHelper.longPressTooltipEnabled = false
        scrollButtonTooltipHelper.longPressTooltipEnabled = true
        portalArrow.longPressTooltipEnabled = false
        portalArrow.longPressTooltipEnabled = true
        mouseTooltipOwner.enabled = true
        mouseTooltipOwner.visible = true
        scrollOwner.enabled = true
        scrollOwner.visible = true
        touchScrollFixture.contentY = 0
        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
    }

    function test_android_hover_alone_shows_nothing() {
        action.tooltipHovered = true
        compare(action.tooltipRequested, false)
    }
    function test_android_long_press_shows_tooltip_without_activation() {
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(600)
        verify(action._longPressTooltip, "the hint should appear ~0.4 s into the hold, finger still down")
        compare(action.tooltipRequested, true)
        compare(action.ToolTip.delay, 0)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 0)
        compare(action.tooltipRequested, true)   // stays up after release, hides ~1.5 s later
        wait(1700)
        compare(action.tooltipRequested, false)
    }
    function test_android_short_tap_still_activates() {
        wait(700)
        const p = action.mapToItem(testWindow.contentItem, 70, 20)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(actionTriggers.count, 1)
        compare(action._longPressTooltip, false)
    }
    function test_android_button_short_tap_uses_native_action_once() {
        const p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                                  buttonTooltipControl.width / 2,
                                                  buttonTooltipControl.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(80)
        compare(buttonTooltipControl.down, true)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(50)
        compare(buttonClicks, 1)
        compare(buttonActionTriggers, 1)
        compare(buttonActionToggles, 1)
        compare(buttonTooltipControl.checked, true)
        compare(buttonPressSignals, 1)
        compare(buttonReleaseSignals, 1)
        compare(buttonTooltipControl.activeFocus, true)
        compare(buttonTooltipHelper.active, false)
    }

    function test_android_button_long_hold_shows_hint_until_release_timeout() {
        const p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                                  buttonTooltipControl.width / 2,
                                                  buttonTooltipControl.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(300)
        compare(buttonTooltipHelper.active, false)
        compare(buttonTooltipControl.ToolTip.visible, false)
        tryCompare(buttonTooltipHelper, "active", true, 250)
        tryCompare(buttonTooltipControl.ToolTip, "visible", true, 100)
        compare(buttonClicks, 0)
        compare(buttonActionTriggers, 0)
        compare(buttonActionToggles, 0)
        compare(buttonTooltipControl.checked, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1200)
        compare(buttonTooltipHelper.active, true)
        tryCompare(buttonTooltipHelper, "active", false, 500)
        tryCompare(buttonTooltipControl.ToolTip, "visible", false, 500)
        compare(buttonClicks, 0)
        compare(buttonActionTriggers, 0)
    }

    function test_android_button_drag_out_cancels_hint_and_activation() {
        const p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                                  buttonTooltipControl.width / 2,
                                                  buttonTooltipControl.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        sequence.move(0, testWindow.contentItem, p.x + 90, p.y + 80).commit()
        wait(40)
        sequence.release(0, testWindow.contentItem, p.x + 90, p.y + 80).commit()
        wait(40)
        compare(buttonTooltipHelper.active, false)
        compare(buttonClicks, 0)
        compare(buttonActionTriggers, 0)
        compare(buttonActionToggles, 0)
        compare(buttonTooltipControl.checked, false)
    }

    function test_android_button_disabled_or_hidden_clears_hint_without_activation() {
        let p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                               buttonTooltipControl.width / 2,
                                               buttonTooltipControl.height / 2)
        let sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(450)
        tryCompare(buttonTooltipHelper, "active", true)
        buttonTooltipControl.enabled = false
        compare(buttonTooltipHelper.active, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        buttonTooltipControl.enabled = true

        p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                           buttonTooltipControl.width / 2,
                                           buttonTooltipControl.height / 2)
        sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(450)
        tryCompare(buttonTooltipHelper, "active", true)
        buttonTooltipControl.visible = false
        compare(buttonTooltipHelper.active, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        buttonTooltipControl.visible = true
        compare(buttonClicks, 0)
        compare(buttonActionTriggers, 0)
    }

    function test_android_button_adapter_preserves_touch_flickable_scroll() {
        const p = scrollButtonControl.mapToItem(testWindow.contentItem, 50, 28)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        for (let step = 1; step <= 8; ++step) {
            sequence.move(0, testWindow.contentItem, p.x, p.y - step * 12).commit()
            wait(20)
        }
        sequence.release(0, testWindow.contentItem, p.x, p.y - 96).commit()
        wait(150)
        verify(buttonTouchScrollFixture.contentY > 10,
               "touch scrolling should remain available over the button")
        compare(scrollButtonClicks, 0)
        compare(scrollButtonTooltipHelper.active, false)
    }

    function test_android_inert_label_long_press_and_touch_scroll() {
        const p = passiveLabelOwner.mapToItem(testWindow.contentItem,
                                              passiveLabelOwner.width / 2,
                                              passiveLabelOwner.height / 2)
        let sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(500)
        tryCompare(passiveLabelTooltip, "active", true)
        compare(passiveLabelOwner.ToolTip.delay, 0)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(50)
        compare(passiveLabelTooltip.active, true)

        passiveLabelScrollFixture.contentY = 0
        const start = passiveLabelOwner.mapToItem(testWindow.contentItem,
                                                   passiveLabelOwner.width / 2,
                                                   passiveLabelOwner.height / 2)
        sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, start.x, start.y).commit()
        wait(30)
        for (let step = 1; step <= 8; ++step) {
            sequence.move(0, testWindow.contentItem, start.x, start.y - step * 12).commit()
            wait(20)
        }
        sequence.release(0, testWindow.contentItem, start.x, start.y - 96).commit()
        wait(150)
        verify(passiveLabelScrollFixture.contentY > 10,
               "a long-press observer on an informational label must not block Flickable scrolling")
        compare(passiveLabelTooltip.active, false)
    }

    function test_android_watched_badge_hold_does_not_open_tile() {
        const helper = findChild(watchedTile, "continueWatchedTooltip")
        const input = helper.target
        const p = input.mapToItem(testWindow.contentItem, input.width / 2, input.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(300)
        compare(helper.active, false)
        tryCompare(helper, "active", true, 250)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1200)
        compare(helper.active, true)
        compare(watchedTileDetails, 0)
        tryCompare(helper, "active", false, 500)
    }

    function test_android_watched_badge_short_tap_and_rest_of_tile_keep_activation() {
        const helper = findChild(watchedTile, "continueWatchedTooltip")
        const input = helper.target
        let p = input.mapToItem(testWindow.contentItem, input.width / 2, input.height / 2)
        let sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(50)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        tryCompare(testCase, "watchedTileDetails", 1)
        compare(helper.active, false)

        p = watchedTile.mapToItem(testWindow.contentItem, 12, watchedTile.height - 30)
        sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(600)
        compare(helper.active, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        tryCompare(testCase, "watchedTileDetails", 2)
    }

    function test_android_watched_badge_preserves_touch_scroll() {
        const helper = findChild(watchedTile, "continueWatchedTooltip")
        const input = helper.target
        const p = input.mapToItem(testWindow.contentItem, input.width / 2, input.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        for (let step = 1; step <= 8; ++step) {
            sequence.move(0, testWindow.contentItem, p.x, p.y - step * 12).commit()
            wait(20)
        }
        sequence.release(0, testWindow.contentItem, p.x, p.y - 96).commit()
        wait(100)
        verify(watchedTileScrollFixture.contentY > 10)
        compare(helper.active, false)
        compare(watchedTileDetails, 0)
    }

    function test_android_unavailable_collections_uses_shared_inert_owner() {
        compare(unavailableCollectionsButton.available, false)
        let helper = null
        let keyboard = null
        for (const child of unavailableCollectionsButton.children) {
            if (child.tapOwner === true && child.owner === unavailableCollectionsButton)
                helper = child
            if (child.objectName === unavailableCollectionsButton.objectName + "Action")
                keyboard = child
        }
        verify(helper !== null)
        verify(keyboard !== null)
        helper.longPressTooltipEnabled = true
        keyboard.longPressTooltipEnabled = true
        const p = unavailableCollectionsButton.mapToItem(testWindow.contentItem, 50, 21)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(300)
        compare(helper.active, false)
        tryCompare(helper, "active", true, 250)
        tryCompare(unavailableCollectionsButton.ToolTip, "visible", true, 100)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1200)
        compare(helper.active, true)
        tryCompare(helper, "active", false, 500)
        helper.longPressTooltipEnabled = false
        keyboard.longPressTooltipEnabled = false
    }

    function test_android_dca_u_tap_handler_uses_shared_long_press_state() {
        const p = portalArrow.mapToItem(testWindow.contentItem,
                                        portalArrow.width / 2,
                                        portalArrow.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(300)
        compare(portalArrow.ToolTip.visible, false)
        tryCompare(portalArrow.ToolTip, "visible", true, 250)
        compare(portalArrow.ToolTip.delay, 0)
        compare(portalArrowTriggers, 0)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(1200)
        compare(portalArrow.ToolTip.visible, true)
        tryCompare(portalArrow.ToolTip, "visible", false, 500)
        compare(portalArrowTriggers, 0)
    }

    function test_android_dca_u_short_tap_keeps_existing_trigger() {
        const p = portalArrow.mapToItem(testWindow.contentItem,
                                        portalArrow.width / 2,
                                        portalArrow.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(50)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        compare(portalArrowTriggers, 1)
    }

    function test_desktop_button_hover_delay_and_click_are_unchanged() {
        buttonTooltipHelper.longPressTooltipEnabled = false
        compare(buttonTooltipControl.ToolTip.delay, 350)
        const p = buttonTooltipControl.mapToItem(testWindow.contentItem,
                                                  buttonTooltipControl.width / 2,
                                                  buttonTooltipControl.height / 2)
        mouseMove(testWindow.contentItem, 5, 300)
        mouseMove(testWindow.contentItem, p.x, p.y)
        tryCompare(buttonTooltipControl, "hovered", true)
        const popup = buttonTooltipControl.ToolTip.toolTip
        verify(popup !== null)
        wait(180)
        compare(popup.visible, false)
        tryCompare(popup, "visible", true, 500)
        mouseMove(testWindow.contentItem, 5, 300)
        tryCompare(popup, "visible", false)
        mouseClick(buttonTooltipControl, buttonTooltipControl.width / 2,
                   buttonTooltipControl.height / 2)
        compare(buttonClicks, 1)
        compare(buttonActionTriggers, 1)
        compare(buttonActionToggles, 1)
        compare(buttonTooltipHelper.active, false)
    }

    function test_desktop_button_short_and_long_mouse_match_native_baseline() {
        desktopButtonHelper.longPressTooltipEnabled = false
        const helperPoint = desktopButtonControl.mapToItem(testWindow.contentItem,
                                                           desktopButtonControl.width / 2,
                                                           desktopButtonControl.height / 2)
        const baselinePoint = desktopBaselineButton.mapToItem(testWindow.contentItem,
                                                              desktopBaselineButton.width / 2,
                                                              desktopBaselineButton.height / 2)
        mouseClick(testWindow.contentItem, helperPoint.x, helperPoint.y)
        mouseClick(testWindow.contentItem, baselinePoint.x, baselinePoint.y)
        compare(desktopButtonClicks, 1)
        compare(desktopBaselineButtonClicks, 1)

        desktopButtonClicks = 0
        desktopBaselineButtonClicks = 0
        mousePress(testWindow.contentItem, helperPoint.x, helperPoint.y)
        wait(900)
        mouseRelease(testWindow.contentItem, helperPoint.x, helperPoint.y)
        mousePress(testWindow.contentItem, baselinePoint.x, baselinePoint.y)
        wait(900)
        mouseRelease(testWindow.contentItem, baselinePoint.x, baselinePoint.y)
        compare(desktopButtonClicks, desktopBaselineButtonClicks)
    }

    function test_desktop_keeps_hover_tooltip() {
        action.longPressTooltipEnabled = false
        action.tooltipHovered = true
        compare(action.ToolTip.text, "Open details")
        compare(action.tooltipRequested, true)
    }

    function test_mouse_area_touch_long_press_suppresses_click_and_times_from_release() {
        compare(mouseTooltipOwner.pressAndHoldInterval, 400)
        compare(mouseTooltipOwner.ToolTip.delay, 0)
        const p = mouseTooltipOwner.mapToItem(testWindow.contentItem,
                                               mouseTooltipOwner.width / 2,
                                               mouseTooltipOwner.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        tryCompare(mouseTooltipOwner, "pressed", true)
        wait(300)
        compare(mouseTooltipHelper.active, false)
        compare(mouseTooltipOwner.ToolTip.visible, false)
        wait(160)
        tryCompare(mouseTooltipHelper, "active", true, 500)
        compare(mouseOwnerClicks, 0)
        const popup = mouseTooltipOwner.ToolTip.toolTip
        verify(popup !== null)
        tryCompare(popup, "visible", true)

        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(mouseOwnerClicks, 0)
        compare(mouseTooltipHelper.active, true)
        wait(1200)
        compare(mouseTooltipHelper.active, true)
        tryCompare(mouseTooltipHelper, "active", false, 500)
    }

    function test_mouse_area_touch_short_tap_still_activates_once() {
        compare(mouseTooltipOwner.pressAndHoldInterval, 400)
        const p = mouseTooltipOwner.mapToItem(testWindow.contentItem,
                                               mouseTooltipOwner.width / 2,
                                               mouseTooltipOwner.height / 2)
        const sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(40)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        wait(30)
        compare(mouseOwnerClicks, 1)
        compare(mouseTooltipHelper.active, false)
    }

    function test_mouse_area_flick_drag_cancels_without_hint_or_click() {
        compare(scrollOwner.pressAndHoldInterval, 400)
        const p = scrollOwner.mapToItem(testWindow.contentItem, 24, 24)
        mousePress(testWindow.contentItem, p.x, p.y)
        for (let step = 1; step <= 5; ++step)
            mouseMove(testWindow.contentItem, p.x, p.y - 16 * step, 20)
        wait(450)
        compare(scrollTooltipHelper.active, false)
        mouseRelease(testWindow.contentItem, p.x, p.y - 80)
        wait(40)
        compare(scrollOwnerClicks, 0)
        verify(touchScrollFixture.contentY > 0)
    }

    function test_mouse_area_target_disable_and_hide_clear_hint() {
        const p = mouseTooltipOwner.mapToItem(testWindow.contentItem,
                                               mouseTooltipOwner.width / 2,
                                               mouseTooltipOwner.height / 2)
        let sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(500)
        tryCompare(mouseTooltipHelper, "active", true)
        mouseTooltipOwner.enabled = false
        compare(mouseTooltipHelper.active, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        mouseTooltipOwner.enabled = true

        sequence = touchEvent(testWindow.contentItem)
        sequence.press(0, testWindow.contentItem, p.x, p.y).commit()
        wait(500)
        tryCompare(mouseTooltipHelper, "active", true)
        mouseTooltipOwner.visible = false
        compare(mouseTooltipHelper.active, false)
        sequence.release(0, testWindow.contentItem, p.x, p.y).commit()
        mouseTooltipOwner.visible = true
        compare(mouseOwnerClicks, 0)
    }

    function test_desktop_mouse_tooltip_delay_and_long_hold_match_baseline() {
        mouseTooltipHelper.longPressTooltipEnabled = false
        compare(mouseTooltipOwner.pressAndHoldInterval, 720)
        compare(mouseTooltipOwner.ToolTip.delay, 350)
        const hoverPoint = mouseTooltipOwner.mapToItem(testWindow.contentItem,
                                                        mouseTooltipOwner.width / 2,
                                                        mouseTooltipOwner.height / 2)
        mouseMove(testWindow.contentItem, 5, 300)
        mouseMove(testWindow.contentItem, hoverPoint.x, hoverPoint.y)
        tryCompare(mouseTooltipOwner, "containsMouse", true)
        const popup = mouseTooltipOwner.ToolTip.toolTip
        verify(popup !== null)
        wait(180)
        compare(popup.visible, false)
        tryCompare(popup, "visible", true, 500)

        mouseMove(testWindow.contentItem, 5, 300)
        wait(200)
        const helperPoint = desktopHelperOwner.mapToItem(testWindow.contentItem, 24, 20)
        const baselinePoint = desktopBaselineOwner.mapToItem(testWindow.contentItem, 24, 20)
        mousePress(testWindow.contentItem, helperPoint.x, helperPoint.y)
        wait(820)
        mouseRelease(testWindow.contentItem, helperPoint.x, helperPoint.y)
        mousePress(testWindow.contentItem, baselinePoint.x, baselinePoint.y)
        wait(820)
        mouseRelease(testWindow.contentItem, baselinePoint.x, baselinePoint.y)
        compare(desktopHelperClicks, desktopBaselineClicks)
        compare(desktopTooltipHelper.active, false)
    }

    function test_attached_tooltip_uses_colosseum_style() {
        const tip = action.ToolTip.toolTip
        verify(tip !== null)
        compare(tip.padding, 0)
        compare(tip.leftPadding, 16)
        compare(tip.rightPadding, 16)
        verify(tip.contentItem !== null)
    }

    function test_smart_pick_tooltip_uses_colosseum_style() {
        const tip = findChild(smartPick, "smartPickHoverExplanation")
        verify(tip !== null)
        compare(tip.width, 280)
        compare(tip.padding, 0)
        compare(tip.leftPadding, 16)
        compare(tip.rightPadding, 16)
        verify(tip.contentItem !== null)
    }

    function findTooltipArea(root, label) {
        if (!root) return null
        try {
            if (root.tooltipText === label && root.containsMouse !== undefined)
                return root
        } catch (error) {}
        const descendants = root.children || []
        for (let i = 0; i < descendants.length; ++i) {
            const found = findTooltipArea(descendants[i], label)
            if (found) return found
        }
        return null
    }

    function findReaderTopBar(area) {
        let item = area
        while (item) {
            try {
                if (item.shown !== undefined && item.shellWindowed !== undefined)
                    return item
            } catch (error) {}
            item = item.parent
        }
        return null
    }

    function test_reader_chrome_appearance_tooltip_reveals_without_activation() {
        const area = findTooltipArea(readerChrome, "Appearance")
        verify(area !== null)
        const topBar = findReaderTopBar(area)
        verify(topBar !== null)
        const tip = area.ToolTip.toolTip
        verify(tip !== null)
        compare(readerChrome.appearanceOpen, false)

        const directChildren = readerChrome.children || []
        const topBands = []
        for (let i = 0; i < directChildren.length; ++i) {
            const child = directChildren[i]
            if (Math.abs(child.width - readerChrome.width) < 1
                    && Math.abs(child.height - readerChrome.revealBandPx) < 1
                    && Math.abs(child.y) < 1)
                topBands.push(child)
        }
        compare(topBands.length, 1)
        const topBand = topBands[0]
        compare(topBar.parent, topBand)
        let revealHandler = null
        const bandData = topBands[0].data || []
        for (let i = 0; i < bandData.length; ++i) {
            try {
                if (bandData[i].hovered !== undefined && bandData[i].blocking !== undefined) {
                    revealHandler = bandData[i]
                    break
                }
            } catch (error) {}
        }
        verify(revealHandler !== null)
        compare(revealHandler.blocking, false)

        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
        const hiddenCenter = area.mapToItem(testWindow.contentItem, area.width / 2, area.height / 2)
        mouseMove(testWindow.contentItem, hiddenCenter.x, readerChrome.revealBandPx + 4)
        mouseMove(testWindow.contentItem, hiddenCenter.x, readerChrome.revealBandPx - 6)
        tryCompare(readerChrome, "awake", true)
        tryVerify(function() { return topBar.opacity > 0.99 })
        const center = area.mapToItem(testWindow.contentItem, area.width / 2, area.height / 2)
        mouseMove(testWindow.contentItem, center.x, center.y)
        tryCompare(revealHandler, "hovered", true)
        tryCompare(area, "containsMouse", true)
        tryVerify(function() { return tip.visible && tip.text === "Appearance" })
        compare(readerChrome.appearanceOpen, false)
        verify(tip.background !== null)
        const panelOrigin = tip.background.mapToItem(testWindow.contentItem, 0, 0)
        verify(panelOrigin.x >= 0 && panelOrigin.y >= 0
               && panelOrigin.x + tip.width <= testWindow.width
               && panelOrigin.y + tip.height <= testWindow.height)
        verify(readerChrome.revealState.frozen)

        mouseClick(area, area.width / 2, area.height / 2)
        tryCompare(readerChrome, "appearanceOpen", true)
        verify(readerChrome.awake)
        readerChrome.closeAppearance(false)
        mouseMove(testWindow.contentItem, testWindow.width / 2, testWindow.height / 2)
        tryCompare(revealHandler, "hovered", false)
        tryVerify(function() { return !readerChrome.revealState.frozen })
        tryCompare(readerChrome, "awake", false, 4000)
    }

    function check_smart_pick_tooltip_at(controlX, expectedPanelX) {
        smartPick.x = controlX
        const info = findChild(smartPick, "smartPickInfo")
        const tip = findChild(smartPick, "smartPickHoverExplanation")
        verify(info !== null)
        verify(tip !== null)

        mouseMove(testWindow.contentItem, 5, 300)
        tryCompare(tip, "visible", false)
        mouseMove(info, info.width / 2, info.height / 2)
        tryCompare(info, "hovered", true)
        tryCompare(tip, "visible", true)
        tryCompare(tip, "width", 280)
        tryCompare(tip, "margins", 12)

        const panel = tip.background
        verify(panel !== null)
        tryVerify(function() { return tip.parent === info && panel.anchorItem === info })
        const notch = findChild(panel, "tooltipPointerNotch")
        verify(notch !== null)
        tryCompare(notch, "visible", true)
        const iconOrigin = info.mapToItem(testWindow.contentItem, 0, 0)
        const expectedX = Math.max(12, Math.min(testWindow.width - tip.width - 12,
                                                iconOrigin.x + (info.width - tip.width) / 2))
        const expectedY = Math.max(12, Math.min(testWindow.height - tip.height - 12,
                                                iconOrigin.y + info.height + 8))
        const actualPanelOrigin = panel.mapToItem(testWindow.contentItem, 0, 0)
        tryVerify(function() { return Math.abs(expectedX - expectedPanelX) < 1 })
        tryVerify(function() {
            const panelOrigin = panel.mapToItem(testWindow.contentItem, 0, 0)
            return Math.abs(panelOrigin.x - expectedX) < 1
                && Math.abs(panelOrigin.y - expectedY) < 1
                && panelOrigin.x >= 12 && panelOrigin.y >= 12
                && panelOrigin.x + tip.width <= testWindow.width - 12
                && panelOrigin.y + tip.height <= testWindow.height - 12
        })
        tryVerify(function() {
            const anchorCentre = info.mapToItem(panel, info.width / 2, info.height / 2)
            const expectedNotchX = Math.max(panel.radius + 7,
                                            Math.min(panel.width - panel.radius - 7, anchorCentre.x))
            return Math.abs(panel.notchX - expectedNotchX) < 1
                && Math.abs(notch.x + notch.width / 2 - panel.notchX) < 1
        })
    }

    function test_smart_pick_tooltip_stays_centered_in_window() {
        check_smart_pick_tooltip_at(94, 184)
    }

    function test_smart_pick_tooltip_clamps_at_left_edge() {
        check_smart_pick_tooltip_at(-180, 12)
    }

    function test_smart_pick_tooltip_clamps_at_right_edge() {
        check_smart_pick_tooltip_at(1024, 988)
    }
}
