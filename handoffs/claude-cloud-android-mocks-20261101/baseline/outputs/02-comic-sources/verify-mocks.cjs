/* Isolated local-browser inspection. Requires preinstalled BDC Puppeteer; no installs. */
'use strict';
const fs=require('fs'), path=require('path');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const root=__dirname, reportPath=path.join(root,'captures','verification.json');
const cases=[
  {file:'comic-sources.html',label:'sources-portrait',w:412,h:900,state:'results'},
  {file:'comic-sources.html',label:'sources-narrow',w:354,h:792,state:'results'},
  {file:'comic-sources.html',label:'sources-insets-scale',w:412,h:900,state:'results',scale:1.3,top:24,bottom:24},
  {file:'comic-sources.html',label:'sources-narrow-scale-insets',w:354,h:792,state:'results',scale:1.3,top:24,bottom:24},
  {file:'comic-sources.html',label:'sources-landscape',w:900,h:412,state:'results'},
  {file:'comic-sources.html',label:'sources-short',w:792,h:354,state:'results'},
  {file:'comic-sources.html',label:'sources-desktop',w:1280,h:700,state:'results'},
  {file:'comic-source-confirmation.html',label:'confirmation-portrait',w:412,h:900,state:'weak'},
  {file:'comic-source-confirmation.html',label:'confirmation-narrow',w:354,h:792,state:'weak',scale:1.3,top:24,bottom:24},
  {file:'comic-source-confirmation.html',label:'confirmation-landscape',w:900,h:412,state:'weak'},
  {file:'comic-source-confirmation.html',label:'confirmation-desktop',w:1280,h:700,state:'weak'},
  {file:'comic-source-confirmation.html',label:'ambiguous-portrait',w:412,h:900,state:'ambiguous'},
  {file:'comic-source-confirmation.html',label:'combined-short',w:792,h:354,state:'combined'},
  {file:'comic-source-confirmation.html',label:'incomplete-short',w:792,h:354,state:'incomplete'},
  {file:'comic-sources.html',label:'sources-landscape-scale-insets',w:792,h:354,state:'results',scale:1.3,top:24,bottom:24},
  {file:'comic-sources.html',label:'sources-tablet-portrait',w:800,h:1280,state:'results'},
  {file:'comic-sources.html',label:'sources-tablet-landscape',w:1280,h:800,state:'results'},
  {file:'comic-source-confirmation.html',label:'confirmation-landscape-scale-insets',w:792,h:354,state:'weak',scale:1.3,top:24,bottom:24},
  {file:'comic-source-confirmation.html',label:'combined-short-scale',w:792,h:354,state:'combined',scale:1.3,top:24,bottom:24},
  {file:'comic-source-confirmation.html',label:'incomplete-short-scale',w:792,h:354,state:'incomplete',scale:1.3,top:24,bottom:24},
  {file:'comic-source-confirmation.html',label:'archive-landscape',w:900,h:412,state:'ambiguous'}
];
function makeUrl(test){
  return 'file:///' + path.join(root,test.file).replace(/\\/g,'/') +
    '?capture=1&vw='+test.w+'&vh='+test.h+'&state='+test.state+
    '&scale='+(test.scale||1)+'&top='+(test.top||0)+'&bottom='+(test.bottom||0);
}
async function checkGeometry(page){
  return page.evaluate(()=>{
    const a=document.getElementById('viewport');
    const r=(el)=>{if(!el)return null;const b=el.getBoundingClientRect();return {x:b.left,y:b.top,right:b.right,bottom:b.bottom,w:b.width,h:b.height};};
    const vp=r(a), header=r(a.querySelector('.title-block')), queryEl=a.querySelector('.query-bar'), query=getComputedStyle(queryEl).display==='none'?null:r(queryEl);
    const table=r(a.querySelector('.glass-panel')),back=r(a.querySelector('.back-action'));
    const dlg=a.querySelector('.weak-layer:not(.hidden) .weak-dialog'), dialog=r(dlg);
    const issues=[];
    if(Math.abs(vp.x)>1||Math.abs(vp.y)>1)issues.push('app viewport not at capture origin');
    if(table.x<0||table.right>vp.w+1||table.bottom>vp.h+1)issues.push('glass outside app bounds');
    if(header.bottom>(query?query.y:table.y)+1)issues.push('header overlaps query/table');
    if(query&&query.bottom>table.y+1)issues.push('query overlaps results glass');
    if(table.h<90)issues.push('glass has less than 90px of accessible height');
    if(back.h<44)issues.push('Back hit target below 44px');
    const buttons=Array.from(a.querySelectorAll('button:not(.hidden)')).filter(el=>el.getClientRects().length && getComputedStyle(el).visibility!=='hidden');
    for(const btn of buttons){
      const b=r(btn);
      if(b.w<44||b.h<44)issues.push('undersized button: '+(btn.textContent||btn.getAttribute('aria-label')));
      if(b.x<vp.x-1||b.right>vp.right+1||b.y<vp.y-1||b.bottom>vp.bottom+1)issues.push('button outside viewport: '+(btn.textContent||''));
    }
    const picks=Array.from(a.querySelectorAll('.source-row,.archive-row')).filter(el=>el.getClientRects().length>0);
    for(const row of picks){
      const b=r(row), p=r(row.querySelector('.round-pick'));
      if(b.x<vp.x-1||b.right>vp.right+1)issues.push('source row horizontal overrun');
      if(p.w<44||p.h<44||p.right>b.right+1)issues.push('round action inaccessible');
      if(row.getAttribute('role')!=='button'||row.tabIndex!==0)issues.push('row lacks keyboard activation semantics');
    }
    if(dialog){
      if(dialog.x<vp.x-1||dialog.right>vp.right+1||dialog.y<vp.y-1||dialog.bottom>vp.bottom+1)issues.push('dialog clipped by app');
      const actions=Array.from(dlg.querySelectorAll('button')).map(r);
      for(const b of actions){if(b.x<dialog.x-1||b.right>dialog.right+1||b.y<dialog.y-1||b.bottom>dialog.bottom+1)issues.push('modal action outside dialog');}
      if(actions.length===2){const v=actions[0],w=actions[1];if(v.x<w.right&&w.x<v.right&&v.y<w.bottom&&w.y<v.bottom)issues.push('modal actions overlap');}
    }
    return {issues,viewport:vp,header,query,table,dialog,visibleRowCount:picks.length,buttonCount:buttons.length,hasFonts:document.fonts.check('16px "Colosseum Segoe"')};
  });
}
async function testInteractions(browser){
  const page=await browser.newPage();await page.setViewport({width:412,height:900});
  await page.goto(makeUrl({file:'comic-sources.html',w:412,h:900,state:'results'}),{waitUntil:'load'});
  const outcomes=[];
  const check=async (label,expected)=>{
    const actual=await page.$eval('#stateSelect',el=>el.value);
    outcomes.push({label,expected,actual,pass:actual===expected});
    if(actual!==expected)throw Error(label+': expected '+expected+' got '+actual);
  };
  await page.locator('.source-row:nth-child(2)').click(); await check('weak row opens confirmation','weak');
  await page.locator('[data-action="cancel-weak"]').click(); await check('Go back cancels weak selection','results');
  await page.locator('.source-row:nth-child(2)').click();
  await page.locator('[data-action="confirm-weak"]').click(); await check('Choose anyway inspects pack','inspecting');
  await page.evaluate(()=>{const s=document.getElementById('stateSelect');s.value='ambiguous';s.dispatchEvent(new Event('change',{bubbles:true}));});
  await check('archive selection state','ambiguous');
  await page.locator('.archive-row:nth-child(2)').click();await check('archive choose inspects pack','inspecting');
  await page.evaluate(()=>{const s=document.getElementById('stateSelect');s.value='results';s.dispatchEvent(new Event('change',{bubbles:true}));});
  await page.locator('.source-row:nth-child(1)').click();await check('strong row directly inspects','inspecting');
  await page.evaluate(()=>{const s=document.getElementById('stateSelect');s.value='results';s.dispatchEvent(new Event('change',{bubbles:true}));});
  await page.locator('#queryInput').fill('not-a-matching-release');
  await page.locator('.search-button').click();await check('local nonmatching query shows empty','empty');
  await page.evaluate(()=>{const s=document.getElementById('stateSelect');s.value='results';s.dispatchEvent(new Event('change',{bubbles:true}));});
  await page.focus('.source-row:nth-child(2)');await page.keyboard.press('Enter');await check('keyboard Enter chooses weak source','weak');
  await page.keyboard.press('Escape');await check('keyboard Escape dismisses weak dialog','results');
  await page.locator('.back-action').click();await check('Back closes source overlay','closed');
  await page.close();return outcomes;
}
(async function main(){
  const findings={tool:'Puppeteer (preinstalled in BDC), isolated Chrome',timestamp:new Date().toISOString(),cases:[],interactions:[],externalRequests:[],verdict:'unverified'};
  let browser;
  try{
    browser=await puppeteer.launch({headless:true,executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',
      userDataDir:path.join(root,'captures','.puppeteer-verified'),
      args:['--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-extensions','--disable-sync']});
    for(const c of cases){
      const page=await browser.newPage();await page.setViewport({width:c.w,height:c.h,deviceScaleFactor:1});
      await page.setRequestInterception(true);
      page.on('request',req=>{const u=req.url();if(/^https?:/i.test(u)){findings.externalRequests.push(u);req.abort();}else req.continue();});
      await page.goto(makeUrl(c),{waitUntil:'load',timeout:12000});
      await page.evaluate(()=>document.fonts.ready);
      const result=await checkGeometry(page);
      const screenshotName=(c.label.startsWith('sources-')?'comic-sources-'+c.label.slice(8):c.label.startsWith('confirmation-')?'comic-source-confirmation-'+c.label.slice(13):'comic-source-confirmation-'+c.label)+'.png';
      await page.screenshot({path:path.join(root,'captures',screenshotName),captureBeyondViewport:false,animations:'disabled'});
      findings.cases.push({name:c.label,file:c.file,capture:screenshotName,logicalViewport:[c.w,c.h],state:c.state,scale:c.scale||1,insets:[c.top||0,c.bottom||0],...result});
      console.log(c.label+' '+(result.issues.length?'ISSUES: '+result.issues.join('; '):'PASS'));
      await page.close();
    }
    findings.interactions=await testInteractions(browser);
    findings.verdict=findings.externalRequests.length||findings.cases.some(c=>c.issues.length)||findings.interactions.some(c=>!c.pass)?'needs-review':'passed-local-mock-checks';
  }catch(e){findings.error=String(e&&e.stack||e);findings.verdict='blocked-tooling';console.error(findings.error);}
  finally{if(browser)await browser.close().catch(()=>{});fs.writeFileSync(reportPath,JSON.stringify(findings,null,2));}
  console.log('VERDICT '+findings.verdict+' '+reportPath);
  if(findings.verdict!=='passed-local-mock-checks')process.exitCode=1;
})();

