import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const file=process.argv[2] || new URL('../../qml/DownloadsPage.qml',import.meta.url);
const source=fs.readFileSync(file,'utf8');
function method(name){const match=source.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));assert.ok(match,name);return '(function('+match[1]+'){'+match[2]+'\n})';}
function setup(state){
 let rows=[{id:'owned',state}],completed=[],now=100,cleanup=0,enumerations=0,finish=0,deleted=[];
 const context={waitingForDownloadCancellation:true,downloadCancellationDeadline:200,downloadDeletionErrors:[],downloadDeletionQueue:[],worlds:[],Date:{now:()=>now},qsTr:s=>s,
  downloadsApi:{activeJobs:()=>[],removeIncompleteDownloads:()=>{cleanup++;return {success:true};}},
  audiobooksApi:{activeDownloads:()=>rows,downloadedAudiobooks:()=>{enumerations++;return completed;},deleteAudiobook:id=>{deleted.push(id);return {success:true};},removeIncompleteDownloads:()=>{cleanup++;return {success:true};}},
  finishDeletingAllDownloads:()=>{finish++;}};
 context.isLiveState=vm.runInNewContext(method('isLiveState'),context);
 context.advance=vm.runInNewContext(method('advanceDownloadDeletion'),context);
 return {context,setRows:v=>rows=v,setCompleted:v=>completed=v,setNow:v=>now=v,stats:()=>({cleanup,enumerations,finish,deleted})};
}
for(const phase of ['stopping','finishing']){
 const test=setup(phase);test.context.advance();assert.equal(test.stats().cleanup,0,'cleanup must wait for '+phase+' acknowledgment');assert.equal(test.stats().enumerations,0);assert.equal(test.context.waitingForDownloadCancellation,true);
 test.setRows([]);test.setCompleted([{id:'just-finished'}]);test.context.advance();assert.deepEqual(test.stats().deleted,['just-finished']);test.context.advance();assert.equal(test.stats().finish,1);assert.equal(test.stats().cleanup,2);assert.equal(test.context.downloadDeletionErrors.length,0);
}
const failure=setup('failed');failure.setRows([{id:'old',state:'failed',error:'Cleanup failed (error 13)'}]);failure.context.advance();assert.ok(failure.context.downloadDeletionErrors.includes('Cleanup failed (error 13)'));
const unknown=setup('stopping');unknown.setNow(300);unknown.context.advance();assert.equal(unknown.stats().cleanup,0);assert.equal(unknown.stats().enumerations,0);assert.equal(unknown.stats().finish,1);assert.ok(unknown.context.downloadDeletionErrors[0].includes('still stopping'));
console.log('PASS: actual DownloadsPage drain waits for stopping/finishing; re-enumerates keep completion; reports delete failure/unknown outcome without unsafe cleanup');
