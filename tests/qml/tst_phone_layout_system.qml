import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as App
import "../../qml/PhoneOverlayRegistry.js" as Overlays

TestCase {
    id: tests
    name: "PhoneLayoutSystem"
    when: windowShown
    width: 1280
    height: 900

    Window {
        id: host
        width: 412
        height: 900
        visible: true
        property bool androidHost: true
        Rectangle { anchors.fill: parent; color: "#ff00ff" }
        TextInput { id: previousFocus; width: 100; height: 48; text: "Focus" }
    }
    Component {
        id: metricsComponent
        App.AdaptiveLayout { touch: true; viewportHeight: 900 }
    }
    Component {
        id:touchOwnerComponent
        App.BrowseScaffold {
            property bool androidHost:true
            width:400;height:800
            App.TouchTarget { objectName:"inheritedTouchTarget" }
            App.TouchComboBox { objectName:"inheritedTouchCombo";y:100;width:200;model:["English","Español"] }
        }
    }
    SignalSpy { id:comboSpy;signalName:"activated" }
    Component {
        id: sheetComponent
        App.PhoneModalSheet {
            parent: host.contentItem
            title: "Sheet"
            App.PhoneListItem { text: "Audio"; width: parent.width }
        }
    }
    Component { id: targetComponent; App.TouchTarget { touch: true; accessibleName: "Small icon" } }

    function test_classifier_boundaries_data() {
        return [
            {tag:"compact", width:354, height:792, expected:"compact-phone"},
            {tag:"compact-edge", width:430, height:900, expected:"compact-phone"},
            {tag:"phone", width:431, height:900, expected:"phone"},
            {tag:"phone-edge", width:599, height:900, expected:"phone"},
            {tag:"tablet", width:600, height:1280, expected:"tablet-portrait"},
            {tag:"T20", width:800, height:1280, expected:"tablet-portrait"},
            {tag:"tablet-edge", width:839, height:1280, expected:"tablet-portrait"},
            {tag:"wide-edge", width:840, height:1280, expected:"wide-with-rail"},
            {tag:"T20-landscape", width:1280, height:800, expected:"wide-with-rail"},
            {tag:"short-landscape", width:792, height:354, expected:"short-landscape"}
        ]
    }
    function test_classifier_boundaries(data) {
        const metrics = createTemporaryObject(metricsComponent, tests,
            {viewportWidth:data.width, viewportHeight:data.height})
        compare(metrics.layoutClass, data.expected)
        compare(metrics.phoneLayout, data.width < 840 || data.height < 500)
    }
    function test_usable_width_and_keyboard() {
        const metrics = createTemporaryObject(metricsComponent, tests,
            {viewportWidth:928, viewportHeight:900, railInset:72,
             safeInsets:({left:8, top:24, right:16, bottom:20}), keyboardInset:300})
        compare(metrics.usableWidth, 832)
        compare(metrics.usableHeight, 556)
        compare(metrics.layoutClass, "tablet-portrait")
        metrics.viewportWidth = 1280
        metrics.touch = false
        compare(metrics.phoneLayout, false)
    }
    function test_touch_target_reserves_space() {
        const target = createTemporaryObject(targetComponent, host.contentItem)
        verify(target.width >= 48)
        verify(target.height >= 48)
        const spy = signalSpy.createObject(tests, {target:target})
        mouseClick(target, 2, 2)
        compare(spy.count, 1)
        spy.destroy()
    }
    Component { id: signalSpy; SignalSpy { signalName:"triggered" } }
    Component { id:observerComponent; QtObject { property int revision:0 } }
    function test_shell_observer_tracks_modal_ownership() {
        const observer=createTemporaryObject(observerComponent,tests)
        const sheet=createTemporaryObject(sheetComponent,host.contentItem)
        Overlays.subscribe(host,observer)
        const before=observer.revision
        sheet.open()
        tryVerify(function() { return observer.revision>before })
        compare(Overlays.current(host),sheet)
        const opened=observer.revision
        sheet.close()
        tryVerify(function() { return observer.revision>opened })
        compare(Overlays.current(host),null)
        Overlays.unsubscribe(observer)
    }
    Component {
        id: clippedPageComponent
        Item {
            width:800; height:1280; clip:true
            Item { id:longPage; width:1100; height:4000 }
            property alias metrics:pageLayout
            App.AdaptiveLayout { id:pageLayout; page:longPage; touch:true }
        }
    }
    function test_clipped_page_uses_visible_viewport() {
        host.width=800; host.height=1280
        const fixture=createTemporaryObject(clippedPageComponent,host.contentItem)
        compare(fixture.metrics.usableWidth,800)
        compare(fixture.metrics.viewportHeight,1280)
        compare(fixture.metrics.layoutClass,"tablet-portrait")
        fixture.width=354; fixture.height=792
        compare(fixture.metrics.usableWidth,354)
        compare(fixture.metrics.layoutClass,"compact-phone")
    }
    function init() {
        host.width=412;host.height=900;host.androidHost=true
        host.requestActivate()
        tryVerify(function() { return host.active })
    }
    function test_touch_parts_inherit_the_page_owner_and_combo_selection() {
        host.androidHost=false
        const owner=createTemporaryObject(touchOwnerComponent,host.contentItem)
        const target=findChild(owner,"inheritedTouchTarget")
        verify(target.touch && target.width>=48 && target.height>=48)
        const combo=findChild(owner,"inheritedTouchCombo")
        comboSpy.target=combo;comboSpy.clear()
        mouseClick(combo,combo.width/2,combo.height/2)
        const popup=combo.activePhoneSheet
        verify(popup!==null)
        tryCompare(popup,"opened",true)
        wait(40)
        compare(popup.width,host.width,"the choice sheet uses the window rather than the small ComboBox")
        const choice=findChild(popup.contentItem,"touchComboChoice_1")
        verify(choice!==null && choice.height>=48)
        mouseClick(choice,choice.width/2,choice.height/2)
        compare(combo.currentIndex,1)
        compare(comboSpy.count,1)
        compare(popup.visible,false)
    }
    function test_sheet_opaque_back_focus_and_outside() {
        previousFocus.forceActiveFocus()
        const sheet = createTemporaryObject(sheetComponent, host.contentItem,
            {safeInsets:({left:12,top:24,right:16,bottom:20}), keyboardInset:250})
        sheet.open()
        tryCompare(sheet, "opened", true)
        compare(sheet.focusReturnItem, previousFocus)
        compare(sheet.background.color.a, 1)
        compare(sheet.background.opacity, 1)
        verify(sheet.y + sheet.height <= host.height - 250)
        keyClick(Qt.Key_Back)
        tryCompare(sheet, "visible", false)
        tryCompare(previousFocus, "activeFocus", true)
        sheet.open()
        tryCompare(sheet, "opened", true)
        mouseClick(host.contentItem, 200, 60)
        tryCompare(sheet, "visible", false)
    }
    function test_sheet_exclusive_per_window() {
        const first = createTemporaryObject(sheetComponent, host.contentItem)
        const second = createTemporaryObject(sheetComponent, host.contentItem)
        previousFocus.forceActiveFocus()
        first.open()
        tryCompare(first, "opened", true)
        compare(first.focusReturnItem, previousFocus)
        second.open()
        tryCompare(second, "opened", true)
        compare(first.visible, false)
        second.close()
        tryCompare(previousFocus, "activeFocus", true)
    }
    function test_sheet_back_survives_underlay_focus_change() {
        const sheet=createTemporaryObject(sheetComponent,host.contentItem)
        sheet.open()
        tryCompare(sheet,"opened",true)
        previousFocus.forceActiveFocus()
        keyClick(Qt.Key_Back)
        tryCompare(sheet,"visible",false)
    }
}
