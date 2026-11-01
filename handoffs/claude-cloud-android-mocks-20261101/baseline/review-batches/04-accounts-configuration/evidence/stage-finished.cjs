"use strict";
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const batch=path.resolve(__dirname,'..'),manifest=JSON.parse(fs.readFileSync(path.join(batch,'INPUTS.json'),'utf8'));
const dest=path.join(batch,'finished-mocks');if(fs.existsSync(dest))throw Error('Finished root already exists; inspect before restaging');
fs.mkdirSync(dest);const changes=[];
for(const job of manifest.jobs){const target=path.join(dest,job.task);fs.mkdirSync(target);for(const name of [...job.pages,'index.html','app.js','style.css']){const src=path.join(job.output,name);if(!fs.existsSync(src))continue;const original=fs.readFileSync(src,'utf8'),text=original.replaceAll('../../reference/','../../../../reference/');fs.writeFileSync(path.join(target,name),text);changes.push({file:job.task+'/'+name,assetPrefixAdjusted:original!==text,originalSha256:crypto.createHash('sha256').update(original).digest('hex')});}}
fs.writeFileSync(path.join(__dirname,'STAGING.json'),JSON.stringify({utc:new Date().toISOString(),scope:'Owned finished copies only; source workers and references preserved',files:changes},null,2)+'\n');
console.log(JSON.stringify({files:changes.length,destination:dest}));