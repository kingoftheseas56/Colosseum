'use strict';
const fs=require('fs'),vm=require('vm'),crypto=require('crypto');
const bytes=fs.readFileSync(process.argv[2]),sha=crypto.createHash('sha256').update(bytes).digest('hex');
if(sha!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle identity mismatch');
const source=bytes.toString();
const start=source.indexOf('    function sendCORSHeaders(req, res, next) {');const end=source.indexOf('    function sendDLNAHeaders',start);
const helper=source.slice(start,end);
const at=source.indexOf('process.env.NO_CORS || !req.headers.origin || req.headers.origin.match');const stop=source.indexOf('\n',at);
const predicate=source.slice(at,stop);
if(!predicate.endsWith(';'))throw Error('unexpected filter extraction');
const rows=[];
for(const origin of [null,'','https://app.strem.io','https://app.stremio.com','https://appXstremioYcom','https://stremio.github.io','https://evil.test/pathgstaticXcomsuffix','https://evil.test','http://localhost:11470','http://localhost:11471','http://127X0Y0Z1:11470','https://app.strem.io:443','https://APP.STREM.IO','https://prefixpearioXxyzsuffix'])
 for(const method of ['GET','POST','OPTIONS'])for(const env of [null,'','0']){
 const input={origin,method,env,requestedHeaders:method==='OPTIONS'&&rows.length%2?'Range, X-Test':null};
 const output={status:201,body:'handled',headers:{}};const req={method,headers:{}};if(origin!==null)req.headers.origin=origin;if(input.requestedHeaders)req.headers['access-control-request-headers']=input.requestedHeaders;
 const res={setHeader(k,v){output.headers[k.toLowerCase()]=v;},end(){output.status=200;output.body='';}};
 const context={req,res,next(){},process:{env:env===null?{}:{NO_CORS:env}},enginefs:{}};
 vm.runInNewContext(helper+';enginefs.sendCORSHeaders=sendCORSHeaders;'+predicate,context);
 rows.push({input,output});
 }
fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2));console.log(JSON.stringify({oracle_sha256:sha,cases:rows.length,filter_sha256:crypto.createHash('sha256').update(predicate).digest('hex'),helper_sha256:crypto.createHash('sha256').update(helper).digest('hex')}));
