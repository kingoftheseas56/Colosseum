import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
TestCase {
    name: "Arc64TabIdentity"
    when: windowShown
    App.Theme { id: theme }
    Window { id: stage; width:800; height:240; visible:true
        Rectangle { id: backdrop; anchors.fill:parent; color:"#101013" }
    }
    property var bar: null

    function test_source_and_docked_names() {
        var c=Qt.createComponent("../../qml/WorldTabBar.qml")
        if (c.status===Component.Error && c.errorString().indexOf("RectangularShadow is not a type")>=0)
            skip("Pinned WorldTabBar requires RectangularShadow unavailable in sandbox Qt6.8.2; run on Qt6.11.1")
        compare(c.status,Component.Ready,c.errorString())
        bar=c.createObject(stage.contentItem,{width:800,height:100,backdrop:backdrop,tabPrefix:"biblioTab",currentTab:"discover",tabModel:[{key:"discover",label:"Discover"},{key:"explore",label:"Explore"},{key:"library",label:"Library"}]})
        verify(bar!==null);bar.tabRequested.connect(function(tab){bar.currentTab=tab})
        stage.requestActivate();wait(50)
        var inline=findChild(bar,"biblioTab_library")
        verify(inline!==null,"pinned WorldTabBar synthesizes the in-flow name")
        mouseClick(inline);compare(bar.currentTab,"library")
        bar.tabPrefix="biblioTabDock";bar.currentTab="discover";wait(0)
        var dock=findChild(bar,"biblioTabDock_library")
        verify(dock!==null);mouseClick(dock);compare(bar.currentTab,"library")
    }
}
