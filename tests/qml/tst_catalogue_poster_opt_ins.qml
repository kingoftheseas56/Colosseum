import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "CataloguePosterOptIns"
    width: 800; height: 600; visible: true
    when: windowShown
    Component { id: cardComponent; App.CataloguePosterCard { width: 148; height: 320 } }
    Component { id: gridComponent; App.CataloguePosterGrid { width: 800; height: 600 } }
    SignalSpy { id: activated; signalName: "activated" }
    SignalSpy { id: contextAction; signalName: "contextActionTriggered" }
    function cleanup() {
        activated.target=null; activated.clear()
        contextAction.target=null; contextAction.clear()
    }
    function test_contextMenuIsLazyAndDoesNotActivate() {
        const row={title:"Local title",fileId:"original"}
        const card=createTemporaryObject(cardComponent,testCase,
            {item:row,contextActionText:"Hide from Vault"})
        card.item=row
        activated.target=card; contextAction.target=card
        let activatedRow=null, hiddenRow=null
        card.activated.connect(row => activatedRow=row)
        card.contextActionTriggered.connect(row => hiddenRow=row)
        compare(findChild(card,"catalogueContextMenu"),null)
        mouseClick(card,30,30)
        compare(activated.count,1)
        verify(activatedRow === row)
        mouseClick(card,30,30,Qt.RightButton)
        const menu=findChild(card,"catalogueContextMenu")
        verify(menu !== null)
        tryCompare(menu,"opened",true)
        compare(activated.count,1)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        compare(contextAction.count,1)
        verify(hiddenRow === row)
        tryCompare(menu,"opened",false)
        card.skeleton=true
        card.openContextMenu()
        tryVerify(() => findChild(card,"catalogueContextMenu") === null)
        compare(activated.count,1)
        compare(contextAction.count,1)
    }
    function test_defaultsLeaveWorldsUnchanged() {
        const card=createTemporaryObject(cardComponent,testCase,{item:{title:"House title",author:"Writer"}})
        compare(card.subtitleText,""); compare(card.badgeText,"")
        compare(card.completed,false); compare(card.progressFraction,0)
        compare(card.dimmed,false); compare(card.opacity,1); compare(card.contextActionText,"")
        compare(card.showAuthorAtRest,false)
        compare(findChild(card,"catalogueBadge").visible,false)
        compare(findChild(card,"catalogueCompleted").visible,false)
        compare(findChild(card,"catalogueProgress").visible,false)
        compare(findChild(card,"catalogueSubtitle").visible,false)
        compare(findChild(card,"catalogueContextMenu"),null)
        const grid=createTemporaryObject(gridComponent,testCase)
        compare(grid.metadataExtraHeight,0)
        const classicHeight=grid.cellHeight
        grid.metadataExtraHeight=16
        compare(grid.cellHeight,classicHeight+16)
        grid.visualProfile="gallery"; grid.metadataExtraHeight=0
        const galleryHeight=grid.cellHeight
        grid.metadataExtraHeight=32
        compare(grid.cellHeight,galleryHeight+32)
    }
    function test_subtitleHugsRenderedTitle_data() {
        return [{tag:"one-line",title:"Title"},
                {tag:"two-lines",title:"A longer title that wraps onto two lines"}]
    }
    function test_subtitleHugsRenderedTitle(data) {
        const card=createTemporaryObject(cardComponent,testCase,
            {item:{title:data.title,author:"Writer"},visualProfile:"gallery",subtitleText:"2026"})
        const subtitle=findChild(card,"catalogueSubtitle")
        const title=card.children.find(child => child.text === data.title)
        verify(title !== undefined)
        compare(subtitle.y,title.y+title.contentHeight+3)
        card.showAuthorAtRest=true
        const author=card.children.find(child => child.text === "Writer")
        compare(subtitle.y,author.y+author.height+3)
    }
    function test_optInProgressClampsAndCompletedWins() {
        const card=createTemporaryObject(cardComponent,testCase,
            {item:{title:"Title"},visualProfile:"gallery",badgeText:"NEW",progressFraction:2,dimmed:true})
        compare(card.opacity,0.48)
        const fill=findChild(card,"catalogueProgressFill")
        compare(fill.width,fill.parent.width)
        compare(findChild(card,"catalogueProgress").visible,true)
        card.completed=true
        compare(findChild(card,"catalogueProgress").visible,false)
        compare(findChild(card,"catalogueCompleted").visible,true)
        card.skeleton=true
        compare(findChild(card,"catalogueBadge").visible,false)
        compare(findChild(card,"catalogueCompleted").visible,false)
        card.skeleton=false; card.completed=false; card.progressFraction=-1
        compare(fill.width,0)
        compare(findChild(card,"catalogueProgress").visible,false)
    }
}
