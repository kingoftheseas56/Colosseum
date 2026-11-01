'use strict';
// Execute the pinned bundle with the real child-process boundary. No module mocks.
const assert=require('assert'),path=require('path');
const {loadBundle}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const bundle=loadBundle(process.argv[2]);
const cp=bundle.require(32),spawn=cp.spawn;let calls=[];
cp.spawn=function(program,args,...rest){calls.push({program,args});return spawn.call(this,program,args,...rest);};
const probe=bundle.require(432),ffprobe=path.resolve(__dirname,'probe_process_fixture.py');
(async()=>{
 const run=(url,timeout=2000)=>probe({mediaURL:'fixture:'+url,ffprobe,timeout});
 await run('a');await run('a');assert.equal(calls.length,1);
 await run('b');await run('c');await run('a');assert.equal(calls.length,3);
 await run('d');await run('a');assert.equal(calls.length,5);
 const first=await run('multiple');assert.equal(first.format.duration,1);
 const errors={};for(const [name,timeout]of [['fail',2000],['malformed',2000],['invalid',2000],['slow',20]]){
 try{await run(name,timeout);throw Error('expected rejection '+name);}catch(e){errors[name]=e.message;}
 }
 assert.equal(errors.fail,'Probe process exited with code: 7');
 assert.equal(errors.malformed,'Probe process failed');assert.equal(errors.invalid,'Probe process failed');
 assert.equal(errors.slow,'Probe process has been terminated with signal: SIGKILL');
 const legacy=bundle.require(305);legacy.init({ffmpeg:'/usr/bin/ffmpeg'});
 const empty=await new Promise((resolve,reject)=>legacy.probeVideo('/tmp/nonexistent-m01-media',(e,r)=>e?reject(e):resolve(r)));
 assert.deepEqual(empty,{streams:[]});
 console.log(JSON.stringify({cache:'FIFO 3; cache hits do not refresh',cacheSpawnCount:5,firstJsonValue:first,errors,legacyMissingInput:empty,args:calls[0].args},null,2));
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{cp.spawn=spawn;});
