(async()=>{const out=[];for(const name of ['theatre-catalogue.html','manga-catalogue.html','comics-catalogue.html']){
qaStart('09-catalogue-index',name);await go(qaPage.job,name,'after',354,792);
await qaCheck('Collapsed rail chevron points towards content',async()=>page.$eval('#railToggle svg',e=>getComputedStyle(e).transform==='none'));
await qaCheck('Expanded rail reserves readable shelf space',async()=>{await qaClick('#railToggle');await delay(300);return page.evaluate(()=>{let a=document.querySelector('.index-rail')||document.querySelector('#catalogue aside'),b=document.querySelector('.shelf-stack'),r=a.getBoundingClientRect(),s=b.getBoundingClientRect();return{pass:s.x>=r.right&&s.width>=110,rail:r.width,content:s.width,gap:s.x-r.right};});});await qaShot('index-open');
await qaCheck('Last index item can be reached and activated',async()=>{const links=await page.$$('.rail-row');await links.at(-1).click();await delay(400);return page.$eval('#worldScroll',e=>e.scrollTop>500);});await qaShot('index-last-row');
await qaCheck('Long catalogue reaches its final section',()=>qaScrollBottom('#worldScroll'));await qaShot('long-content-bottom');
await go(qaPage.job,name,'after',792,354,{rail:'open',font:'1.3',top:'24',bottom:'24'});
await qaCheck('Short-landscape rail toggle remains within screen',()=>qaBounds('#railToggle'));await qaShot('index-short-stress');
await go(qaPage.job,name,'after',412,900);
await qaCheck('Horizontal shelf can scroll',async()=>{let strip=await page.$('.shelf-stack .strip');await strip.hover();await page.mouse.wheel({deltaX:460,deltaY:0});await delay(250);return strip.evaluate(e=>({pass:e.scrollLeft>0,left:e.scrollLeft,width:e.clientWidth,content:e.scrollWidth}));});await qaShot('horizontal-scroll');
if(name==='theatre-catalogue.html'){
await qaCheck('Shows tab has distinct catalogue rows',async()=>{await qaClick('[data-tab="shows"]');return !!await page.$('#shelf-currently-airing');});
await qaCheck('Custom row rename reflects in index',async()=>{await qaClick('[data-action="customize"]');const b=await page.$('[data-edit="rename"]');await b.click();await page.$eval('.rename-input',e=>{e.value='Very long custom shelf title for narrow portrait verification';});await page.focus('.rename-input');await page.keyboard.press('Enter');await delay(100);return page.$$eval('.rail-row',es=>es.some(e=>e.textContent.includes('Very long custom')));});await qaShot('rename-long');
await qaCheck('Hidden row is removed on leaving customize',async()=>{await qaClick('[data-edit="visibility"]');await qaClick('[data-action="customize"]');return page.$$eval('.rail-row',es=>!es.some(e=>e.textContent.includes('Very long custom')));});
}else if(name==='comics-catalogue.html'){await qaCheck('Non-pinnable decade rows omit See all',async()=>page.$eval('#shelf-comics-11',e=>!e.querySelector('[data-action="see-all"]')));}
await go(qaPage.job,name,'after',1280,700);
await qaCheck('Desktop retains all six world pills',async()=>await page.$$eval('.mode-pill',es=>es.length)===6);await qaCheck('Desktop shows original account silhouette',async()=>await page.$('.account-head')!==null);
await go(qaPage.job,name,'after',412,900);
await qaCheck('Library tab reaches matching finished library',async()=>{await qaClick('[data-tab="library"]');await delay(150);return page.url().includes('/16-libraries/'+(name.startsWith('theatre')?'theatre':'tankoban')+'-library.html');});
out.push(qaSave());}return out;})()