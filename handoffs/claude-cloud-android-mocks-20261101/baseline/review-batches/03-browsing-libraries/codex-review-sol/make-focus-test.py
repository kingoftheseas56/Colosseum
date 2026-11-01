from pathlib import Path
p=Path(__file__).parent
s=(p/'test-search-correction.cjs').read_text(encoding='utf-8')
a=s.index('for(const [lane,query]');z=s.index('await context.close();',a)
s=s[:a]+r'''
result.lane='Biblio';result.viewport=[1280,700];await page.setViewport({width:1280,height:700,deviceScaleFactor:1});
await page.goto(result.origin+'/review-batches/03-browsing-libraries/codex-review-sol/candidate/search.html?capture=1&preset=desktop&lane=Biblio',{waitUntil:'networkidle0'});
for(const label of ['Minimize','Fullscreen','Close'])for(const method of ['pointer','keyboard']){
 await replace('Sherlock');await wait(350);await page.keyboard.type(' ');
 if(method==='pointer')await page.click('.sys-controls [aria-label="'+label+'"]');
 else for(let i=0;i<10;i++){await page.keyboard.press('Tab');if(await page.evaluate(label=>document.activeElement.getAttribute('aria-label')===label,label))break;}
 const before=await page.evaluate(()=>({label:document.activeElement.getAttribute('aria-label'),action:document.activeElement.dataset.action}));
 await check(method+' exact '+label+' focus across debounce',async()=>{await wait(350);let after=await page.evaluate(()=>({label:document.activeElement.getAttribute('aria-label'),action:document.activeElement.dataset.action}));return {pass:before.label===label&&after.label===label,before,after}});
}
await check('short query smoke preserves pause typing and Enter',async()=>{await replace('S');await wait(350);const first=await snap();await page.keyboard.type('herlock',{delay:260});await wait(350);const full=await snap();await page.keyboard.press('Enter');await page.keyboard.type('x');await wait(350);const after=await snap();return {pass:first.value==='S'&&full.value==='Sherlock'&&after.value==='Sherlockx'&&after.focus,first,full,after}});
await replace('Sherlock');await wait(350);await page.keyboard.type(' ');await page.click('.top-match [data-action="item-open"]');
await check('result focus remains intentional',async()=>{await wait(350);const d=await snap();return {pass:!d.focus&&d.activeAction==='item-open',...d}});
await page.focus('#searchInput');await page.keyboard.type(' ');await page.keyboard.press('Tab');
await check('clear focus remains intentional',async()=>{await wait(350);const d=await snap();return {pass:!d.focus&&d.activeAction==='search-clear',...d}});
await page.screenshot({path:own+'/focus-identity-'+(process.argv[2]||'after')+'.png'});
'''+s[z:]
s=s.replace("'/remediation-test-results.json'","'/focus-identity-'+(process.argv[2]||'after')+'.json'")
(p/'test-focus-identity.cjs').write_text(s,encoding='utf-8')
