import test from 'node:test';import assert from 'node:assert/strict';import{readFileSync}from'node:fs';import{join}from'node:path';import{createHash}from'node:crypto';
const root=process.env.CATALOGUE_PRECOMPUTED_DIR;
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
test('precomputed envelopes match canonical args path, artifact revision, body hash and response cap',{skip:!root},()=>{
 const report=JSON.parse(readFileSync(join(root,'precompute-report.json'),'utf8'));assert.ok(report.entries.length>=80);
 for(const e of report.entries){const key=createHash('sha256').update(JSON.stringify(canonical(e.args))).digest('hex');assert.ok(e.path.endsWith('/'+key+'.json'));const raw=readFileSync(join(root,e.path));assert.equal(raw.byteLength,e.bytes);assert.ok(raw.byteLength<=4*1024*1024);assert.equal(createHash('sha256').update(raw).digest('hex'),e.sha256);const body=JSON.parse(raw);assert.equal(body.revision,report.revision);assert.equal(body.apiVersion,1);assert.equal(body.method,e.method);assert.ok('result'in body);assert.ok(!(e.dataset==='comics'&&e.method==='discoverPageFiltered'));assert.ok(!(e.dataset==='mal'&&e.method==='discoverPageFiltered'&&e.args[0]==='new-releases'));}
});
