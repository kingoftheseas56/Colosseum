// Arc 74-C: real ContinueRow and ContinueSeeAllPage gesture/keyboard dispatch.
// No live ProgressStore mutation: injected records only.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: test
    name: "Arc74ContinueSurfaces"
    when: windowShown
    Window { id: stage; width: 1240; height: 940; visible: true }
    Component { id: rowComponent; App.ContinueRow {} }
    Component { id: allComponent; App.ContinueSeeAllPage {} }

    property var subject: null
    property int resumed: 0
    property int detailed: 0
    function manga(saved) {
        return {id: "mal:1",kind: "manga",title: "Monster",sub: "Chapter 47",
                updatedAt: 900,cover: Qt.resolvedUrl("../../assets/icons/manga.svg").toString(),resume: saved ? {chapterId: "qualified:47",page: 12} : {}}
    }
    function volume(saved) {
        return {id: "mal:2",kind: "tankoban",title: "Berserk",sub: "Vol 5",
                updatedAt: 850,cover: Qt.resolvedUrl("../../assets/icons/manga.svg").toString(),resume: saved ? {chapterId: "volume-5",page: 9} : {}}
    }
    function findItem(item,name) {
        if (!item) return null
        if (item.objectName === name) return item
        var c = item.children || []
        for (var i=0;i<c.length;i++) {
            var match=findItem(c[i],name)
            if (match) return match
        }
        return null
    }
    function init() {resumed=0;detailed=0}
    function cleanup() {if (subject) subject.destroy();subject=null}

    function test_row_main_click_and_keyboard_activation_resume_manga() {
        subject = rowComponent.createObject(stage.contentItem,{
            x:24,y:24,width:990,items:[manga(true),volume(true)]})
        verify(subject!==null)
        subject.resumeRequested.connect(function(item){resumed++})
        subject.detailRequested.connect(function(item){detailed++})
        wait(100)
        var tile=findItem(subject,"continueRowTile_mal:1")
        verify(tile!==null)
        mouseClick(tile,15,64,Qt.LeftButton)
        compare(resumed,1);compare(detailed,0)
        subject.currentIndex=0
        subject.activateCurrent()
        compare(resumed,2);compare(detailed,0)
        mouseClick(tile,15,19,Qt.LeftButton)
        compare(detailed,1)
    }

    function test_row_missing_target_and_collection_bookmark_remain_details() {
        subject=rowComponent.createObject(stage.contentItem,{
            x:24,y:24,width:990,items:[manga(false)]})
        verify(subject!==null)
        subject.resumeRequested.connect(function(item){resumed++})
        subject.detailRequested.connect(function(item){detailed++})
        wait(80)
        subject.activateCurrent()
        compare(resumed,0);compare(detailed,1)
        subject.primaryMangaResumeEnabled=false
        subject.items=[manga(true)]
        wait(80)
        var tile=findItem(subject,"continueRowTile_mal:1")
        verify(tile!==null)
        verify(!tile.hasMangaResume)
        mouseClick(tile,14,65,Qt.LeftButton)
        compare(resumed,0);compare(detailed,2)
    }

    function test_seeall_grid_pointer_and_keyboard_resume_saved_manga() {
        subject=allComponent.createObject(stage.contentItem,{x:0,y:0,width:1190,height:860,scope:"tankoban"})
        verify(subject!==null)
        subject.resumeRequested.connect(function(item){resumed++})
        subject.detailRequested.connect(function(item){detailed++})
        wait(80)
        subject.rawItems=[manga(true),volume(true)]
        wait(100)
        var tile=findItem(subject,"continueSeeAllTile_mal:1")
        verify(tile!==null,"See All must instantiate shared tile")
        mouseClick(tile,15,65,Qt.LeftButton)
        compare(resumed,1);compare(detailed,0)
        var grid=findItem(subject,"continueSeeAllGrid")
        verify(grid!==null)
        stage.requestActivate()
        wait(30)
        grid.currentIndex=0
        grid.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return grid.activeFocus }, 1000,
                  "The See All grid must actually receive keyboard focus")
        keyClick(Qt.Key_Return)
        compare(resumed,2)
        mouseClick(tile,15,18,Qt.LeftButton)
        compare(detailed,1)
    }

    function test_seeall_missing_target_opens_details() {
        subject=allComponent.createObject(stage.contentItem,{x:0,y:0,width:1190,height:860,scope:"tankoban"})
        verify(subject!==null)
        subject.resumeRequested.connect(function(item){resumed++})
        subject.detailRequested.connect(function(item){detailed++})
        wait(80)
        subject.rawItems=[manga(false)]
        wait(100)
        var tile=findItem(subject,"continueSeeAllTile_mal:1")
        verify(tile!==null)
        mouseClick(tile,15,70,Qt.LeftButton)
        compare(resumed,0);compare(detailed,1)
    }
}
