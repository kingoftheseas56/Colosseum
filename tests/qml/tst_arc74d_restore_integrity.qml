// Arc 74-D: actual ComicReaderShell + controlled Progress and page-store seams.
// No real profile or registry writes; fake owner preserves exact payloads.
import QtQuick
import QtTest

TestCase {
    id: test
    name: "Arc74DRestoreIntegrity"

    component FakeProgress: QtObject {
        property var entries: ({})
        property int writes: 0
        function key(kind, seriesId) { return String(kind) + "|" + String(seriesId) }
        function get(kind, seriesId) { return entries[key(kind, seriesId)] || ({}) }
        function mangaChapterResume(seriesId, chapterNumber) {
            var resume = (get("manga", seriesId).resume || ({}))
            return String(resume.chapterNumber) === String(chapterNumber) ? resume : ({})
        }
        function record(entry) {
            writes++
            entries[key(entry.kind, entry.id)] = JSON.parse(JSON.stringify(entry))
        }
    }
    component FakePages: QtObject {
        property var sourcePages: ({})
        property var metadata: []
        function localPages(chapterId) { return sourcePages[String(chapterId)] || [] }
        function downloadedChapters() { return metadata }
        function statusOf(chapterId) { return localPages(chapterId).length ? {state:"done"} : {state:"none"} }
        signal finished(string chapterId)
        signal failed(string chapterId, string reason)
        signal removed(string chapterId)
    }
    component SeriesRecords: QtObject { property string all: "{}" }
    Component { id: progressComp; FakeProgress {} }
    Component { id: pagesComp; FakePages {} }
    Component { id: recordsComp; SeriesRecords {} }
    property var readerComp: null
    property var ownedReaders: []
    function cleanup() {
        // The production Progress owner outlives the QML reader. Destroy the
        // reader first here too; QtTest's default fixture destruction order
        // otherwise creates a fake-store-after-teardown warning.
        for(var i=ownedReaders.length-1;i>=0;--i) {
            var shell=ownedReaders[i]
            if (!shell) continue
            shell.shutdown()
            shell.progress=null
            shell.destroy()
        }
        ownedReaders=[]
    }

    function pages(n) {
        var result=[]
        for(var i=0;i<n;i++) result.push({index:i,url:"file:///nonexistent/arc74d/p"+i+".png",group:-1})
        return result
    }
    function saved(kind, series, chapter, position, fraction) {
        return {kind:kind,id:series,title:"Saved Portuguese manga",sub:"Chapter 47",cover:"",
                progress:position/5,
                resume:{chapterId:chapter,chapterNumber:"47",page:position,maxSeen:position,
                    scrollFrac:.81,pageFraction:fraction,finished:false}}
    }
    function progWith(entries) {
        return createTemporaryObject(progressComp,test,{entries:entries})
    }
    function storeWith(source,available) {
        var map=({}); if(available!==false) map[source]=pages(5)
        return createTemporaryObject(pagesComp,test,{sourcePages:map})
    }
    function makeReader(series,source,kind,progress,pagesRef,layout,chapterNumber) {
        var settings=createTemporaryObject(recordsComp,test,{
            all:JSON.stringify({[series]:{layout:layout||"long_strip"}})})
        var obj=createTemporaryObject(readerComp,test,{
            width:640,height:480,seriesId:series,seriesTitle:"Saved Portuguese manga",
            chapterId:source,chapterLabel:"Chapter 47",entryKind:kind||"manga",
            chapters:chapterNumber === null ? [] : [{id:source,name:"Chapter 47",number:chapterNumber === undefined ? 47 : chapterNumber}],
            progress:progress,pageStore:pagesRef,seriesRecords:settings,
            recordDebounceMs:20
        })
        verify(obj!==null,"real ComicReaderShell should create")
        ownedReaders.push(obj)
        return obj
    }
    function initTestCase() {
        readerComp=Qt.createComponent("../../qml/comicreader/ComicReaderShell.qml")
        verify(readerComp.status!==Component.Error,readerComp.errorString())
    }

    function test_resume_reconstructs_exact_chapter_page_and_fraction() {
        var entry=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var shell=makeReader("mal:1",entry.resume.chapterId,"manga",progress,
                             storeWith(entry.resume.chapterId,true),"long_strip")
        compare(shell.curChapterId,entry.resume.chapterId)
        compare(shell.currentPage,4)
        compare(shell._pendingPageFraction,.37)
        compare(shell._stripRestorePending,true)
        compare(shell.readerReady,false,"not render-ready merely because saved anchor was loaded")
    }
    function test_pending_restore_minimize_must_not_replace_saved_page() {
        var entry=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var shell=makeReader("mal:1",entry.resume.chapterId,"manga",progress,
                             storeWith(entry.resume.chapterId,true),"long_strip")
        verify(shell._stripRestorePending)
        shell._onPresented(1,0) // provisional mount-time position before the seek
        shell.goMinimize()      // forces an immediate record on the real app path
        var persisted=progress.get("manga","mal:1")
        compare(persisted.resume.page,4,"minimize during restore must preserve page 4")
        compare(persisted.resume.pageFraction,.37,"minimize must preserve within-page anchor")
    }
    function test_pending_restore_hide_must_not_replace_saved_page() {
        var entry=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var shell=makeReader("mal:1",entry.resume.chapterId,"manga",progress,
                             storeWith(entry.resume.chapterId,true),"long_strip")
        shell._onPresented(1,0)
        shell.visible=false
        var persisted=progress.get("manga","mal:1")
        compare(persisted.resume.page,4,"hide during restore must not replace saved page")
    }
    function test_unavailable_saved_download_never_writes_new_position() {
        var entry=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var shell=makeReader("mal:1",entry.resume.chapterId,"manga",progress,
                             storeWith(entry.resume.chapterId,false),"long_strip")
        compare(shell.max,0)
        shell.recordProgress()
        shell.shutdown()
        compare(progress.get("manga","mal:1").resume.page,4)
        compare(progress.writes,0,"missing download should not create progress")
    }
    function test_same_chapter_translation_shares_position_without_early_source_write() {
        var source="tankoyomi:pt:src:chapter47"
        var entry=saved("manga","mal:1",source,4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var en="tankoyomi:en:other:chapter47"
        var shell=makeReader("mal:1",en,"manga",progress,storeWith(en,true),"long_strip")
        compare(shell.currentPage,4,"same numbered chapter shares reading position across translations")
        compare(shell._pendingPageFraction,.37)
        compare(shell.curChapterId,en)
        // Merely constructing a reader for a different translation is NOT
        // evidence that its first page has ever been drawn. The saved Portuguese
        // Continue target must survive until the new source really appears.
        var oldRecord=progress.get("manga","mal:1")
        compare(oldRecord.resume.chapterId,source,
                "opening unpainted English chapter cannot overwrite saved Portuguese source")
        compare(oldRecord.resume.page,4)
        compare(progress.writes,0)
        // After the surface actually presents English, it becomes a legitimate
        // new Continue target. This is the first authorized point for a write.
        shell.manualActivity()
        tryCompare(shell,"_stripRestorePending",false)
        shell._onPresented(4,.37)
        shell.recordProgress()
        var newRecord=progress.get("manga","mal:1")
        compare(newRecord.resume.chapterId,en)
        compare(newRecord.resume.page,4)
        compare(newRecord.resume.pageFraction,.37)
        compare(String(newRecord.resume.chapterNumber),"47")
        compare(progress.writes,1)
    }
    function test_other_chapter_does_not_inherit_translation_position() {
        var source="tankoyomi:pt:src:chapter47"
        var progress=progWith({"manga|mal:1":saved("manga","mal:1",source,4,.37)})
        var en="tankoyomi:en:other:chapter48"
        var shell=makeReader("mal:1",en,"manga",progress,storeWith(en,true),"long_strip",48)
        compare(shell.currentPage,1)
        compare(shell._pendingPageFraction,-1)
        compare(progress.writes,0)
    }
    function test_downloaded_translation_restores_without_live_catalogue() {
        var source="tankoyomi:pt:src:chapter47"
        var progress=progWith({"manga|mal:1":saved("manga","mal:1",source,4,.37)})
        var en="tankoyomi:en:other:opaque-id"
        var pagesRef=storeWith(en,true)
        pagesRef.metadata=[{id:en,seriesId:"mal:1",label:"Chapter 47"}]
        var shell=makeReader("mal:1",en,"manga",progress,pagesRef,"long_strip",null)
        compare(shell.currentPage,4)
        compare(shell._pendingPageFraction,.37)
        compare(progress.writes,0)
    }
    function test_shorter_translation_clamps_shared_page() {
        var source="tankoyomi:pt:src:chapter47"
        var progress=progWith({"manga|mal:1":saved("manga","mal:1",source,4,.37)})
        var en="tankoyomi:en:other:chapter47"
        var pagesRef=storeWith(en,true)
        pagesRef.sourcePages={[en]:pages(3)}
        var shell=makeReader("mal:1",en,"manga",progress,pagesRef,"long_strip")
        compare(shell.currentPage,3,"a shorter translation must not open beyond its last page")
        compare(progress.writes,0,"clamping before paint must preserve the prior source")
    }
    function test_volume_saved_page_and_anchor_are_independent() {
        var manga=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var volume=saved("tankoban","mal:1","tankoban:mal:1:volume5",3,.62)
        var progress=progWith({"manga|mal:1":manga, "tankoban|mal:1":volume})
        var shell=makeReader("mal:1",volume.resume.chapterId,"tankoban",progress,
                             storeWith(volume.resume.chapterId,true),"long_strip")
        compare(shell.curChapterId,volume.resume.chapterId)
        compare(shell.currentPage,3)
        compare(shell._pendingPageFraction,.62)
        compare(shell.progressKind,"tankoban")
        shell._onPresented(1,0)
        shell.goMinimize()
        compare(progress.get("tankoban","mal:1").resume.page,3)
        compare(progress.get("manga","mal:1").resume.page,4)
    }
    function test_volume_namespace_does_not_share_manga_record() {
        var entry=saved("manga","mal:1","tankoyomi:pt:src:chapter47",4,.37)
        var progress=progWith({"manga|mal:1":entry})
        var volume="tankoban:mal:1:volume5"
        var shell=makeReader("mal:1",volume,"tankoban",progress,storeWith(volume,true),"long_strip")
        compare(shell.currentPage,1,"no tankoban record means no chapter namespace contamination")
        compare(shell.progressKind,"tankoban")
    }
}
