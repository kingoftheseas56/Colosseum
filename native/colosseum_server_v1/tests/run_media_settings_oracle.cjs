'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle=extractModules(process.argv[2]);
if(oracle.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
const result=spawnSync(process.argv[3],[process.argv[4],process.argv[5]],{stdio:'inherit'});
if(result.status!==0)process.exit(result.status??1);
function factory(id,deps){const m={exports:{}};vm.runInNewContext('('+oracle.modules[id].source+')',{process:{env:{}},URL,Buffer,console})(m,m.exports,i=>deps[i]);return m.exports;}
const profiles=factory(193,{}),applyProfile=factory(809,{193:profiles}),bitrate=factory(933,{});
const rows=JSON.parse(fs.readFileSync(process.argv[5],'utf8'));
for(const row of rows){
 const settings=row.settings;
 const args=factory(932,{433:{ffmpeg:class{constructor(o){Object.assign(this,o)}}},933:bitrate,22:{cpus:()=>Array(row.cpuCores).fill(0)},106:settings,412:{applyProfile}});
 const options={...row.options};delete options.args;
 const expected=Array.from(args(options).args,String).concat('pipe:1');
 assert.deepStrictEqual(row.options.args,expected,row.name);
}
console.log('PASS '+rows.length+' actual process argument arrays vs pinned M932 after live settings changes');
