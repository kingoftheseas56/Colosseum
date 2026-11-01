from pathlib import Path
p=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries\codex-review-sol')
s=(p/'review.cjs').read_text().replace('for(let m of manifest){',"for(let m of manifest.filter(m=>m.name==='search.html'||m.job==='15-world-home')){").replace('[[412,900],[900,412],[1280,700]]','[]').replace("'/fresh-checks.json'","'/search-carousel-checks.json'")
start=s.index("if(m.name==='search.html')");end=s.index('\n}',start)
s=s[:start]+r'''
if(m.job==='15-world-home'){await check(m.name+' carousel controls change slide',async()=>{const before=await page.$eval('.hero-title',e=>e.textContent);await page.click('[data-action="slide"][data-index="1"]');const after=await page.$eval('.hero-title',e=>e.textContent);return {pass:before!==after,before,after}});}
if(m.name==='search.html'){
await check('Search normal slow typing retains query focus caret',async()=>{await page.focus('#searchInput');await page.keyboard.down('Control');await page.keyboard.press('A');await page.keyboard.up('Control');await page.keyboard.type('Sherlock',{delay:260});await new Promise(r=>setTimeout(r,350));let observed=await page.$eval('#searchInput',e=>({value:e.value,active:document.activeElement===e,start:e.selectionStart,end:e.selectionEnd}));await page.screenshot({path:own+'/search-slow-typing.png'});return {pass:observed.value==='Sherlock'&&observed.active&&observed.start===8,observed}});
await go(m,412,900);await page.focus('#searchInput');await page.keyboard.down('Control');await page.keyboard.press('A');await page.keyboard.up('Control');await page.keyboard.type('Sherlock');await new Promise(r=>setTimeout(r,350));
await check('Search query submission retains typing focus',async()=>{const before=await page.$eval('#searchInput',e=>({value:e.value,active:document.activeElement===e}));await page.keyboard.press('Enter');await new Promise(r=>setTimeout(r,350));let observed=await page.$eval('#searchInput',e=>({value:e.value,active:document.activeElement===e,start:e.selectionStart,end:e.selectionEnd,activeTag:document.activeElement.tagName}));await page.keyboard.type(' Holmes');await new Promise(r=>setTimeout(r,350));let after=await page.$eval('#searchInput',e=>e.value);await page.screenshot({path:own+'/search-enter-focus.png'});return {pass:observed.active&&after==='Sherlock Holmes',before,observed,after}});
await go(m,412,900);
await check('Search matching query ordinary keyboard replacement',async()=>{await page.focus('#searchInput');await page.keyboard.down('Control');await page.keyboard.press('A');await page.keyboard.up('Control');await page.keyboard.type('Dune');await new Promise(r=>setTimeout(r,400));return page.$eval('#searchInput',e=>({pass:e.value==='Dune'&&document.body.innerText.includes('TOP MATCH'),value:e.value,active:document.activeElement===e,text:document.body.innerText.slice(-200)}));});
}
'''+s[end:]
(p/'search-carousel.cjs').write_text(s)
