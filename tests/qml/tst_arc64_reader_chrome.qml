import QtQuick
import QtQuick.Window
import QtTest
TestCase {
    name: "Arc64ReaderChrome"
    when: windowShown
    Arc64Fonts { id: fonts }
    Window { id: stage; width:1280; height:720; visible:true; color:"#101013" }
    property var subject: null
    function init() {
        tryCompare(fonts,"ready",true)
        var component=Qt.createComponent("../../qml/reader2/ReaderChrome.qml")
        compare(component.status,Component.Ready,component.errorString())
        subject=component.createObject(stage.contentItem,{width:1280,height:720,audioAttached:true,audioPlaylist:["One","Two"],audioCurrentIndex:0})
        verify(subject!==null)
        stage.requestActivate();subject.wake();wait(350)
    }
    function cleanup() { if(subject)subject.destroy();subject=null;wait(0) }
    function test_sleep_control_and_focus() {
        var moon=findChild(subject,"readerSleepButton"), menu=findChild(subject,"readerSleepMenu")
        verify(moon!==null);verify(menu!==null);verify(findChild(subject,"readerSpeedButton")!==null)
        var calls=0,chosen=null
        subject.audioSleepChosen.connect(function(mode){calls++;chosen=mode})
        mouseClick(moon);tryCompare(menu,"open",true)
        keyClick(Qt.Key_Down);keyClick(Qt.Key_Return)
        compare(calls,1);compare(chosen,15);compare(menu.open,false)
    }
    function test_mock_right_zone_order() {
        var moon=findChild(subject,"readerSleepButton"), speed=findChild(subject,"readerSpeedButton")
        verify(moon.x < speed.x, "mock order is moon, speed, chapters")
        verify(speed.x + speed.width <= speed.parent.width - 36)
    }
    function test_offer_chip_routes_audio_panel() {
        subject.audioOfferChipVisible=true
        var chip=findChild(subject,"readerAudioChip");verify(chip!==null);tryCompare(chip,"visible",true)
        var action=findChild(chip,"reader2AudioOfferChipAction")
        waitForRendering(chip)
        mouseClick(action)
        compare(subject.panelOpen,true);compare(subject.activeTab,"audio")
    }
}
