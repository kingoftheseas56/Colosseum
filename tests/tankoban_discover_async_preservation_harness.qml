import QtQuick
import "../qml" as UI
Item {
 id: root; width:1200; height:700
 property var results: ({})
 property int phase:0
 property var failures: []
 property var wall: null
 property int priorGen: 0
 property real priorScroll: 0
 property int mangaOpens:0
 property int comicOpens:0
 function check(ok,label){if(!ok){failures.push(label);console.warn('F1_ASSERT_FAIL '+label)}}
 function find(node,name){if(node.objectName===name)return node;var children=node.children||[];for(var i=0;i<children.length;i++){var hit=find(children[i],name);if(hit)return hit}return null}
 component FakeRemote: QtObject {
  property string cardType:"manga"
  property bool remoteEnabled:true
  property int revision:0
  property var answers: ({})
  property var pending: ({})
  signal requestFailed(string method,string reason)
  signal filterCacheReady()
  function ready(){return true}
  function available(){return true}
  function remoteWaiting(){return Object.keys(pending).length>0}
  function remotePending(){return remoteWaiting()}
  function cachedDiscoverFilters(axis,explicitRows){return discoverFilters(axis,explicitRows)}
  function discoverFilters(axis,explicitRows){
   if(cardType==="manga"&&axis==="genre")return [{value:"Action",count:50}]
   if(cardType==="comics"&&axis==="publisher")return [{key:"Marvel",label:"Marvel",count:50}]
   return []
  }
  function k(cat,filters,offset){
   var f=[]; for(var i=0;i<filters.length;i++)f.push(filters[i].axis+"="+filters[i].key)
   return cat+"|"+f.sort().join(";")+"|"+offset
  }
  function discoverPageFiltered(cat,filters,explicitRows,offset,limit){
   var key=k(cat,filters,offset)
   if(answers[key])return answers[key]
   pending[key]=true
   return {items:[],nextOffset:offset,exhausted:true,freshness:"bundled"}
  }
  function settle(cat,offset,axis,fKey,n,last){
   var fs=axis?[{axis:axis,key:fKey}]:[], key=k(cat,fs,offset),rows=[]
   for(var i=0;i<n;i++){
    var id=offset+i+1
    if(cardType==="manga")rows.push({mal_id:id,title:cat+" manga "+id,type:"Manga",explicit:false})
    else rows.push({locgId:cat+"-"+id,title:cat+" comic "+id,publisher:"Marvel",explicit:false})
   }
   answers[key]={items:rows,nextOffset:offset+n,exhausted:last,freshness:"live"}
   delete pending[key]
   revision++
  }
 }
 FakeRemote{id:mal;cardType:"manga"}
 FakeRemote{id:comics;cardType:"comics"}
 UI.TankobanDiscoverPage{id:p;width:1200;height:700;malCatalog:mal;comicsCatalog:comics;
  onMangaSeriesRequested: function(item){root.mangaOpens++}
  onComicSeriesRequested: function(item){root.comicOpens++}
  _xhrFactory:function(){return {open:function(){},send:function(){},setRequestHeader:function(){}}}}
 function state(){var b=p._shellForTest; return {type:b.currentType,catalog:b.currentCatalogKey,
     items:b.items.length,cursor:b.cursor,loading:b.loading,gen:b.fetchGen,
     filters:b.automationFilterSummary}}
 function save(k){results[k]=state();console.warn("F1_SMOKE "+k+" "+JSON.stringify(results[k]))}
 function finish(){console.warn("F1_ASSERT_RESULT "+JSON.stringify({failures:failures,results:results}));console.warn(failures.length?'F1_COMPREHENSIVE_FAIL':'F1_COMPREHENSIVE_OK');Qt.exit(failures.length)}
 Timer {interval:100;running:true;repeat:true;onTriggered:root.tick()}
 Timer {interval:50000;running:true;onTriggered:{console.warn("F1_SMOKE_TIMEOUT "+JSON.stringify(results));Qt.exit(9)}}
 function tick(){
  var b=p._shellForTest
  if(phase===0){p.applyPin({type:"manga",catalogId:"popular"});save("pinManga");phase++;return}
  if(phase===1){mal.settle("popular",0,"","",24,false);save("mangaPageOne");phase++;return}
  if(phase===2){b.requestPage();save("mangaPageTwoRequested");phase++;return}
  if(phase===3){mal.settle("popular",24,"","",24,false);save("mangaPageTwoSettled");phase++;return}
  if(phase===4){b.selectCatalog("top-rated");save("mangaTopRequested");phase++;return}
  if(phase===5){mal.settle("top-rated",0,"","",24,true);save("mangaTopSettled");phase++;return}
  if(phase===6){
   b.setFilter('Genres','action')
   check(b.loading,'manga filter cold request issued')
   mal.settle('top-rated',0,'genre','action',24,true)
   check(b.currentCatalogKey==='top-rated','manga filter completion preserved catalogue')
   check(b.automationFilterSummary==='Genres=action','manga filter preserved')
   check(b.items.length===24,'manga filtered content landed')
   b.activateIndex(1)
   check(root.mangaOpens===1 && root.comicOpens===0,'manga result opened correct detail')
   p.active=false;p.active=true
   check(b.automationFilterSummary==='Genres=action','manga Back preserved filter')
   p.applyPin({type:"comics",catalogId:"popular"});save("pinComics");phase++;return
  }
  if(phase===7){comics.settle("popular",0,"","",24,false);save("comicsPageOne");phase++;return}
  if(phase===8){b.requestPage();save("comicsPageTwoRequested");phase++;return}
  if(phase===9){comics.settle("popular",24,"","",24,false);save("comicsPageTwoSettled");phase++;return}
  if(phase===10){b.selectCatalog("most-stocked");save("comicsMostRequested");phase++;return}
  if(phase===11){comics.settle("most-stocked",0,"","",24,true);save("comicsMostSettled");phase++;return}
  if(phase===12){
   check(b.currentCatalogKey==='most-stocked','comics catalogue retained after cold selection')
   b.setFilter('Publishers','marvel');save('comicsFilterRequested');phase++;return
  }
  if(phase===13){comics.settle('most-stocked',0,'publisher','marvel',24,true);save('comicsFilterSettled');phase++;return}
  if(phase===14){
   check(b.currentCatalogKey==='most-stocked','comics filter completion did not reset catalogue')
   check(b.automationFilterSummary==='Publishers=marvel','comics filter completion retained filter')
   wall=find(p,'tankobanDiscoverWall');check(!!wall,'real poster wall exists')
   if(wall)wall.contentY=100
   priorScroll=wall?wall.contentY:0
   priorGen=b.fetchGen;comics.revision++;mal.filterCacheReady();save('metadataRevision');phase++;return
  }
  if(phase===15){
   check(b.fetchGen===priorGen,'unrelated revision did not restart fetch')
   check(b.currentCatalogKey==='most-stocked','unrelated revision retained catalogue')
   check(b.automationFilterSummary==='Publishers=marvel','unrelated revision retained filters')
   if(wall)check(wall.contentY===priorScroll,'unrelated revision retained scroll')
   b.activateIndex(1)
   check(root.comicOpens===1 && root.mangaOpens===1,'comics result opened correct detail')
   p.active=false;p.active=true;save('returnFromDetail');phase++;return
  }
  if(phase===16){
   check(b.currentCatalogKey==='most-stocked','Back retained user catalogue')
   check(b.automationFilterSummary==='Publishers=marvel','Back retained filters')
   if(wall)check(wall.contentY===priorScroll,'Back retained scroll')
   p.applyPin({type:'manga',catalogId:'popular'});save('genuineNewPin');phase++;return
  }
  if(phase===17){
   check(b.currentType==='manga'&&b.currentCatalogKey==='popular','genuine new pin navigates')
   b.requestPage();b.requestPage();check(b.loading,'superseded request was genuinely cold');save('stalePageRequested');phase++;return
  }
  if(phase===18){
   p.applyPin({type:'comics',catalogId:'popular'});save('supersedingPin');
   mal.settle('popular',48,'','',24,true);save('staleMangaDelivered');phase++;return
  }
  if(phase===19){
   check(b.currentType==='comics'&&b.currentCatalogKey==='popular','stale reply could not supersede newer pin')
   check(b.items.every(function(it){return it.type==='comics'}),'stale manga rows not merged into comics')
   p.extensions=[{manifest:{id:'f1',name:'Test',catalogs:[{type:'comics',id:'special',name:'Special',isDiscoverable:true}]}}]
   p.applyPin({type:'comics',catalogId:'ext:f1:special'});save('extensionPin');phase++;return
  }
  if(phase===20){
   check(b.currentCatalogKey==='ext:f1:special','extension catalogue pin selected')
   p.extensions=[];save('extensionRemoved');phase++;return
  }
  if(phase===21){
   check(b.currentCatalogKey==='popular','extension removal fallback retained')
   check(results.mangaPageTwoSettled&&results.mangaPageTwoSettled.items===48,'manga page two appended')
   check(results.comicsPageTwoSettled&&results.comicsPageTwoSettled.items===48,'comics page two appended')
   check(results.mangaTopSettled&&results.mangaTopSettled.catalog==='top-rated','manga top-rated retained')
   check(results.comicsMostSettled&&results.comicsMostSettled.catalog==='most-stocked','comics most-stocked retained')
   finish();phase++;return
  }
 }
}