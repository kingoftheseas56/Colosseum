import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneTextScaling"
    when: windowShown
    Window { id: window; width:354; height:792; visible:true; property bool androidHost:true }
    App.Theme { id: tokens }
    Component { id: backComponent; App.BackAction { label:"Back" } }
    Component {
        id: navComponent
        App.PhoneWorldTabBar {
            width:window.width
            worlds:[{name:"Theatre",icon:""},{name:"Tankoban",icon:""},{name:"Biblio",icon:""},{name:"Universes",icon:""}]
        }
    }
    Component {
        id: sheetComponent
        App.ExtensionsSetupSheetAndroid { width:window.width; height:window.height; addon:({name:"Torrentio",slug:"torrentio",configurable:true}) }
    }
    Component { id: theatreComponent; App.LibraryPage { anchors.fill:parent } }
    Component { id: biblioComponent; App.BiblioLibraryPage { anchors.fill:parent } }
    Component { id: tankobanComponent; App.TankobanLibraryTab { anchors.fill:parent } }
    Component { id: featuredComponent; App.FeaturedCarousel { width:window.width; height:implicitHeight } }
    function find(root,predicate) {
        if (predicate(root)) return root
        for (const child of root.children) { const found=find(child,predicate); if(found)return found }
        return null
    }
    function init() { FontTestFixture.setScale(1); window.width=354; window.height=792; window.requestActivate() }
    function cleanup() { FontTestFixture.setScale(1) }
    function test_native_preference_notifies_shared_tokens() {
        compare(tokens.fontSize(16),16)
        FontTestFixture.setScale(1.3)
        fuzzyCompare(tokens.fontSize(16),20.8,0.001)
        FontTestFixture.setScale(NaN)
        fuzzyCompare(tokens.fontScale,1.3,0.001)
    }
    function test_readable_back_and_navigation_labels_scale_live() {
        const back=createTemporaryObject(backComponent,window.contentItem)
        const nav=createTemporaryObject(navComponent,window.contentItem,{y:600})
        const backText=find(back,item=>item["text"]==="Back")
        const navText=find(nav,item=>item["text"]==="Theatre")
        const initialBack=backText.font.pixelSize, initialNav=navText.font.pixelSize
        FontTestFixture.setScale(1.3)
        verify(backText.font.pixelSize>initialBack,"Back ignores Android font preference")
        verify(navText.font.pixelSize>initialNav,"World navigation ignores Android font preference")
        verify(back.height>=48 && nav.height>=56)
        FontTestFixture.setScale(1.8)
        for (const entry of nav.worlds) {
            const label=find(nav,item=>item.objectName==="phoneWorldTabLabel_"+entry.name)
            const tab=find(nav,item=>item.objectName==="phoneWorldTab_"+entry.name)
            verify(label.width<=tab.width && label.height<tab.height)
            const position=label.mapToItem(tab,0,0)
            verify(position.y+label.height<=tab.height+1,"Navigation label spills out of its target")
        }
    }
    function test_long_setup_message_is_scrollable_at_large_text() {
        FontTestFixture.setScale(1.8)
        window.width=792; window.height=354
        const sheet=createTemporaryObject(sheetComponent,window.contentItem)
        sheet.status="A long setup message that must remain reachable with large text. ".repeat(20)
        wait(20)
        const popup=sheet.activePhoneSheet
        verify(popup!==null && popup.visible, "the shared modal is the active phone surface")
        const panel=popup.contentItem.parent
        const scroll=popup.bodyViewport
        verify(panel.y>=0 && panel.y+panel.height<=window.height)
        verify(scroll.contentHeight>scroll.height)
        const install=find(popup.contentItem,item=>item.objectName==="extensionsSetupInstall")
        verify(install!==null, "the hosted install action is in the modal visual tree")
        install.forceActiveFocus()
        sheet.revealFocusedControl()
        wait(10)
        const point=install.mapToItem(scroll,0,0)
        verify(point.y>=0 && point.y+install.height<=scroll.height+1)
    }
    function test_portrait_library_captions_do_not_overflow_their_cards_data() {
        return [{tag:"theatre",component:theatreComponent,grid:"theatreLibraryGrid"},
                {tag:"biblio",component:biblioComponent,grid:"biblioLibraryGrid"},
                {tag:"tankoban",component:tankobanComponent,grid:"tankobanLibraryGrid"}]
    }
    function test_portrait_library_captions_do_not_overflow_their_cards(data) {
        FontTestFixture.setScale(1.8)
        const rows=[]
        for(let index=0;index<6;++index)rows.push({entry:{id:"large-"+index,title:"A Long Library Caption Number "+index,type:"movie"},
            author:"A Long Author Name",progress:0,state:"unwatched",canResume:false,downloaded:false,year:2024,newCount:0})
        const page=createTemporaryObject(data.component,window.contentItem,{allRows:rows,visibleRows:rows})
        wait(30)
        const grid=find(page,item=>item.objectName===data.grid)
        grid.forceLayout()
        const card=grid.itemAtIndex(0)
        verify(card!==null)
        const title=find(card,item=>item["text"]===rows[0].entry.title)
        verify(title!==null)
        verify(title.font.pixelSize>=20,"Poster captions must actually scale")
        const point=title.mapToItem(card,0,0)
        verify(point.y>=0 && point.y+title.height<=card.height+1,
            "Caption exceeds its card: bottom="+(point.y+title.height)+" height="+card.height)
        const metadata=find(card,item=>item["text"]==="2024 · Movie" || item["text"]==="A Long Author Name")
        if(metadata) {
            const detail=metadata.mapToItem(card,0,0)
            verify(detail.y+metadata.height<=card.height+1,"Metadata overlaps the next poster row")
        }
    }
    function test_featured_copy_and_actions_fit_at_large_text_data() {
        return [{tag:"portrait",width:354,height:792},{tag:"landscape",width:792,height:354}]
    }
    function test_featured_copy_and_actions_fit_at_large_text(data) {
        FontTestFixture.setScale(1.8)
        window.width=data.width;window.height=data.height
        const title="A very long featured title that must remain inside its artwork at the larger Android text setting"
        const blurb="The full description remains a bounded preview. ".repeat(8)
        const carousel=createTemporaryObject(featuredComponent,window.contentItem,
            {slides:[{id:"large-feature",title:title,blurb:blurb,c1:"#333333",c2:"#151515"}]})
        wait(40)
        for (const value of ["FEATURED",title,blurb]) {
            const copy=find(carousel,item=>item["text"]===value)
            verify(copy!==null)
            const point=copy.mapToItem(carousel,0,0)
            verify(point.y>=0 && point.y+copy.height<=carousel.height+1,"Large featured copy is clipped")
        }
        const action=find(carousel,item=>item.objectName==="featuredPrimaryAction_0")
        const point=action.mapToItem(carousel,0,0)
        verify(point.y>=0 && point.y+action.height<=carousel.height+1)
    }
}
