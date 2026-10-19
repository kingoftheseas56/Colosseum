import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {test} from 'node:test';
import assert from 'node:assert/strict';
const js=readFileSync(new URL('../qml/ComicsShelfDelivery.js',import.meta.url),'utf8').replace(/^\.pragma.*$/gm,'');
function fixture(){
 const owner=vm.createContext({});vm.runInContext(js,owner);
 const reads=[], key=(method,args)=>JSON.stringify([method,args]);
 const row={title:'A run',year:2020,cover:'https://owned.invalid/hash.webp',coverCandidates:['https://original.invalid/art.jpg'],gcdId:42,locgId:'original-id'};
 const catalog={remoteQueryKey:key,shelf(...args){reads.push(key('shelf',args));return[row];},discoverPageFiltered(...args){reads.push(key('discoverPageFiltered',args));return{items:[row]};}};
 return {owner,catalog,reads,key};
}
test('one exact completion rereads only its shelf and retains peers',()=>{
 const {owner,catalog,reads,key}=fixture(); const before=owner.initial(catalog);assert.equal(reads.length,15);reads.length=0;
 const changed=key('shelf',['publisher','DC',24]);const after=owner.update(catalog,changed,before);
 assert.deepEqual(reads,[changed]);assert.notEqual(after[9],before[9]);
 for(let i=0;i<15;i++)if(i!==9)assert.equal(after[i],before[i]);
 assert.deepEqual(Array.from(after[9].rows[0].coverCandidates),['https://original.invalid/art.jpg']);
});
test('unknown query does no reads or object churn',()=>{
 const {owner,catalog,reads}=fixture();const before=owner.initial(catalog);reads.length=0;
 assert.equal(owner.update(catalog,'peer-detail-query',before),before);assert.equal(reads.length,0);
});
test('curated shelves use the native filtered-page key and retain identity',()=>{
 const {owner,catalog,reads,key}=fixture();const before=owner.initial(catalog);reads.length=0;
 const changed=key('discoverPageFiltered',['popular',[{axis:'format',key:'omnibus'}],false,0,24]);
 const after=owner.update(catalog,changed,before);assert.deepEqual(reads,[changed]);
 assert.equal(after[3].rows[0].locgId,'locg:original-id');assert.equal(after[3].catalogId,'popular');
});

// Exercise the actual world's gate, preserving SQLite priority when an API is configured.
const world=readFileSync(new URL('../qml/TankobanWorld.qml',import.meta.url),'utf8');
const gate=world.slice(world.indexOf('    function prepareComicLanding()'),world.indexOf('    function updateComicGenres()'));
test('world landing gate preserves local SQLite precedence and primes a retained bundle',()=>{
 let reads=0,state={source:'local',status:'missing'};
 const ComicsCatalog={remoteEnabled:true,landingState(){return state;},landing(){reads++;}};
 const ctx=vm.createContext({ComicsCatalog});vm.runInContext(gate,ctx);
 assert.equal(ctx.prepareComicLanding(),true);assert.equal(reads,0);
 state={source:'remote',status:'success',cached:true};assert.equal(ctx.prepareComicLanding(),true);assert.equal(reads,1);
 state={source:'remote',status:'failure',error:'INVALID_LANDING'};assert.equal(ctx.prepareComicLanding(),false);assert.equal(reads,1);
 state={source:'remote',status:'failure',error:'STATIC_NOT_PREPARED'};assert.equal(ctx.prepareComicLanding(),true);
});
