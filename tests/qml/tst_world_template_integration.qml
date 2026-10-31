import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    name:"WorldTemplateIntegration"
    when:windowShown
    Window { id:host;visible:true;width:800;height:1280;property bool androidHost:true }
    Component { id:theatre;App.TheatreWorld { anchors.fill:parent;androidHost:host.androidHost;lifecycleActive:false } }
    Component { id:tankoban;App.TankobanWorld { anchors.fill:parent;androidHost:host.androidHost;lifecycleActive:false } }
    Component { id:biblio;App.BiblioWorld { anchors.fill:parent;androidHost:host.androidHost;lifecycleActive:false } }
    Component { id:universes;App.UniversesWorld { anchors.fill:parent;androidHost:host.androidHost;lifecycleActive:false } }
    function test_actual_worlds_data() {
        return [{tag:"Theatre",factory:theatre},{tag:"Tankoban",factory:tankoban},{tag:"Biblio",factory:biblio},{tag:"Universes",factory:universes}]
    }
    function test_actual_worlds(data) {
        const world=createTemporaryObject(data.factory,host.contentItem)
        verify(world!==null)
        wait(30)
        compare(world.phoneLayout,true)
        verify(world.pageFlickable!==null && world.pageFlickable.height>0)
        verify(world.chromeLeft>=world.layout.pageMargin)
    }
}
