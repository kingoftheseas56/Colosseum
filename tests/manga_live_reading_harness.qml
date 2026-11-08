// Real series pages and reader shell with a deterministic transport boundary.
import QtQuick
import QtTest
TestCase {
    id: test
    name: "MangaLiveReading"
    when: windowShown
    visible: true
    property bool finished: false
    property string failure: ""
    property var win: ({width:1440,height:900,phoneNarrow:false})
    width: 1440; height: 900
    property var page: null
    property int variant: 0
    property var views: ["TankobanSeriesView.qml", "MangaSeries.qml"]
    QtObject {
        id: manga
        property bool liveReadingEnabled: false
        property string chapterDefaultLanguage: "en"
        signal chapterConfigurationChanged()
        function chapterLanguages() { return [{code:"en",label:"English",providerCount:1}] }
        function chapterCatalogueForProfile(id, profile, language) {}
    }
    QtObject {
        id: extensions
        property int revision: 0
        function installed() { return [{id:"colosseum.well.tankoyomi",enabled:true}] }
        signal changed()
    }
    QtObject {
        id: backend
        property int starts: 0
        property int permanent: 0
        property int stops: 0
        property double token: 0
        property string currentId: ""
        property int count: 0
        property var readyPages: ({})
        property var downloaded: ({})
        property var focuses: []
        signal progress(string id, int done, int total)
        signal finished(string id)
        signal failed(string id, string reason)
        signal removed(string id)
        signal thumbReady(string id, string url)
        signal liveManifestReady(string id, int count, double token)
        signal livePageReady(string id, int index, string url, double token)
        signal liveFailed(string id, int index, string reason, double token)
        function statusOf(id) { return {state: downloaded[id] ? "done" : "none"} }
        function localPages(id) { return downloaded[id] ? [{index:0,url:Qt.resolvedUrl("comicreader_fixtures/page-200x300.png")}] : [] }
        function downloadedChapters() { return [] }
        function fetchThumb(sid,id) { thumbReady(id, "") }
        function downloadChapter(id,sid,title,label) { permanent++ }
        function cancelDownload(id) {}
        function deleteChapter(id) { return {success:true} }
        function startLive(id,sid) { starts++; currentId=id; token++; count=0; readyPages=({}); return token }
        function stopLive(id,number) { if (id===currentId && number===token) { stops++;currentId="" } }
        function setLiveFocus(id,index,number) {
            if (id===currentId && number===token) { var a=focuses.slice();a.push(index);focuses=a }
        }
        function retryLivePage(id,index,number) { if(id===currentId && number===token) deliver(index) }
        function liveStatus(id) { return {state:"reading",total:count,token:token,done:Object.keys(readyPages).length} }
        function livePages(id) {
            if (id!==currentId) return []
            var a=[]
            for(var i=0;i<count;++i) a.push({index:i,url:Qt.resolvedUrl("comicreader_fixtures/page-200x300.png"),pending:!readyPages[i]})
            return a
        }
        function manifest() { count=18; liveManifestReady(currentId,count,token) }
        function deliver(index) { var a=Object.assign({},readyPages);a[index]=true;readyPages=a;livePageReady(currentId,index,String(Qt.resolvedUrl("comicreader_fixtures/page-200x300.png")),token) }
        function reset() { starts=0;permanent=0;stops=0;currentId="";count=0;readyPages=({});downloaded=({});focuses=[] }
    }
    QtObject {
        id: progress
        property var records: []
        signal syncedEntryApplied(string kind, string id, var entry)
        function get(kind,id) { return ({}) }
        function record(value) { var a=records.slice();a.push(value);records=a }
        function mangaChapterResume(sid,number) { return ({}) }
    }
    function ck(ok,why) { if(!ok) throw new Error(views[variant]+": "+why) }
    function find(item,name) {
        if(!item) return null
        if(item.objectName===name) return item
        var a=item.children||[]
        for(var i=0;i<a.length;i++){var v=find(a[i],name);if(v)return v}
        return null
    }
    function abortFlow(error) { failure=String(error);finished=true }
    function begin() {
        try {
            backend.reset();manga.liveReadingEnabled=false
            var c=Qt.createComponent("../qml/"+views[variant])
            ck(c.status===Component.Ready,c.errorString())
            page=c.createObject(test,{width:1280,height:850,mangaEngineRef:manga,downloadsRef:backend,
                                      extensionsRef:extensions,progressRef:progress})
            ck(!!page,"series component constructs")
            page.seriesTitle="Fixture";page.seriesId="fixture-series"
            page.chapterMode=true;page.loading=false
            page.chaptersModel=[{id:"second",number:2,name:"Chapter 2"},{id:"first",number:1,name:"Chapter 1"}]
            Qt.callLater(run)
        } catch(e) { abortFlow(e) }
    }
    function run() {
        try {
            page._readChapter("first","Chapter 1")
            ck(backend.permanent===1 && backend.starts===0,"setting off uses the existing permanent download path")
            page._clearPendingChapterRead()
            manga.liveReadingEnabled=true
            page._readChapter("first","Chapter 1")
            ck(backend.starts===1 && backend.permanent===1,"opt-in Read starts live without a permanent download")
            ck(page.openChapterId==="","reader stays closed before manifest/first page")
            var oldToken=backend.token
            backend.manifest()
            ck(page.openChapterId==="","manifest alone is not readable")
            backend.deliver(2)
            ck(page.openChapterId==="","out-of-order page cannot open the reader")
            page._readChapter("second","Chapter 2")
            backend.livePageReady("first",0,"file:///stale.png",oldToken)
            ck(page.openChapterId==="","superseded chapter cannot open")
            backend.manifest();backend.deliver(0)
            Qt.callLater(afterOpen)
        } catch(e) { abortFlow(e) }
    }
    function afterOpen() {
        try {
            ck(page.openChapterId==="second","exact chapter opens on page one before completion")
            var reader=find(page,"comicReaderShell")
            ck(!!reader,"uses the existing comic reader")
            ck(reader.pageCount===18,"reader retains full manifest length")
            reader.progress=progress
            reader.setLayout("single_page")
            reader.currentPage=11
            ck(backend.focuses.indexOf(10)>=0,"page jump prioritizes current missing page")
            var n=progress.records.length
            reader._onPresented(11,0,false);reader.recordProgress()
            ck(progress.records.length===n,"pending page never advances progress")
            backend.deliver(10)
            reader._onPresented(11,0,true);reader.recordProgress()
            ck(progress.records.length>n,"delivered presented page records normal progress")
            ck(reader.maxSeen>=11,"arrival/presentation advances the high-water mark")
            reader.setLayout("paired_pages")
            reader.currentPage=17
            var pair=find(reader,"doubleSurface")
            ck(!!pair,"existing paired surface is mounted")
            pair.unitShown(18)
            ck(reader.maxSeen===11,"requesting a pending pair cannot advance the reading high-water mark")
            reader.setLayout("single_page")
            reader.goPrevChapter(false)
            ck(backend.currentId==="first","chapter crossing uses live acquisition")
            backend.manifest();backend.deliver(0)
            Qt.callLater(afterCross)
        } catch(e) { abortFlow(e) }
    }
    function afterCross() {
        try {
            ck(page.openChapterId==="first","crossing opens correct next live reader")
            page.openChapterId="";page._clearPendingChapterRead()
            ck(backend.currentId==="","closing stops live transport")
            backend.downloaded=({first:true})
            var n=backend.starts
            page._readChapter("first","Chapter 1")
            ck(backend.starts===n && page.openChapterId==="first","downloaded Read never becomes live")
            page.openChapterId="";backend.downloaded=({})
            page._readChapter("second","Chapter 2")
            var token=backend.token
            page._enterTankobanMode()
            backend.livePageReady("second",0,"file:///late.png",token)
            ck(page.openChapterId==="","leaving Chapter Mode cancels delayed open")
            page.destroy();page=null;variant++
            if(variant<views.length) Qt.callLater(begin)
            else {console.log("MANGA_LIVE_QML_OK");finished=true}
        } catch(e) { abortFlow(e) }
    }
    function test_opt_in_lifecycle_and_progress() {
        begin()
        tryCompare(test, "finished", true, 30000)
        compare(failure, "")
        compare(variant, 2, "both production title pages were exercised")
    }
    function cleanup() {
        if (page) { page.destroy(); page=null }
    }
}
