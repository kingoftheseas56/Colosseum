// Exercise actual SourcesSheet JS function bodies without a GUI/backend.
const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const source = fs.readFileSync('qml/SourcesSheet.qml','utf8').replace(/\r\n/g,'\n');
function method(name) {
    const start = source.indexOf('function ' + name + '(');
    assert.ok(start >= 0, name);
    let open = source.indexOf('{', start), depth = 1, i = open + 1;
    for (; depth; ++i) { if (source[i] === '{') ++depth; else if (source[i] === '}') --depth; }
    return source.slice(start,i);
}
let played=[], local=[], downloads=[], hidden=0, refreshed=0;
const a={vaultLocal:true,localPath:'/a',vaultId:'v1',infoHash:'vault-local:v1'}, b={vaultLocal:true,localPath:'/b',vaultId:'v2',infoHash:'vault-local:v2'};
const context={sheet:{mode:'play',vaultLocalRows:[a,b],homeServerRows:[{homeServer:true}],nuvioRows:[],rows:[{streamKind:'Torrent',infoHash:'hash'}],
    visibleRows:[a,b],hide:()=>hidden++,localMediaRequested:p=>local.push(p),playRequested:(...args)=>played.push(args),downloadRequested:r=>downloads.push(r),
    refreshVaultLocalRows:()=>refreshed++,extFilter:'all',qualityFilter:'all'},
    LocalLibrarySources:{sources:()=>[a,b]},qsTr:x=>x};
vm.createContext(context);
for (const name of ['baseRows','activateSource','sourceContextOptions','providerKey','extOptions','warmTopRow']) {
    vm.runInContext(method(name),context);context.sheet[name]=context[name];
}
assert.equal(context.baseRows()[0],a); assert.equal(context.baseRows()[1],b);
context.activateSource(1);
assert.deepEqual(local,['/b']); assert.equal(hidden,1); assert.equal(played.length,0);
context.sheet.mode='download';assert.equal(context.baseRows().length,1);context.activateSource(0);assert.equal(downloads.length,0);
context.sheet.mode='season';context.AddonClient={isSeasonPack:()=>false};assert.equal(context.baseRows().length,0);
context.sheet.mode='play';context.sheet.sourceContextRow=a;assert.equal(context.sourceContextOptions().length,1);
assert.equal(context.providerKey(a),'vault-local'); assert.equal(context.extOptions()[1].key,'vault-local');
context.LocalLibrarySources.sources=()=>[];context.activateSource(0);assert.equal(refreshed,1);assert.equal(local.length,1);
context.activateSource(0,[{streamKind:'Torrent',infoHash:'hash',fileIdx:0}]);
assert.equal(played.length,1);assert.equal(played[0][6].some(r=>r.vaultLocal),false);
assert.ok(method('refreshVaultLocalRows').includes('sheet.smartPlayCancelled = true'));
assert.ok(method('warmTopRow').includes('sheet.vaultLocalRows.length'));
assert.ok(method('copySource').includes('vaultLocal'));assert.ok(method('downloadSource').includes('vaultLocal'));
assert.ok(source.includes('onOpenChanged: if (!sheet.open) {\n        sheet.vaultLocalRows = [];'));
for (const file of ['qml/TheatreSeries.qml','qml/TheatreSeriesView.qml']) {
    const page=fs.readFileSync(file,'utf8');
    assert.ok(page.includes('signal localMediaRequested(string path)'));
    assert.ok(page.includes('onLocalMediaRequested: (path) => page.localMediaRequested(path)'));
}
console.log('Vault source integration tests passed');
// A cached server Continue row cannot route into a different active profile.
const pageSource=fs.readFileSync('qml/VaultLibraryPage.qml','utf8');
const resumeStart=pageSource.indexOf('function resumeRow(');
let resumeEnd=pageSource.indexOf('{',resumeStart)+1, resumeDepth=1;
for (;resumeDepth;resumeEnd++) {if(pageSource[resumeEnd]==='{')resumeDepth++;else if(pageSource[resumeEnd]==='}')resumeDepth--;}
let routes=0, notices=0, refreshes=0;
const Data={};vm.createContext(Data);vm.runInContext(fs.readFileSync('qml/VaultLibraryData.js','utf8').replace(/^\.pragma library\s*/,''),Data);
const pageContext={Data,adapter:{homeServers:{profileGeneration:2},refresh:()=>refreshes++},flash:()=>notices++,openRow:()=>routes++,viewWorldRequested:()=>routes++};
vm.createContext(pageContext);vm.runInContext(pageSource.slice(resumeStart,resumeEnd),pageContext);
pageContext.resumeRow({serverId:'server',profileGeneration:1,progressRecord:{id:'tt1',resume:{position:1}},identity:{identityId:'tt1'}});
assert.equal(routes,0);assert.equal(notices,1);assert.equal(refreshes,1);
pageContext.resumeRow({serverId:'server',profileGeneration:2,kind:'movie',progressRecord:{id:'tt1',resume:{position:1}},identity:{identityId:'tt1'}});
assert.equal(routes,1);
console.log('Server resume profile boundary tests passed');

let localResumeOpens=0;pageContext.openMediaRequested=()=>localResumeOpens++;
pageContext.resumeRow({kind:'movie',offline:false,resumeOffline:true,resumePath:'/offline-copy',path:'/online-copy'});
assert.equal(localResumeOpens,0);assert.equal(notices,2);
pageContext.resumeRow({kind:'movie',offline:false,resumeOffline:false,resumePath:'/online-copy',path:'/online-copy'});
assert.equal(localResumeOpens,1);
console.log('Unavailable exact resume copy routing test passed');

let routedIdentity;
pageContext.viewWorldRequested=identity=>routedIdentity=identity;
for (const [id,provider,number,season,episode] of [['tmdb:tv:123:2:3','tmdbId',123,2,3],['tvdb:tv/456:0:1','tvdbId',456,0,1],['tt123:4:5','imdbId','tt123',4,5]]) {
    pageContext.resumeRow({serverId:'server',profileGeneration:2,kind:'show',progressRecord:{id,resume:{position:77}},identity:{identityId:'show'}});
    const ctx=routedIdentity.resumeSources.context;
    assert.equal(ctx[provider],number);assert.equal(ctx.season,season);assert.equal(ctx.episode,episode);assert.equal(ctx.resumePosition,77);
    if(provider!=='imdbId') assert.equal(ctx.imdbId,undefined);
}
const adapterSource=fs.readFileSync('qml/VaultLibraryAdapter.qml','utf8');
function adapterMethod(name) {
    const start=adapterSource.indexOf('function '+name+'('), open=adapterSource.indexOf('{',start);
    assert.ok(start>=0);let end=open+1,depth=1;
    for(;depth;end++){if(adapterSource[end]==='{')depth++;else if(adapterSource[end]==='}')depth--;}
    return adapterSource.slice(start,end);
}
const nativeContext={Data, library:null, progressStore:{get:()=>({}),recent:()=>[]},homeServers:{profileGeneration:5,
    serverTabs:[{id:'s',state:'cached',dimmed:true,enabled:true}],
    serverLibraries:id=>{assert.equal(id,'s');return [{id:'empty',name:'',kind:'movies',state:'cached',titles:[]},
        {id:'second',name:'Shows',kind:'series',state:'cached',titles:[{id:'tt2',title:'Show',kind:'series'}]},
        {id:'third',name:'Duplicate',titles:[{id:'tt2',title:'Show',kind:'series'}]}]}}};
vm.createContext(nativeContext);
for(const name of ['call','refresh','groupsForServer','rowsForServer','attachExtras','extrasForShow']) vm.runInContext(adapterMethod(name),nativeContext);
nativeContext.refresh();
assert.deepEqual(Array.from(nativeContext.groupsForServer('s'),g=>g.id),['empty','second','third']);
assert.equal(nativeContext.groupsForServer('s')[0].name,'');assert.equal(nativeContext.groupsForServer('s')[0].items.length,0);
assert.equal(nativeContext.rowsForServer('s').length,1);assert.equal(nativeContext.rowsForServer('s')[0].profileGeneration,5);
assert.equal(nativeContext.allItems.length,0);assert.ok(pageSource.includes('groups: root.adapter.groupsForServer(root.currentServer)'));
let attachments=[];
nativeContext.library={attachExtrasToShow:(from,to)=>{attachments.push([from,to]);return true},extrasForShow:key=>[{id:'supplement',supplemental:true,path:'/extras'}]};
assert.equal(nativeContext.attachExtras({nativeKind:'video',seriesKey:'unmatched'},{kind:'show',seriesKey:'show'}),true);
assert.deepEqual(attachments,[['unmatched','show']]);assert.equal(nativeContext.extrasForShow({seriesKey:'show'})[0].supplemental,true);
assert.equal(nativeContext.attachExtras({nativeKind:'book'},{kind:'show'}),false);
console.log('Canonical server Resume, ordered native library groups and explicit extras contract tests passed');
