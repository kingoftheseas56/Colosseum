import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const source=fs.readFileSync(new URL('../../qml/BiblioBook.qml',import.meta.url),'utf8');
function method(name){const owner = ["syncStatus", "onRemoved"].includes(name) ? source.slice(source.indexOf("id: abCol")) : source;const match=owner.match(new RegExp(String.raw`^([ \t]*)function ${name}\(([^)]*)\) \{\r?\n([\s\S]*?)^\1\}`, 'm'));assert.ok(match,name);return '(function('+match[2]+'){'+match[3]+'\n})';}
let state='none', callbacks=[], downloads=0, prefetches=0;
const detail={pairKey:'book',sourceGeneration:1,book:{title:'Book',author:'Author'},localPath:'',abRows:[],loadLibgenEditions(){},loadTorrents(){},refreshLocal(){}};
const abCol={abState:'idle',abActiveSlug:'',abPct:0};
const context={detail,abCol,Qt:{platform:{os:'android'}},Abb:{fetchInfoHash:(slug,cb)=>callbacks.push(cb),resolveAudiobook:(title,author,cb)=>cb({rows:[{slug:'top'}]})},Audiobooks:{statusOf:()=>({state}),isDownloaded:()=>false,downloadAudiobook:()=>downloads++},Stream:{prefetch:()=>prefetches++,warmSource:()=>assert.fail('Android playback engine must not warm')}};
for(const name of ['audiobookBusy','activateAudiobookRow','loadEditions'])detail[name]=vm.runInNewContext(method(name),context);
abCol.syncStatus=vm.runInNewContext(method('syncStatus'),context);
const removed=vm.runInNewContext(method('onRemoved'),context);
detail.activateAudiobookRow({slug:'chosen'});detail.activateAudiobookRow({slug:'chosen'});assert.equal(callbacks.length,1);callbacks.shift()({infoHash:'hash'});assert.equal(downloads,1);
for(const phase of ['queued','stopping','finishing']){state=phase;abCol.abState='idle';detail.activateAudiobookRow({slug:'chosen'});assert.equal(callbacks.length,0);assert.equal(abCol.abState,phase);}
state='none';removed('other');assert.equal(abCol.abState,'finishing');removed('book');assert.equal(abCol.abState,'idle');assert.equal(detail.audioLocal,false);
detail.activateAudiobookRow({slug:'chosen'});assert.equal(callbacks.length,1);detail.sourceGeneration++;callbacks.shift()({infoHash:'stale'});assert.equal(downloads,1);
abCol.abState='idle';detail.activateAudiobookRow({slug:'chosen'});detail.pairKey='new';callbacks.shift()({infoHash:'stale-key'});assert.equal(downloads,1);
abCol.abState='idle';detail.activateAudiobookRow({slug:'chosen'});abCol.abActiveSlug='different';callbacks.shift()({infoHash:'stale-slug'});assert.equal(downloads,1);
detail.loadEditions();callbacks.shift()({infoHash:'android-hash'});assert.equal(detail.abInfoHash,'android-hash');assert.equal(prefetches,0);
context.Qt.platform.os='windows';detail.loadEditions();callbacks.shift()({infoHash:'desktop-hash'});assert.equal(prefetches,1);
detail.loadEditions();detail.sourceGeneration++;callbacks.shift()({infoHash:'late'});assert.equal(detail.abInfoHash,'');
console.log('PASS: actual BiblioBook functions fence late callbacks, preserve pending lookup, block queued/stopping/finishing, clear cancellation for retry, and retain desktop-only prefetch');
