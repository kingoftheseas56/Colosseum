import QtQuick
import QtTest
TestCase {
    name: "VinylHomePage"
    when: windowShown
    property var host
    function item(name) { return findChild(host.contentItem,name) }
    function init() {
        var c=Qt.createComponent("../../qml/vinyl/VinylPagesPrototype.qml")
        verify(c.status===Component.Ready,c.errorString())
        host=c.createObject(null); host.pageData.frozen=true; host.page="world"
        tryCompare(host,"ready",true)
    }
    function cleanup() { if(host){host.close();host.destroy()} }

    function test_skipDoneAndSearch() {
        verify(host.worldData!==undefined)
        var picker=item("vinylTastePicker")
        verify(picker.visible)
        host.worldScroll.contentY=item("vinylWorld").contentTop
        mouseClick(item("vinylPickerSkip"))
        verify(host.worldData.pickerAnswered)
        verify(host.worldData.coldShelves.length>0)
        item("vinylWorld").setTab("discover");item("vinylWorld").setTab("home")
        verify(!picker.visible)
        host.worldData.resetPicker()
        host.worldData.setPickerQuery("Rahman")
        verify(picker.offered.length>0)
        picker.offered.forEach(function(i){verify(i.title.toLowerCase().indexOf("rahman")>=0)})
        host.worldData.setPickerQuery("")
        picker.toggleArtist(picker.offered[0].id)
        picker.toggleArtist(picker.offered[1].id)
        picker.toggleArtist(picker.offered[2].id)
        mouseClick(item("vinylPickerDone"))
        verify(host.worldData.homeRows.length>0)
        compare(host.worldData.coldShelves.length,0)
    }
    function test_pickerDraftDoesNotCrossProfiles() {
        host.worldData.setProfile("skipped")
        host.worldData.resetPicker()
        var picker=item("vinylTastePicker")
        picker.toggleArtist(picker.offered[0].id)
        verify(picker.selected.length===1)
        host.worldData.setProfile("new")
        compare(picker.selected.length,0)
        host.worldData.answerPicker(picker.selected)
        compare(host.worldData.picks.length,0)
    }
    function test_doneEmptyAndDiscoverAccessible() {
        verify(host.worldData!==undefined)
        var world=item("vinylWorld");verify(item("vinylTastePicker").visible)
        world.setTab("discover");compare(world.currentTab,"discover")
        world.setTab("home");host.worldScroll.contentY=world.contentTop
        mouseClick(item("vinylPickerDone"));verify(host.worldData.pickerAnswered)
        verify(host.worldData.coldShelves.length>0)
    }
    function test_seasonedRowsAndOffline() {
        verify(host.worldData!==undefined)
        host.worldData.setProfile("seasoned")
        var page=item("vinylHomePage")
        compare(page.rowKeys[0],"jump-back")
        compare(page.rowKeys[page.rowKeys.length-1],"rediscovery")
        compare(page.rowKeys.filter(function(k){return k.indexOf("recommend-")===0}).length,5)
        ;["popular","trending","new","essentials","global"].forEach(function(k){verify(page.rowKeys.indexOf(k)<0)})
        var rows=JSON.stringify(page.rowKeys)
        host.worldData.setCatalogueReachable(false);compare(JSON.stringify(page.rowKeys),rows)
        host.worldData.openList(page.rowKeys[1],true)
        compare(item("vinylListPage").itemCount,host.worldData.row(page.rowKeys[1],true).items.length)
    }
}
