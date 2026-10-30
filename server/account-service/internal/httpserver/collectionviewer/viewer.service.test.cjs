// Assembled account-service + real Chromium receiving journeys.
// Start a disposable PostgreSQL and real account-service in this same executor
// namespace, then run SERVICE_URL=http://127.0.0.1:PORT node this-file [evidence-dir].
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const {chromium}=require('playwright');
const base=process.env.SERVICE_URL;if(!base||!/^http:\/\/(127\.0\.0\.1|localhost):\d+$/.test(base))throw new Error('SERVICE_URL must name a disposable localhost service');
const evidence=process.argv[2]||'/tmp/collections-service-browser-evidence';fs.mkdirSync(evidence,{recursive:true});
const fixture=JSON.parse(fs.readFileSync(path.join(__dirname,'../../../../..','tests/collections_sharing/fixtures/mixed-gift-v1.json'),'utf8'));
for(const m of fixture.members){m.album_source_id ||= '';m.album_browse_id ||= '';m.comic_post_ids ||= [];}
const op=()=>crypto.randomUUID();const results=[];
async function api(method,url,body,token,expected=200){const res=await fetch(base+url,{method,headers:{'Content-Type':'application/json',...(token?{Authorization:'Bearer '+token}:{})},body:body===undefined?undefined:JSON.stringify(body)});const data=await res.json();assert.equal(res.status,expected,`${method} ${url}: ${JSON.stringify(data)}`);return data;}
async function account(){const data=await api('POST','/v1/accounts',{username:'view'+crypto.randomBytes(5).toString('hex'),password:'Synthetic!'+crypto.randomBytes(18).toString('hex'),device_install_id:op(),device_label:'Disposable browser verification',platform:'linux'},undefined,201);return data.session.access_token;}
(async()=>{
 const owner=await account(),other=await account();
 const create={operation_id:op(),kind:'collection',snapshot:structuredClone(fixture),passphrase:''};
 const ordinary=await api('POST','/v1/collections/publications',create,owner,201);
 const repeat=await api('POST','/v1/collections/publications',create,owner,201);assert.equal(repeat.share_id,ordinary.share_id);results.push('create-retry-idempotent');
 const gift=await api('POST','/v1/collections/publications',{operation_id:op(),kind:'gift',snapshot:fixture,passphrase:'quiet sea'},owner,201);
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try{
  const context=await browser.newContext({viewport:{width:1366,height:1000},acceptDownloads:true});const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/collections/s/'+ordinary.share_id);await page.locator('#collection').waitFor({state:'visible'});assert.equal(await page.locator('.member').count(),fixture.members.length);results.push('anonymous-mixed-collection');
  const edits=structuredClone(fixture);edits.name='A new evening';edits.members[0].note='Changed privately, until publishing.';
  await page.reload();await page.locator('#collection').waitFor({state:'visible'});assert.equal(await page.locator('#collection-title').textContent(),fixture.name);results.push('private-edits-not-published');
  const updated=await api('PUT','/v1/collections/publications/'+ordinary.share_id,{operation_id:op(),expected_revision:1,snapshot:edits,passphrase:''},owner);assert.equal(updated.revision,2);
  await page.reload();await page.locator('#collection').waitFor({state:'visible'});assert.equal(await page.locator('#collection-title').textContent(),edits.name);results.push('explicit-publish-changes');
  await page.screenshot({path:path.join(evidence,'service-browser-ordinary-desktop.png'),fullPage:true});
  await api('PUT','/v1/collections/publications/'+ordinary.share_id,{operation_id:op(),expected_revision:2,snapshot:fixture,passphrase:''},other,404);results.push('cross-account-update-denied');
  await page.goto(base+'/collections/s/'+gift.share_id);await page.locator('#locked').waitFor({state:'visible'});
  await page.locator('#passphrase').fill('incorrect');await page.locator('#unlock').click();await page.waitForFunction(()=>document.querySelector('#unlock-error').textContent.includes('didn’t'));
  await page.locator('#passphrase').fill('quiet sea');await page.locator('#unlock').click();await page.locator('#opening').waitFor({state:'visible'});results.push('passphrase-wrong-then-correct');
  await page.screenshot({path:path.join(evidence,'service-browser-gift-opening-desktop.png'),fullPage:true});
  await page.locator('#explore').press('Enter');await page.locator('#collection').waitFor({state:'visible'});assert.equal(await page.locator('#collection-title').textContent(),fixture.name);assert.equal(await page.locator('#custom-cover').isVisible(),true);results.push('gift-original-content-preserved');
  await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.screenshot({path:path.join(evidence,'service-browser-gift-mobile.png'),fullPage:true});
  const downloading=page.waitForEvent('download');await page.locator('#save-edition').click();const download=await downloading;const saved=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.equal(saved.share_id,gift.share_id);assert.deepEqual(saved.snapshot.members,gift.snapshot.members);assert.equal(saved.snapshot.presentation.cover_data,gift.snapshot.presentation.cover_data);fs.writeFileSync(path.join(evidence,'received-gift-edition.json'),JSON.stringify(saved,null,2));results.push('native-import-file-exact-edition');
  const changedGift=structuredClone(fixture);changedGift.name='A later gift edition';await api('POST','/v1/collections/publications',{operation_id:op(),kind:'gift',snapshot:changedGift,passphrase:''},owner,201);
  const original=await api('POST','/v1/collections/shared/'+gift.share_id+'/open',{passphrase:'quiet sea'});assert.equal(original.snapshot.name,fixture.name);results.push('later-gift-does-not-rewrite-original');
  await api('DELETE','/v1/collections/publications/'+gift.share_id,{operation_id:op(),expected_revision:1},other,404);results.push('cross-account-revoke-denied');
  await api('DELETE','/v1/collections/publications/'+gift.share_id,{operation_id:op(),expected_revision:1},owner);
  await page.reload();await page.locator('#failure').waitFor({state:'visible'});assert.equal(await page.locator('#retry').isVisible(),false);results.push('revocation-blocks-browser');
  assert.deepEqual(errors,[]);
  const report={status:'PASS',browser:await browser.version(),tests:results,qualification:'real account-service and browser; native app import/restart is separate',evidence};fs.writeFileSync(path.join(evidence,'browser-service-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
