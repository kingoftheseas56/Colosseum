import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/account" as Account

TestCase {
    name:"SettingsTemplate"
    when:windowShown
    Window { id:host; visible:true; width:800; height:1280; property bool androidHost:true }
    QtObject { id:ime; property bool visible:false; property rect keyboardRectangle:Qt.rect(0,0,0,0) }
    Component {
        id:frameComponent
        Account.AccountPageFrame {
            anchors.fill:parent; inputMethod:ime
            headline:"Sign in to your account"
            Account.AccountField { id:field; objectName:"fixtureField"; width:parent.width; label:"Password"; password:true; controlObjectName:"fixturePassword" }
            Account.AccountButton { objectName:"fixtureSubmit"; text:"Continue" }
        }
    }
    Component { id:centerComponent; Account.AccountCenter { anchors.fill:parent } }
    Component { id:stremioComponent; App.StremioSyncPanel { anchors.fill:parent; shown:true; onCloseRequested:shown=false } }
    Component { id:flyoutComponent; Account.AccountFlyout {} }
    Component { id:profileComponent; App.PhoneProfilePage { anchors.fill:parent; rows:[{key:"account",label:"Account",note:"Sign in, devices and security"}] } }
    Component { id:trackerComponent; App.TrackerSyncCenterPage { anchors.fill:parent; trackerModel:null;trackerConnector:null } }
    SignalSpy { id:profileSpy; signalName:"rowRequested" }
    function init() { host.width=800;host.height=1280;host.androidHost=true;ime.visible=false;ime.keyboardRectangle=Qt.rect(0,0,0,0) }
    function find(item,name) {
        if(item.objectName===name)return item
        for(const child of item.children || []) { const found=find(child,name);if(found)return found }
        return null
    }
    function test_tablet_settings_and_auth_use_touch_geometry() {
        const frame=createTemporaryObject(frameComponent,host.contentItem)
        verify(frame!==null)
        compare(frame.phoneLayout,true)
        verify(find(frame,"fixturePassword").height>=48)
        verify(find(frame,"fixturePasswordReveal").width>=48)
        verify(find(frame,"fixtureSubmit").height>=48)
        ime.keyboardRectangle=Qt.rect(0,900,800,380);ime.visible=true
        compare(frame.keyboardOverlap,380)
        const center=createTemporaryObject(centerComponent,host.contentItem)
        verify(center!==null)
        compare(center.compactPhoneLayout,true)
        ime.visible=false
    }
    function test_auth_sizes_data() {
        return [{tag:"narrow",w:354,h:792},{tag:"phone",w:412,h:900},{tag:"tablet",w:800,h:1280},{tag:"wide-tablet",w:1280,h:800},{tag:"desktop",w:1280,h:700,desktop:true}]
    }
    function test_auth_sizes(data) {
        host.width=data.w;host.height=data.h;host.androidHost=!data.desktop
        const frame=createTemporaryObject(frameComponent,host.contentItem)
        const password=find(frame,"fixturePassword")
        wait(20)
        verify(password.width>0 && password.width<=data.w-frame.horizontalMargin*2)
        verify(password.height>=(data.desktop ? 46 : 48))
        verify(find(frame,"fixtureSubmit").height>=(data.desktop ? 46 : 48))
    }
    function test_profile_uses_the_shared_row_and_existing_route() {
        const profile=createTemporaryObject(profileComponent,host.contentItem)
        wait(30)
        const row=find(profile,"profileRow_account")
        verify(row!==null && row.height>=48)
        profileSpy.target=profile;profileSpy.clear()
        mouseClick(row,row.width/2,row.height/2)
        compare(profileSpy.count,1)
        compare(profileSpy.signalArguments[0][0],"account")
    }
    function test_tracker_keeps_measured_cards_and_touch_actions() {
        const page=createTemporaryObject(trackerComponent,host.contentItem)
        verify(page!==null)
        wait(20)
        compare(page.connectedColumns,2)
        const button=find(page,"trackerPreferencesButton")
        verify(button!==null && button.width>=48 && button.height>=48)
    }
    function test_wide_account_session_action_keeps_a_touch_target() {
        host.width=1280;host.height=800
        const center=createTemporaryObject(centerComponent,host.contentItem)
        verify(center!==null)
        tryCompare(center,"compactPhoneLayout",false)
        const session=find(center,"accountCenterRailSessionAction")
        verify(session!==null && session.width>=48 && session.height>=48)
    }
    function test_existing_panel_content_uses_the_exclusive_sheet_and_returns_home() {
        host.requestActivate()
        const panel=createTemporaryObject(stremioComponent,host.contentItem)
        verify(panel!==null)
        const sheet=panel.activePhoneSheet
        tryCompare(sheet,"opened",true)
        compare(sheet.background.color.a,1)
        const popupParent=sheet.hostedItem.parent
        panel.confirmation="disconnect"
        keyClick(Qt.Key_Back)
        compare(panel.confirmation,"")
        compare(panel.shown,true)
        const flyout=createTemporaryObject(flyoutComponent,host.contentItem)
        flyout.open()
        tryCompare(flyout.activePhoneSheet,"opened",true)
        compare(panel.shown,false)
        verify(sheet.hostedItem.parent!==popupParent,"closing restores the existing content's original parent")
        keyClick(Qt.Key_Back)
        tryCompare(flyout,"visible",false)
    }
}
