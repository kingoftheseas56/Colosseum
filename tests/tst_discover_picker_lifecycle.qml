import QtQuick
import QtTest
import "../qml" as UI

Item {
    id: host
    width: 640; height: 480
    Rectangle { id: invoker; width: 20; height: 20; focus: true }
    Component { id: factory; UI.DiscoverPicker { x: 40; y: 30; width: 220; label: "Genre" } }
    SignalSpy { id: picked; signalName: "picked" }
    TestCase {
        name: "DiscoverPickerLifecycle"
        when: windowShown
        function options() { return [{header:"Genres"},{key:"a",text:"Action"},{key:"b",text:"Drama"},{header:"Publishers"},{key:"c",text:"Other"}] }
        function make() {
            var p = createTemporaryObject(factory,host,{options:options(),currentKey:"b"})
            verify(p !== null); picked.target = p; picked.clear(); return p
        }
        function listIn(item) {
            if (typeof item.positionViewAtIndex === "function" && item.contentItem !== undefined) return item
            for (var i=0;i<item.children.length;i++) { var found=listIn(item.children[i]);if(found)return found }
            return null
        }
        function test_closed_refresh_keeps_label_without_list_work() {
            var p=make(), list=listIn(p);verify(list !== null)
            compare(p.current.text,"Drama");compare(p.hasValue,true)
            tryCompare(list,"count",0)
            var many=[];for(var i=0;i<500;i++)many.push({key:"x"+i,text:"Genre "+i})
            many.push({key:"b",text:"Updated drama"});p.options=many
            compare(p.current.text,"Updated drama");tryCompare(list,"count",0)
            compare(picked.count,0)
        }
        function test_open_keyboard_selection_skips_headers_and_restores_focus() {
            var p=make(),list=listIn(p);invoker.forceActiveFocus();p.openPopup(invoker)
            tryCompare(list,"count",5);tryVerify(function(){return list.activeFocus})
            compare(list.currentIndex,2)
            keyClick(Qt.Key_Down);compare(list.currentIndex,4)
            keyClick(Qt.Key_Up);compare(list.currentIndex,2)
            keyClick(Qt.Key_Home);compare(list.currentIndex,1)
            keyClick(Qt.Key_End);compare(list.currentIndex,4)
            keyClick(Qt.Key_Return);compare(picked.count,1);compare(picked.signalArguments[0][0],"c")
            compare(p.open,false);tryCompare(list,"count",0);tryVerify(function(){return invoker.activeFocus})
        }
        function test_escape_releases_rows_and_reopen_uses_latest_options() {
            var p=make(),list=listIn(p);p.openPopup(invoker)
            tryVerify(function(){return list.activeFocus});keyClick(Qt.Key_Escape)
            compare(p.open,false);tryCompare(list,"count",0);compare(picked.count,0)
            p.options=[{header:"New"},{key:"b",text:"New drama"}]
            p.openPopup(invoker);tryCompare(list,"count",2);compare(list.currentIndex,1)
            tryVerify(function(){return list.activeFocus});keyClick(Qt.Key_Return)
            compare(picked.count,1);compare(picked.signalArguments[0][0],"b");tryCompare(list,"count",0)
        }
    }
}
