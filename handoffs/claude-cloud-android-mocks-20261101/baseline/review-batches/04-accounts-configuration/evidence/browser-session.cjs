"use strict";
const fs=require('fs'),path=require('path'),readline=require('readline');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const out=__dirname,batch=path.dirname(out);
const receipt={startedUtc:new Date().toISOString(),profile:path.join(out,'browser-profile'),events:[],closed:false};
const save=()=>fs.writeFileSync(path.join(out,'browser-session.json'),JSON.stringify(receipt,null,2)+'\n');
(async()=>{
 let browser;
 try{
 browser=await puppeteer.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,userDataDir:receipt.profile,args:['--disable-background-networking','--disable-component-update','--disable-extensions','--no-first-run','--no-default-browser-check'],timeout:45000});
 receipt.browserVersion=await browser.version();receipt.browserPid=browser.process().pid;save();
 console.log('REVIEW_READY '+JSON.stringify({pid:receipt.browserPid,version:receipt.browserVersion}));
 const cleanup=setTimeout(async()=>{receipt.events.push({deadlineReached:new Date().toISOString()});try{await browser.close();}finally{receipt.closed=true;save();process.exit(0);}},155*60*1000);cleanup.unref();
 for await(const line of readline.createInterface({input:process.stdin,crlfDelay:Infinity})){
 if(!line.trim())continue;
 let command;
 try{
 command=JSON.parse(line);
 if(command.cmd==='close'){await browser.close();receipt.closed=true;receipt.closedUtc=new Date().toISOString();save();clearTimeout(cleanup);console.log('REVIEW_CLOSED');process.exit(0);}
 const modulePath=path.join(out,command.module||'qa-core.cjs');
 if(!modulePath.startsWith(out+path.sep))throw Error('Module outside owned evidence');
 delete require.cache[require.resolve(modulePath)];
 const value=await require(modulePath).run({browser,batch,out,base:path.resolve(batch,'../..')},command);
 receipt.events.push({utc:new Date().toISOString(),command,result:value});save();
 console.log('REVIEW_DONE '+JSON.stringify(value));
 }catch(e){receipt.events.push({utc:new Date().toISOString(),command,error:e.stack});save();console.log('REVIEW_ERROR '+e.stack);}
 }
 }finally{if(browser&&!receipt.closed){await browser.close();receipt.closed=true;receipt.closedUtc=new Date().toISOString();save();}}
})().catch(e=>{receipt.error=e.stack;save();console.error(e.stack);process.exitCode=1;});