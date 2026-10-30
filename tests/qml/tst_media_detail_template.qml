import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    name:"MediaDetailTemplate"
    when:windowShown
    Window { id:host; width:800; height:1280; visible:true; property bool androidHost:true }
    Component { id:bookComponent; App.BiblioBook { anchors.fill:parent } }
    Component { id:mangaComponent; App.TankobanSeriesView { anchors.fill:parent } }
    Component { id:thumbnailComponent; App.MangaReadingRoomThumbnailMock { anchors.fill:parent } }
    function find(item,name) {
        if (item.objectName===name) return item
        for (const child of item.children || []) { const result=find(child,name); if (result) return result }
        return null
    }
    function test_book_order_data() {
        return [{tag:"narrow",width:354,height:792},{tag:"phone",width:412,height:900},{tag:"tablet",width:800,height:1280}]
    }
    function test_book_order(data) {
        host.width=data.width; host.height=data.height
        const page=createTemporaryObject(bookComponent,host.contentItem,{androidHost:true,booksRef:null,bookTorrentsRef:null,collectionRef:null,progressRef:null})
        page.book={title:"A long fixture title for a shared media detail page",synopsis:"A short synopsis that belongs before the primary Read action.",author:"Fixture author"}
        page.edLoading=false; page.torLoading=false; page.abLoading=false
        wait(60)
        const title=find(page,"biblioBookTitle")
        const read=find(page,"biblioPrimaryRead")
        const cover=find(page,"biblioBookCoverColumn")
        verify(title !== null && read !== null && cover !== null)
        verify(title.mapToItem(page,0,0).y>=cover.mapToItem(page,0,cover.height).y)
        verify(read.mapToItem(page,0,0).y>=title.mapToItem(page,0,title.height).y)
        verify(read.width>=48 && read.height>=48)
        const titleX=title.mapToItem(page,0,0).x
        verify(titleX>=0 && titleX+title.width<=page.width)
    }
    function test_manga_uses_one_vertical_owner_data() {
        return [{tag:"phone",width:354,height:792},{tag:"tablet",width:800,height:1280},{tag:"short",width:792,height:354}]
    }
    function test_manga_uses_one_vertical_owner(data) {
        host.width=data.width; host.height=data.height
        const page=createTemporaryObject(mangaComponent,host.contentItem)
        page.seriesTitle="A manga detail fixture with a longer title"
        page.author="Fixture author"
        page.synopsis="A longer synopsis. ".repeat(30)
        page.loading=false
        page.chapterMode=true
        const chapters=[]
        for (let i=0;i<10;++i) chapters.push({id:"chapter-"+i,name:"Chapter "+(i+1),number:i+1})
        page.chaptersModel=chapters
        wait(80)
        const viewport=find(page,"tankobanDetailScroll")
        const list=find(page,"mangaChapterSeriesView")
        verify(viewport !== null && viewport.visible && viewport.height>0)
        verify(list !== null)
        compare(list.pageFlick,viewport)
        verify(viewport.contentHeight>viewport.height)
        viewport.contentY=viewport.contentHeight-viewport.height
        wait(20)
        const bottom=list.mapToItem(viewport,0,list.height).y
        verify(bottom<=viewport.height+1 && bottom>=0)
    }
    function test_conditional_thumbnail_keeps_grid_virtualized() {
        host.width=354; host.height=792
        const page=createTemporaryObject(thumbnailComponent,host.contentItem,{seriesTitle:"Thumbnail layout fixture",synopsis:"A local synopsis.",service:null,progress:null,downloader:null})
        const rows=[]
        for (let i=1;i<=80;++i) rows.push({id:"volume-"+i,number:i,volumeNumber:i,volumeId:"volume-"+i,title:"Volume "+i,cover:""})
        page.library.volumeRows=rows
        wait(80)
        const viewport=find(page,"thumbnailMangaDetailScroll")
        const grid=find(page,"volumeShelfGrid")
        verify(viewport !== null && viewport.visible)
        verify(grid !== null && !grid.interactive)
        compare(grid.count,80)
        verify(grid.height<=viewport.height)
        verify(page.library.liveVolumeTiles<80)
        viewport.contentY=viewport.contentHeight-viewport.height
        grid.forceLayout()
        tryVerify(function() { return grid.itemAtIndex(79)!==null })
    }
}
