import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source = name => fs.readFileSync(new URL('../qml/'+name,import.meta.url),'utf8').replace(/^\.(pragma|import).*$/gm,'');
function signal() {const f=new Set();return {connect:x=>f.add(x),disconnect:x=>f.delete(x),emit:(...a)=>[...f].forEach(x=>x(...a)),size:()=>f.size};}
function setup() {
    const delivery=vm.createContext({console});vm.runInContext(source('CatalogDelivery.js'),delivery);
    const context=vm.createContext({console,CatalogDelivery:delivery,XMLHttpRequest:function(){throw Error('Unexpected provider network request');}});
    vm.runInContext(source('WorldSearch.js'),context);return context;
}
function owner(rows, async=false) {
    let state={source:'local',key:'',enabled:false,status:'success',cached:false};
    const engine={available:()=>true,search:()=>rows,searchState:()=>({...state}),
        revisionChanged:signal(),readyChanged:signal(),remoteQueryChanged:signal(),
        complete(value=rows){rows=value;state={source:'remote',key:'native-exact-search-key',enabled:true,status:'success',cached:true};this.remoteQueryChanged.emit(state.key);},
        fail(cached=false){state={source:'remote',key:'native-exact-search-key',enabled:true,status:'failure',cached,error:'fixture failure'};this.remoteQueryChanged.emit(state.key);}};
    if(async)state={source:'remote',key:'native-exact-search-key',enabled:true,status:'pending',cached:false};
    return engine;
}
const manga=[{mal_id:13,title:'One Piece',coverUrl:'https://provider.test/13.jpg',type:'Manga'}];
test('Tankoban search uses native MAL and preserves title/id routing without provider XHR',()=>{
    const api=setup(),mal=owner(manga),comics=owner([]);let out;
    api.searchFor('Tankoban','one piece',(rows,error)=>{out={rows,error};},comics,{malCatalog:mal});
    assert.equal(out.rows.length,1);assert.equal(out.rows[0].title,'One Piece');assert.equal(out.rows[0].data.title,'One Piece');assert.equal(out.error,'');
});
test('Theatre search maps native IMDb identities and has no serial metadata dependency',()=>{
    const api=setup(),imdb=owner([{tt:'tt0133093',title:'The Matrix',type:'movie',year:1999,rating:8.7}]);let out;
    api.searchFor('Theatre','matrix',(rows,error)=>out={rows,error},null,{imdbCatalog:imdb});
    assert.equal(out.rows[0].data.id,'tt0133093');assert.equal(out.rows[0].group,'Movies');assert.equal(out.error,'');
});
test('exact native key controls completion; unrelated signals cannot release pending search',()=>{
    const api=setup(),mal=owner(manga,true),comics=owner([]);let calls=0,out;
    api.searchFor('Tankoban','one',(rows)=>{++calls;out=rows;},comics,{malCatalog:mal});
    mal.remoteQueryChanged.emit('another-native-key');assert.equal(calls,0);mal.complete();assert.equal(calls,1);assert.equal(out.length,1);assert.equal(mal.remoteQueryChanged.size(),0);
});
test('cancellation removes exact-query subscriptions and suppresses late completion',()=>{
    const api=setup(),mal=owner(manga,true);let calls=0;
    const cancel=api.searchFor('Tankoban','one',()=>++calls,owner([]),{malCatalog:mal});cancel();mal.complete();assert.equal(calls,0);assert.equal(mal.remoteQueryChanged.size(),0);
});
test('unprepared Comics failure preserves healthy manga results with a truthful partial error',()=>{
    const api=setup(),comics=owner([],true);let out;
    api.searchFor('Tankoban','one',(rows,error)=>out={rows,error},comics,{malCatalog:owner(manga)});comics.fail();assert.equal(out.rows.length,1);assert.equal(out.error,'provider unavailable');
});
test('validated stale native search remains usable while reporting failed refresh',()=>{
    const api=setup(),mal=owner(manga,true);let out;
    api.searchFor('Tankoban','one',(rows,error)=>out={rows,error},owner([]),{malCatalog:mal});mal.fail(true);assert.equal(out.rows.length,1);assert.equal(out.error,'provider unavailable');
});
test('native genre browse reuses existing exact-query MAL delivery',()=>{
    const api=setup(),mal=owner(manga);mal.genreEntries=()=>manga;mal.genreEntriesState=()=>({source:'local',status:'success'});let out;
    api.browseGenre('Tankoban','Action',(rows,error)=>out={rows,error},{malCatalog:mal});assert.equal(out.rows.length,1);assert.equal(out.error,'');
});
test('short native search finishes empty without opening a provider request',()=>{
    const api=setup();let out;api.searchFor('Theatre','a',(rows,error)=>out={rows,error},null,{imdbCatalog:owner([])});assert.equal(out.rows.length,0);assert.equal(out.error,'');
});
