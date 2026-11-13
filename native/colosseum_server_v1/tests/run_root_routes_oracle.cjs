'use strict';
// Extract only the pinned callbacks. Never execute M564 startup or networking.
const fs=require('node:fs'), vm=require('node:vm'), {spawnSync}=require('node:child_process');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle=extractModules(process.argv[2]);
if(oracle.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
const root=oracle.modules[564].source;
function callback(method,path) {
    const start=root.indexOf(`enginefs.router.${method}("${path}", `);
    if(start<0)throw Error('missing route '+path);
    let p=root.indexOf(',',start)+1;while(/\s/.test(root[p]))p++;
    let depth=0,quote='',escape=false;
    for(let i=p;i<root.length;i++) {
        const c=root[i];if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote='';continue;}
        if(c==='"'||c==="'"||c==='`'){quote=c;continue;}
        if(c==='(')depth++;else if(c===')'&&!--depth)return root.slice(p,i+1);
    }
    throw Error('unterminated callback');
}
let drives=[],networkError=false;
const os={networkInterfaces:()=>{if(networkError)throw Error('controlled network error');return ({eth0:[{address:'192.0.2.8',family:'IPv4',internal:false},{address:'::1',family:'IPv6',internal:true}],eth1:[{address:'10.0.0.4',family:'IPv4',internal:false}]});}};
const proc={platform:'linux',env:{}};
const module414={exports:{}};
vm.runInNewContext('('+oracle.modules[414].source+')',{process:proc,setTimeout(){},clearTimeout(){},console})(module414,module414.exports,id=>({172:{},806:{},32:{exec(cmd,cb){cb(null,'Caption\n'+drives.join('\n'));}},1:{},5:require('node:path'),35:x=>x,22:os})[id]);
const rows=[];
function record(name,path,method='GET',body='',headers={},webUI='https://app.strem.io/shell-v4.4/',winDrives=[],failNetwork=false) {
    networkError=failNetwork;
    drives=winDrives;proc.platform=drives.length?'win32':'linux';
    const settings={extend(v){Object.assign(this,v);},save(cb){cb();}};
    const res={status:200,headers:{},body:'',writeHead(s,h={}){this.status=s;for(const [k,v]of Object.entries(h))this.headers[k.toLowerCase()]=String(v);},end(b=''){this.body=b;}};
    const ctx={os,process:proc,console:{error(){}},settings,cache:{getOptions:module414.exports.getOptions,setOptionValues(){}},enginefs:{baseUrl:'http://192.0.2.8:11470'},webUILocation:webUI};
    const handler=vm.runInNewContext(callback(method.toLowerCase(),path),ctx);
    const formPost=(headers['content-type']||'').startsWith('application/x-www-form-urlencoded');
    const extension=body?(formPost?parse(body,{allowPrototypes:true,arrayLimit:100,depth:Infinity}):JSON.parse(body)):{};
    const req={headers,socket:{constructor:{name:'Socket'}},body:extension};
    handler(req,res,error=>{res.status=500;res.headers={'content-type':'text/plain'};res.body=error;});
    rows.push({name,path,method,body,headers,webUI,drives:winDrives,networkError:failNetwork,formPost,extension,expected:{status:res.status,headers:res.headers,body:res.body}});
}
record('heartbeat','/heartbeat');record('network','/network-info');record('settings-linux','/settings');record('settings-windows','/settings','GET','',{},undefined,['C:\\','E:\\']);
record('settings-post','/settings','POST','{"btMaxConnections":79,"cacheSize":0}',{'content-type':'application/json'});
record('redirect','/','GET','',{host:'localhost:11470'});
record('redirect-query','/','GET','',{host:'[::1]:11470'},'https://example.test/ui?x=1');
record('redirect-encodeURIComponent','/','GET','',{host:"foo!'()*:11470"});record('redirect-no-host','/');
const defaultsStart=root.indexOf('enginefs.getDefaults = function(ih)')+'enginefs.getDefaults = '.length;
const defaultsEnd=root.indexOf('\n        };',defaultsStart)+10;
const trackerModule={exports:{}};vm.runInNewContext('('+oracle.modules[804].source+')')(trackerModule,trackerModule.exports);
for(const [name,patch,noCache] of [
    ['configured',{},false],['disabled-cache',{cacheSize:0},false],['string-zero',{cacheSize:'0'},false],['null-cache',{cacheSize:null},false],['invalid-numbers',{btMaxConnections:-1,btHandshakeTimeout:0,btRequestTimeout:'400',btDownloadSpeedSoftLimit:1.5,btDownloadSpeedHardLimit:null,btMinPeersForStable:false},false],['command-no-cache',{},true]]) {
    const settings={btMaxConnections:55,btHandshakeTimeout:9000,btRequestTimeout:3000,btDownloadSpeedSoftLimit:2000000,btDownloadSpeedHardLimit:3000000,btMinPeersForStable:7,cacheSize:2147483648,...patch};
    const fn=vm.runInNewContext('('+root.slice(defaultsStart,defaultsEnd)+')',{settings,argv:{noCache},defaultTrackers:trackerModule.exports.map(x=>'tracker:'+x),isPositiveInteger:x=>typeof x==='number'&&Number.isInteger(x)&&x>0});
    rows.push({name,action:'defaults',settings,noCache,expected:fn('a'.repeat(40))});
}
const qsCache=new Map();
function qsLoad(id){if(qsCache.has(id))return qsCache.get(id).exports;const m={exports:{}};qsCache.set(id,m);vm.runInNewContext('('+oracle.modules[id].source+')')(m,m.exports,qsLoad);return m.exports;}
const parse=qsLoad(608);
for(const body of [
    'cacheSize=0&btMaxConnections=79',
    'profile[video][codec]=h264&profile[video][width]=1920&tags[]=one&tags[]=two',
    'items[2]=two&items[0]=zero&items[100]=hundred&items[101]=object',
    'x[0][a]=one&x[0][b]=two&x[1][a]=three',
    'x[a]=one&x=two', 'x=one&x[a]=two', 'x[]=one&x[a]=two',
    'x=one&x=two&x[]=three', 'a.b=c&empty&x[]=',
    'encoded%5Bname%5D=hello+world&encoded%5Buri%5D=a%26b%3Dc',
    'x[01]=leading&x[-1]=negative&x[1]=indexed',
    'x[abc]suffix=one&x[abc]=two', '__proto__[polluted]=true&safe=ok',
]) rows.push({name:'form '+body,action:'form',body,expected:parse(body,{allowPrototypes:true,arrayLimit:Math.max(100,body.split('&').length-1),depth:Infinity,parameterLimit:1000})});
record('settings-nested-form','/settings','POST','btMaxConnections=79&profile[video][codec]=h264&tags[]=one&tags[]=two',{'content-type':'application/x-www-form-urlencoded'});
record('network-error','/network-info','GET','',{},undefined,[],true);
fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2)+'\n');
console.log('Pinned M564/M414 root fixtures:',rows.length);
if(process.argv[4]) {const result=spawnSync(process.argv[4],[process.argv[3]],{stdio:'inherit'});process.exit(result.status??1);}
