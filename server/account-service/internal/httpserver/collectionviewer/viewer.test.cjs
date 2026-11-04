// Real Chromium UI tests against a disposable, synthetic HTTP fixture.
// Run: node viewer.test.cjs [evidence-directory]
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');
const id='abcdefghijklmnopqrstuvwxyz012345';
const gift = {share_id:id,kind:'gift',revision:1,protected:false,revoked:false,snapshot:{schema_version:1,name:'For the long way home',description:'Stories for the train, the evening, and the days in between.',presentation:{preset:'midnight',dedication:'A few of my favourites, in the order I wanted to share them with you.\nStart anywhere. Come back whenever.',signature:'From a friend',cover_data:''},members:[{world:'theatre',kind:'film',source:'imdb',source_id:'tt0095327',edition_id:'',title:'A quiet evening',creator:'A favourite filmmaker',year:'1988',note:'Save this one for when you have nowhere else to be.'},{world:'tankoban',kind:'manga',source:'mal',source_id:'2',edition_id:'color',title:'The next adventure',creator:'A favourite storyteller',year:'1997',note:'The colour edition we talked about.'},{world:'biblio',kind:'book',source:'apple-books',source_id:'12345',edition_id:'ebook',title:'One more chapter',creator:'An old favourite',year:'2010',note:'I think you’ll like the ending.'}]}};
gift.snapshot.members.forEach(m => {m.canonical_id='';m.canonical_source_id='';m.album_source_id='';m.album_browse_id='';m.comic_post_ids=[];});
let mode='gift', calls=0, receivedPassphrase='';
const api = (res, status, value) => {res.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(value));};
const server = http.createServer(async(req,res)=>{
 if (process.env.VIEWER_FIXTURE_SERVER === '1' && req.url.startsWith('/__fixture/')) { mode=req.url.slice('/__fixture/'.length); res.end(mode); return; }
 if (req.url.startsWith('/v1/')) {
  ++calls;
  if(mode==='offline') {res.destroy();return;}
  if(mode==='revoked') return api(res,404,{error:{code:'publication_not_found'}});
  if(mode==='rate') return api(res,429,{error:{code:'rate_limited'}});
  if(mode==='protected') {
   let body='';for await(const chunk of req)body+=chunk;
   receivedPassphrase=body?JSON.parse(body).passphrase:'';
   if(receivedPassphrase!=='train ride')return api(res,403,{error:{code:body?'passphrase_invalid':'passphrase_required'}});
  }
  const value=structuredClone(gift);
  if(mode==='collection'||mode==='empty'){value.kind='collection';value.revision=2;}
  if(mode==='empty')value.snapshot.members=[];
  if(mode==='malicious'){value.snapshot.name='<img src=x onerror="window.XSS=1">';value.snapshot.members[0].note='<script>window.XSS=2</script>';value.snapshot.members[0].title='<a href="javascript:alert(1)">Title</a>';}
  if(mode==='badcover')value.snapshot.presentation.cover_data='data:image/svg+xml,<svg onload="alert(1)"/>';
  if(mode==='huge')value.snapshot.name='x'.repeat(2300000);
  return api(res,200,value);
 }
 const name=req.url==='/collections/assets/viewer.js'?'viewer.js':req.url==='/collections/assets/viewer.css'?'viewer.css':req.url.startsWith('/collections/s/')?'index.html':null;
 if(!name){res.writeHead(404);res.end();return;}
 res.writeHead(200,{'Content-Type':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html','Cache-Control':'no-store','Content-Security-Policy':"default-src 'none'; script-src 'self'; style-src 'self'; img-src data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"});
 res.end(fs.readFileSync(path.join(__dirname,name)));
});
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 if(process.env.VIEWER_FIXTURE_SERVER === '1'){ console.log(`FIXTURE http://127.0.0.1:${server.address().port}/collections/s/${id}`);return;}
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
 try {
  const context=await browser.newContext({viewport:{width:1366,height:1000},acceptDownloads:true});
  const page=await context.newPage(); const errors=[];page.on('pageerror',error=>errors.push(error.message));
  const url=`http://127.0.0.1:${server.address().port}/collections/s/${id}`;
  await page.goto(url); await page.locator('#opening').waitFor({state:'visible'});
  assert.equal(await page.locator('#collection').isVisible(),false);
  await page.locator('#explore').focus();await page.keyboard.press('Enter');
  await page.locator('#collection').waitFor({state:'visible'});assert.equal(await page.locator('.member').count(),3);
  assert.equal(await page.locator('#collection-title').evaluate(el=>el===document.activeElement),true);
  assert.equal(await page.evaluate(()=>localStorage.length+sessionStorage.length),0);
  const evidence=process.argv[2];if(evidence){fs.mkdirSync(evidence,{recursive:true});await page.screenshot({path:path.join(evidence,'browser-desktop.png'),fullPage:true});}
  const downloadPromise=page.waitForEvent('download');await page.locator('#save-edition').click();const download=await downloadPromise;
  const downloaded=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.deepEqual(downloaded,{share_id:gift.share_id,kind:gift.kind,revision:gift.revision,snapshot:gift.snapshot});
  await page.locator('#read-dedication').click();assert.equal(await page.locator('#opening').isVisible(),true);
  await page.setViewportSize({width:390,height:844});
  if(evidence)await page.screenshot({path:path.join(evidence,'browser-mobile-opening.png'),fullPage:true});
  await page.locator('#explore').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  if(evidence)await page.screenshot({path:path.join(evidence,'browser-mobile-explore.png'),fullPage:true});
  mode='protected';await page.reload();await page.locator('#locked').waitFor({state:'visible'});
  await page.locator('#passphrase').fill('wrong');await page.locator('#unlock').click();await page.waitForFunction(()=>document.querySelector('#unlock-error').textContent.includes('didn’t'));
  assert.equal(await page.locator('#passphrase').inputValue(),'');assert.equal(await page.locator('#collection').isVisible(),false);
  await page.locator('#passphrase').fill('train ride');await page.locator('#show-passphrase').click();assert.equal(await page.locator('#passphrase').getAttribute('type'),'text');await page.locator('#show-passphrase').click();
  await page.locator('#unlock').click();await page.locator('#opening').waitFor({state:'visible'});assert.equal(receivedPassphrase,'train ride');assert.equal(new URL(page.url()).search,'');
  // Navigating away scrubs protected display, and back must authorize again.
  mode='revoked';await page.goto('about:blank');await page.goBack();await page.locator('#failure').waitFor({state:'visible'});assert.equal(await page.locator('#retry').isVisible(),false);
  for(const [testMode,screen] of [['collection','collection'],['empty','collection'],['rate','failure'],['offline','failure'],['badcover','failure'],['huge','failure']]){
   mode=testMode;await page.goto(url);await page.locator('#'+screen).waitFor({state:'visible'});
   if(mode==='collection'){assert.equal(await page.locator('#keep-panel').isVisible(),false);assert.match(await page.locator('#edition-label').textContent(),/EDITION 2/);}
   if(mode==='empty')assert.equal(await page.locator('#empty').isVisible(),true);
  }
  mode='malicious';await page.goto(url);await page.locator('#explore').waitFor({state:'visible'});await page.locator('#explore').click();assert.equal(await page.evaluate(()=>window.XSS),undefined);assert.equal(await page.locator('#members script,#members a,#collection-title img').count(),0);
  assert.match(await page.locator('#collection-title').textContent(),/<img/);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.goto(url.replace(id,'bad'));await page.locator('#failure').waitFor({state:'visible'});assert.equal(await page.locator('#retry').isVisible(),false);
  assert.deepEqual(errors,[]);
  console.log(JSON.stringify({status:'PASS',browser:await browser.version(),apiRequests:calls,journeys:['gift-open-explore','keyboard-focus','desktop-mobile-layout','edition-file-export','passphrase-wrong-correct','revocation-back-navigation','ordinary-revision','empty','rate-limit','offline','unsafe-cover','response-size','text-injection','invalid-link','no-persistent-browser-storage']},null,2));
 }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
