// Modeled host-policy checks; native SavePage serialization is tested separately.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
const source=readFileSync(new URL('../qml/feria/FeriaSessionDeck.qml',import.meta.url),'utf8');
function fn(name) {let start=source.indexOf('function '+name+'(');assert(start>=0);let p=source.indexOf('{',start),depth=1,end=p+1;for(;depth;++end){if(source[end]==='{')++depth;if(source[end]==='}')--depth;}return source.slice(start,end);}
for(const pending of [true,false]) {
 const calls=[];
 const entry={sessionId:'source',retiring:false,browser:{},descriptor:{privateBrowsing:false},closeController:{begin(){calls.push('close');return true;}}};
 const owner={pendingPageSaveForView(){return pending;}};
 const ctx={entries:{source:entry},regularDownloads:owner,privateDownloads:owner,message(id,text){calls.push('message');}};
 if(source.includes('function pageSavePending('))runInNewContext(fn('pageSavePending'),ctx);
 runInNewContext(fn('closeTab')+'; result=closeTab("source")',ctx);
 assert.equal(ctx.result,!pending,'host close must retain a document-dependent pending page save');
 assert.equal(calls.includes('close'),!pending);
}
console.log('PASS modeled host close respects native page serialization lifetime');
