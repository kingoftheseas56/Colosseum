// Run with node tests/test_vault_library_data.js. Pure projection, no QML context.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const data = {};
vm.createContext(data);
vm.runInContext(fs.readFileSync('qml/VaultLibraryData.js', 'utf8').replace(/^\.pragma library\s*/, ''), data);
function row(key, identity, kind = 'video', away = false) {
    return data.normalize({key, kind, identityId: identity, title: identity ? 'Matched title' : 'raw.1080p.mkv'},
        [{id: 'vault:' + key, identityId: identity, identityTitle: identity ? 'Matched title' : '', kind, path: '/' + key, away}], {}, false);
}
assert.equal(row('a', '').identified, false);
assert.equal(row('a', '').title, 'Needs a match');
const merged = data.deduplicate([row('a', 'imdb:tt1', 'video', true), row('b', 'imdb:tt1')]);
assert.equal(merged.length, 1);
assert.equal(merged[0].copies.length, 2);
assert.equal(merged[0].offline, false);
assert.equal(merged[0].path, '/b');
assert.equal(data.search(merged, '/b').length, 1);
const pg = {get: (_, id) => ({id, progress: .4, updatedAt: 7}), recent: () => []};
assert.equal(data.joinProgress(row('a', 'imdb:tt1'), pg, {}).hasProgress, undefined);
assert.equal(data.continuing([data.joinProgress(row('a', 'imdb:tt1'), pg, {'vault:a':'Admitted'})]).length, 1);
const show = data.normalize({key:'show', kind:'video', identityId:'imdb:tt2', title:'Show'}, [],
    {seasons:[{key:'season1', supplemental:false}]}, false);
assert.equal(show.kind, 'show');
const film = data.normalize({key:'movie',kind:'video',identityId:'imdb:tt3',title:'Movie'}, [],
    {seasons:[{key:'', supplemental:false}, {key:'Extras',supplemental:true}]}, false);
assert.equal(film.kind, 'movie');
const remote = data.serverRow({id:'tt2',title:'Server show',kind:'series'}, {id:'server'});
const serverProgress = {get:()=>({}),recent:()=>[{id:'tt2:1:3',progress:.5,updatedAt:9,resume:{infoHash:'home-server:server:item'}}]};
assert.equal(data.continuing([data.joinProgress(remote,serverProgress)]).length,1);
assert.equal(data.joinProgress(remote,{get:()=>({}),recent:()=>[{id:'tt2:1:3',progress:.5,resume:{infoHash:'home-server:other:item'}}]}).hasProgress,undefined);
assert.equal(data.continuing([data.joinProgress(row('book','epub:1','book'), {get:(_,id)=>({id,progress:1,finished:true})})]).length,0);
console.log('Vault library projection tests passed');
// Native series.title is filename fallback, NEVER title provenance.
const orphan = data.normalize({key:'bad',kind:'video',identityId:'imdb:tt8',title:'Raw.1080p.mkv'},
    [{id:'vault:bad',identityId:'imdb:tt8',identityTitle:'',displayTitle:'Raw.1080p.mkv'}],{},false);
assert.equal(orphan.identified,false); assert.equal(orphan.title,'Needs a match');
const suppressed = data.normalize({key:'bad',kind:'video',title:'Raw'},
    [{id:'vault:bad',identityId:'imdb:tt8',identityTitle:'Curated',identitySuppressed:true}],{},false);
assert.equal(suppressed.identified,false);
const epubFallback = data.normalize({key:'book',kind:'book',title:'file-name'},
    [{id:'vault:book',metadataSource:'EPUB',displayTitle:'file-name',title:'file-name'}],{},false);
assert.equal(epubFallback.identified,false);
const nativeBook = data.normalize({key:'book',kind:'book',title:'wrong-folder',identityId:'epub:book'},
    [{id:'vault:book',kind:'book',path:'/book.epub',identityId:'epub:book',identityTitle:'Dune',author:'Frank Herbert',synopsis:'A desert world.'}],{},false);
assert.equal(nativeBook.title,'Dune'); assert.equal(nativeBook.blurb,'A desert world.');
const bookProgress = data.joinProgress(nativeBook,{get:(kind,id)=>{assert.equal(kind,'book');return {id,progress:.38,updatedAt:10,resume:{path:'/book.epub',book:{id}}}}});
assert.equal(bookProgress.progressText,'38%'); assert.equal(bookProgress.resumePath,'/book.epub');
const nativeComic = data.normalize({key:'comic',kind:'comic',identityId:'comics:1'},
    [{id:'vault:comic',kind:'comic',path:'/comic.cbz',pages:100,identityId:'comics:1',identityTitle:'Comic'}],{},false);
const comicProgress=data.joinProgress(nativeComic,{get:(kind,id)=>{assert.equal(kind,'comic');return {id,progress:.42,resume:{chapterId:'/comic.cbz',page:42,finished:false}}}});
assert.equal(comicProgress.progressText,'Page 42 of 100'); assert.equal(comicProgress.resumePath,'/comic.cbz');
const showFiles=[{id:'vault:ep1',kind:'video',path:'/ep1',identityId:'imdb:tt9',identityTitle:'Show',durationSec:3600},
    {id:'vault:ep2',kind:'video',path:'/ep2',identityId:'imdb:tt9',identityTitle:'Show',durationSec:3600}];
const nativeShow=data.normalize({key:'show',kind:'video',identityId:'imdb:tt9'},showFiles,
    {seasons:[{key:'season1',supplemental:false,episodes:[{id:'vault:ep1',season:1,episode:1},{id:'vault:ep2',season:1,episode:2}]}]},false,
    {runtimeText:'1h',ratingText:'IMDb 8.8',genresLine:'Drama, Crime',year:2002});
assert.equal(nativeShow.imdbRating,8.8);assert.equal(nativeShow.runtimeText,'1h');assert.equal(nativeShow.genres,'Drama, Crime');assert.equal(nativeShow.year,2002);
const showProgress=data.joinProgress(nativeShow,{get:(_,id)=>({id,progress:.5,updatedAt:id.endsWith('2')?20:10,resume:{position:1800}}),watchedMark:id=>id.endsWith('1')?1:0}, {'vault:ep1':'Admitted','vault:ep2':'Admitted'});
assert.equal(showProgress.resumePath,'/ep2');assert.equal(showProgress.progressText,'S1 E2 · 30 min left');assert.equal(showProgress.completed,false);
const watchedShow=data.joinProgress(nativeShow,{get:()=>({}),watchedMark:()=>1},{});
assert.equal(watchedShow.completed,true);
const watchedMovie=data.joinProgress(row('a','imdb:tt1'),{get:()=>({}),watchedMark:()=>1},{});
assert.equal(watchedMovie.completed,true);assert.equal(data.continuing([watchedMovie]).length,0);
const disconnected=data.serverRow({id:'tt2',title:'Server show',kind:'series'},{id:'server',needsReconnect:true});
assert.equal(disconnected.offline,true);
const hiddenServerProgress={get:()=>({}),recent:()=>[{id:'tt2:1:3',progress:.5,updatedAt:9,_homeServerHidden:true,resume:{infoHash:'home-server:server:item'}}]};
assert.equal(data.continuing([data.joinProgress(disconnected,hiddenServerProgress)]).length,0);
console.log('Native-shape title, metadata, reader progress and watched tests passed');

// Copy B keeps the title online, but Resume must retain unavailable copy A's position.
const mixedCopies=data.deduplicate([row('offline-copy','imdb:tt55','video',true),row('online-copy','imdb:tt55','video',false)])[0];
const mixedProgress=data.joinProgress(mixedCopies,{get:(_,id)=>({id,progress:.5,updatedAt:id==='vault:offline-copy'?20:10})},{'vault:offline-copy':'Admitted','vault:online-copy':'Admitted'});
assert.equal(mixedProgress.offline,false);assert.equal(mixedProgress.path,'/online-copy');
assert.equal(mixedProgress.resumePath,'/offline-copy');assert.equal(mixedProgress.resumeOffline,true);
console.log('Exact resume copy availability test passed');
