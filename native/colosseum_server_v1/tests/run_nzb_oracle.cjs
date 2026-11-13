'use strict';
const fs=require('fs'),vm=require('vm');
const {extractModules,loadBundle}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const extracted=extractModules(process.argv[2]);
if(extracted.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle identity mismatch');
const cache={};function load(id){if(cache[id])return cache[id].exports;if(id===245)return {err:()=>{}};if(id===10)return {Buffer};const m={exports:{}};cache[id]=m;vm.runInNewContext('('+extracted.modules[id].source+')',{Buffer,console,process})(m,m.exports,load);return m.exports;}
function yenc(bytes){const out=[];for(const c of bytes){let e=(c+42)&255;if([0,10,13,61].includes(e))out.push(61,(e+64)&255);else out.push(e);}let body=Buffer.from(out);if(body[0]===46)body=Buffer.concat([Buffer.from('.'),body]);return Buffer.concat([Buffer.from(`=ybegin line=128 size=${bytes.length} name=movie.mkv\r\n`),body,Buffer.from(`\r\n=yend size=${bytes.length}\r\n`)]);}
const cases=[Buffer.from('abcd'),Buffer.from('ij'),Buffer.from('00010a0d133dffd6e0e304','hex'),Buffer.from([4]),Buffer.from(Array.from({length:256},(_,i)=>i))];
const rows=cases.map(bytes=>{const input=yenc(bytes);const [filename,output]=load(1115)(input);if(!output.equals(bytes))throw Error('oracle mismatch');return {input:input.toString('base64'),filename,hex:output.toString('hex')};});
const bundle=loadBundle(process.argv[2]);
const xmlCases=[
 '<nzb><file subject="&quot;movie.mkv&quot;"><groups><group>test.group</group><group>ignored</group></groups><segments><segment bytes="12" number="2">b</segment><segment bytes="12" number="1">a</segment><segment bytes="3" number="3">c</segment></segments></file></nzb>',
 '<nzb xmlns="http://www.newzbin.com/DTD/2003/nzb"><file subject="name"><groups><group>g</group></groups><segments><segment bytes="4" number="1">a</segment><segment bytes="5" number="3">c</segment></segments></file></nzb>',
 '<nzb><file subject="x"><groups><group>a</group></groups><segments><segment bytes="1" number="1">old</segment><segment bytes="2" number="1">new</segment></segments></file></nzb>'
];
(async()=>{const xml=[];for(const input of xmlCases){const files=await new Promise((resolve,reject)=>bundle.require(504)(input,(err,files)=>err?reject(err):resolve(files)));xml.push({input,files});}
fs.writeFileSync(process.argv[3],JSON.stringify({sha256:extracted.sha256,rows,xml}));console.log('5 pinned M1115 and 3 M504 differential cases generated');})();
