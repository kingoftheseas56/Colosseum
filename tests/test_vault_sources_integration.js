// Exercise actual SourcesSheet JS function bodies without a GUI/backend.
const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const source = fs.readFileSync('qml/SourcesSheet.qml','utf8');
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
const pageContext={adapter:{homeServers:{profileGeneration:2},refresh:()=>refreshes++},flash:()=>notices++,openRow:()=>routes++,viewWorldRequested:()=>routes++};
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
