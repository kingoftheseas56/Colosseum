import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const context = {}; vm.createContext(context);
vm.runInContext(fs.readFileSync(new URL('../qml/CollectionsReferences.js', import.meta.url),'utf8').replace(/^\.pragma library\s*/,''), context);
const ref = (world, kind, source, sourceId, editionId = '', payload = {}) => ({world,kind,source,sourceId,editionId,payload,route:'',key:'source-key',mediaId:'',title:'Same title',creator:'Creator',year:'2026'});
const route = (r, vault, identity) => context.sharedReopenReference(r,vault,identity);
const manga=ref('tankoban','manga','mal','mal:13');
assert.equal(route(manga).route,'manga-detail');
assert.equal(manga.route,'','resolution does not mutate retained edition');
assert.equal(route(manga).key,'','recipient registry derives absent canonical identity');
assert.equal(route(ref('tankoban','manga','mal','mal:13:color','')).route,'','edition mismatch unavailable');
assert.equal(route(ref('tankoban','manga','mal','mal:13:color','color')).payload.sourceRequiredMarkers[0],'colored');
assert.equal(route(ref('biblio','book','apple-books','123','audiobook')).payload.book.isAudiobook,true);
assert.equal(route(ref('biblio','book','apple-books','123','abridged')).route,'');
assert.equal(route(ref('theatre','series','theatre','kitsu:13783','',{canonicalSourceId:'tt1234567'})).route,'','sender aliases never teach recipient identity');
assert.equal(route(ref('theatre','series','theatre','kitsu:13783','',{canonicalSourceId:'tt1234567'}),null,{knownTheatreAnchor:()=> 'tt1234567'}).route,'theatre-detail');
assert.equal(route(ref('theatre','series','theatre','kitsu:13783','',{canonicalSourceId:'tt0000001'}),null,{knownTheatreAnchor:()=> 'tt1234567'}).route,'','conflicting sender pivot rejected before native resolver');
assert.equal(route(ref('theatre','series','theatre','tt1234567','',{canonicalSourceId:'tt0000001'})).route,'','direct IMDb identity cannot pivot');
assert.equal(route(ref('theatre','series','theatre','kitsu:13783')).route,'');
assert.equal(route(ref('tankoban','comic','unknown','abc')).route,'');
assert.equal(route(ref('tankoban','comic','getcomics','gc:posts:12','',{posts:['12','34']})).payload.posts.length,2);
assert.equal(route(ref('vinyl','song','youtube-music','video123','release1',{albumId:'ytm:album:MPRE123',browseId:'MPRE123'})).route,'vinyl-song-detail');
assert.equal(route(ref('vinyl','song','youtube-music','video123','release1')).route,'');
assert.equal(route(ref('vinyl','album','youtube-music','album','release',{albumId:'other',browseId:'MPRE123'})).route,'');
const vault=ref('vault','film','imdb','imdb:tt1234');
const detail=key=>({found:true,isFileDetail:false,identitySource:'IMDB',identitySourceId:'imdb:tt1234',key});
assert.equal(route(vault,{series:()=>[{key:'a'}],browseDetail:detail}).payload.vaultKey,'a');
assert.equal(route(vault,{series:()=>[{key:'a'},{key:'b'}],browseDetail:detail}).route,'','ambiguous local editions never guessed');
assert.equal(route(vault).route,'');
assert.equal(route(ref('biblio','book','apple-books','file:/private','ebook')).route,'');
console.log('Shared references resolve exact recipient routes without changing stored editions');

// Equal canonical keys can retain different provider routes in different lists.
// The active kept edition's reference must win, not a global first-seen row.
const adapterSource=fs.readFileSync(new URL('../qml/collections/CollectionsAdapter.qml',import.meta.url),'utf8');
const begin=adapterSource.indexOf('function reference(key)'),end=adapterSource.indexOf('function mediaKind(',begin);
const original={key:'same',source:'theatre',route:'theatre-detail'};
const kept={key:'same',source:'imdb',route:'',sourceId:'imdb:tt1234'};
const adapterContext={adapter:{activeCollectionId:'gift',getCollection:()=>({members:[kept]}),references:{same:original},transientReference:{}}};
vm.createContext(adapterContext);vm.runInContext(adapterSource.slice(begin,end),adapterContext);
assert.equal(adapterContext.reference('same'),kept,'kept gift reference cannot be substituted by another list');

assert.equal(route(ref("tankoban","manga","weebcentral","01J0EXACTSOURCE")).route,"manga-weebcentral-detail");
