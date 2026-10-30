import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id:tests
    name:"BrowseTemplate"
    when:windowShown
    Window { id:host; visible:true; width:800; height:1280; property bool androidHost:true }
    Component { id:browserComponent; App.DiscoverBrowser { anchors.fill:parent; active:false } }
    Component { id:pickerComponent; App.DiscoverPicker { width:200; options:[{key:"a",text:"Adventure"},{key:"b",text:"Drama"}] } }
    QtObject {
        id:adapterFixture
        function types() { return [{key:"movie",label:"Movies"}] }
        function catalogs(type) { return [{key:"first",title:"First category",sourceKind:"builtin",section:"Fixture"},{key:"second",title:"Second category",sourceKind:"builtin",section:"Fixture"}] }
        function filters(type,key) { return [] }
        function defaultCatalog(type) { return "first" }
    }
    function find(item,name) {
        if (item.objectName===name) return item
        for (const child of item.children || []) { const result=find(child,name); if (result) return result }
        return null
    }
    function test_tablet_browse_keeps_content_width() {
        const browser=createTemporaryObject(browserComponent,host.contentItem)
        wait(30)
        compare(browser.phoneNarrow,true)
        compare(browser.contentLeft,0)
    }
    function test_touch_filter_is_an_opaque_modal() {
        host.requestActivate()
        const picker=createTemporaryObject(pickerComponent,host.contentItem)
        compare(picker.implicitHeight,48)
        picker.openPopup(null)
        wait(30)
        const sheet=picker.activePhoneSheet
        verify(sheet !== null)
        compare(sheet.background.color.a,1)
        keyClick(Qt.Key_Back)
        tryCompare(picker,"open",false)
    }
    function test_category_sheet_preserves_the_existing_selection_route() {
        host.requestActivate()
        const browser=createTemporaryObject(browserComponent,host.contentItem,{adapter:adapterFixture,currentType:"movie",currentCatalogKey:"first"})
        wait(30)
        browser.openCatalogMenu(null)
        tryVerify(function() { return browser.activeChoiceSheet !== null && browser.activeChoiceSheet.opened })
        const sheet=browser.activeChoiceSheet
        wait(40)
        compare(sheet.background.color.a,1)
        const second=find(sheet.contentItem,"second")
        verify(second !== null && second.height>=48)
        mouseClick(second,second.width/2,second.height/2)
        compare(browser.currentCatalogKey,"second")
        compare(browser.catalogMenuOpen,false)
    }
}
