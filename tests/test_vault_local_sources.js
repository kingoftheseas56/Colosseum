const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
const data = {}; vm.createContext(data);
vm.runInContext(fs.readFileSync('qml/LocalLibrarySources.js','utf8').replace(/^\.pragma library\s*/,''),data);
const files = [{id:'v1',path:'/one',size:1},{id:'v2',path:'/two',size:2},{id:'extra',path:'/extra'},{id:'offline',path:'/offline',away:true}];
const lib = {series:()=>[{key:'k',identityId:'imdb:tt1'}],items:()=>files,
    admissionById:()=>({v1:'Admitted',v2:'Admitted',extra:'Admitted',offline:'Admitted'}),
    browseDetail:()=>({copies:[{path:'/one'},{path:'/two'},{path:'/offline'}]}),
    seasonFactsForShow:()=>({seasons:[{supplemental:false,episodes:[{path:'/one',season:1,episode:2},{path:'/two',season:1,episode:3}]}]})};
assert.equal(data.sources(lib,'movie','tt1',{}).length,2);
assert.equal(data.sources(lib,'movie','tt2',{}).length,0);
assert.equal(data.sources(lib,'series','tt1',{}).length,0);
assert.equal(data.sources(lib,'series','tt1:1:2',{})[0].localPath,'/one');
assert.equal(data.sources(lib,'series','tt1:1:4',{}).length,0);
assert.equal(data.sources(lib,'movie','tt1',{})[0].addonName,'Your copy');
lib.seasonFactsForShow=()=>({seasons:[{supplemental:false,episodes:[{path:'/one',season:1,episode:2},{path:'/two',season:1,episode:2}]}]});
assert.equal(data.sources(lib,'series','tt1:1:2',{}).length,2);
lib.admissionById=()=>({v1:'Admitted',v2:'RejectedUnsupported'});
assert.equal(data.sources(lib,'series','tt1:1:2',{}).length,1);
console.log('Vault local source tests passed');
