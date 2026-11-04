// DOM-adapter unit tests, not browser/runtime acceptance.
const test=require('node:test'), assert=require('node:assert/strict'), vm=require('node:vm'), fs=require('node:fs'), path=require('node:path');
const snapshot=JSON.parse(fs.readFileSync(path.join(__dirname,'../../../../..','tests/collections_sharing/fixtures/mixed-gift-v1.json'),'utf8'));
const share='abcdefghijklmnopqrstuvwxyz012345';
const source=fs.readFileSync(path.join(__dirname,'viewer.js'),'utf8');
class Element {
 constructor(){this.hidden=false;this.value='';this.type='password';this.attrs={};this.events={};this.children=[];this.textContent='';this.dataset={};}
 addEventListener(name,fn){this.events[name]=fn;}
 setAttribute(k,v){this.attrs[k]=v;}
 removeAttribute(k){delete this.attrs[k];}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(...nodes){this.children=nodes;}
 focus(){this.focused=true;}
 click(){return this.events.click?.({preventDefault(){}});}
}
async function setup(response, pathname='/collections/s/'+share){
 const elements=new Map(), window=new Element(), requests=[], downloads=[];
 const document={title:'',body:new Element(),getElementById(id){if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},createElement(){return new Element();},createDocumentFragment(){return new Element();}};
 const URL={createObjectURL(blob){downloads.push(blob);return 'blob:fixture';},revokeObjectURL(){}};
 const timers=[];
 const context={document,window,location:{pathname},URL,Blob,AbortController,TextDecoder,Uint8Array,Object,Number,Set,JSON,Error,
 setTimeout(fn,ms){timers.push({fn,ms});return timers.length;},clearTimeout(){},fetch:async(url,init)=>{requests.push({url,init});const answer=typeof response==='function'?response(requests.length,init):response;return new Response(JSON.stringify(answer.body),{status:answer.status||200});}};
 window.scrollTo=()=>{};
 vm.runInNewContext(source,context);
 await new Promise(resolve=>setImmediate(resolve));
 return {elements,document,window,requests,downloads,get:id=>document.getElementById(id),flush:()=>new Promise(resolve=>setImmediate(resolve))};
}
function envelope(){return {share_id:share,kind:'gift',revision:1,snapshot:structuredClone(snapshot)};}
test('gift opening, ordered members, exact canonical edition export, no provider payload',async()=>{
 const value=envelope();value.snapshot.members[0].canonical_id='tt0133093';value.snapshot.members[0].canonical_source_id='tt0133093';value.snapshot.members[0].payload={private:'must not export'};
 const ui=await setup({body:value});assert.equal(ui.get('opening').hidden,false);assert.equal(ui.get('collection').hidden,true);
 assert.equal(ui.get('members').children[0].children.length,5);
 ui.get('explore').click();assert.equal(ui.get('collection').hidden,false);assert.equal(ui.get('collection-title').focused,true);
 ui.get('save-edition').click();assert.equal(ui.downloads.length,1);const saved=JSON.parse(await ui.downloads[0].text());
 delete value.snapshot.members[0].payload;for(const m of value.snapshot.members){m.album_source_id ||= '';m.album_browse_id ||= '';m.comic_post_ids ||= [];}assert.deepEqual(saved,value);
 assert.equal(ui.requests[0].init.credentials,'omit');assert.equal(ui.requests[0].init.cache,'no-store');
});
test('ordinary collection skips opening and gift retention controls',async()=>{
 const value=envelope();value.kind='collection';const ui=await setup({body:value});assert.equal(ui.get('collection').hidden,false);assert.equal(ui.get('keep-panel').hidden,true);ui.get('save-edition').click();assert.equal(ui.downloads.length,0);
});
test('passphrase remains in request body only and is cleared after response',async()=>{
 const ui=await setup(n=>n===1?{status:403,body:{error:{code:'passphrase_required'}}}:{body:envelope()});
 assert.equal(ui.get('locked').hidden,false);ui.get('passphrase').value='synthetic phrase';ui.get('unlock-form').events.submit({preventDefault(){}});await ui.flush();
 assert.equal(ui.get('opening').hidden,false);assert.equal(ui.get('passphrase').value,'');assert.equal(ui.requests[1].url,`/v1/collections/shared/${share}/open`);assert.equal(JSON.parse(ui.requests[1].init.body).passphrase,'synthetic phrase');
});
test('revoked links and invalid gift schema never expose content or export',async()=>{
 for(const body of [{status:404,body:{}},{body:{...envelope(),revoked:true}},{body:{...envelope(),snapshot:{...snapshot,schema_version:99}}}]){
 const ui=await setup(body);assert.equal(ui.get('failure').hidden,false);assert.equal(ui.get('collection').hidden,true);ui.get('save-edition').click();assert.equal(ui.downloads.length,0);
 }
});
test('reject active image formats and oversized edition body',async()=>{
 for(const cover of ['data:image/svg+xml,<svg/>','https://example.invalid/tracker.png','data:image/png;base64,'+'A'.repeat(350001)]){
 const value=envelope();value.snapshot.presentation.cover_data=cover;const ui=await setup({body:value});assert.equal(ui.get('failure').hidden,false);
 }
 const value=envelope();value.snapshot.name='x'.repeat(2200000);const ui=await setup({body:value});assert.equal(ui.get('failure').hidden,false);
});
test('personal text is rendered literally and pagehide clears retained edition',async()=>{
 const value=envelope();value.snapshot.name='<img src=x onerror=alert(1)>';value.snapshot.members[0].note='<script>alert(1)</script>';
 const ui=await setup({body:value});assert.equal(ui.get('opening-title').textContent,value.snapshot.name);assert.equal(ui.get('opening-title').children.length,0);
 ui.window.events.pagehide();assert.equal(ui.get('opening-title').textContent,'');assert.equal(ui.get('members').children.length,0);ui.get('save-edition').click();assert.equal(ui.downloads.length,0);
});
test('malformed links do not make network requests',async()=>{const ui=await setup({body:envelope()},'/collections/s/short');assert.equal(ui.requests.length,0);assert.equal(ui.get('failure').hidden,false);});
