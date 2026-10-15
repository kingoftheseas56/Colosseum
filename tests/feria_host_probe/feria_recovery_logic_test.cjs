const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=process.argv[2],read=p=>fs.readFileSync(path.join(root,p),'utf8');
const H=read('qml/feria/FeriaRecoveryHostState.qml'),W=read('qml/feria/FeriaWorld.qml'),T=read('qml/feria/PorticoDiscoveryTitleView.qml');
function fn(s,n){let a=s.indexOf('function '+n+'(');assert(a>=0,'missing '+n);let b=s.indexOf('{',a),d=0;for(let i=b;i<s.length;i++){if(s[i]==='{')d++;if(s[i]==='}'&&!--d)return s.slice(a,i+1)}throw Error('unclosed '+n)}
function bind(c,s,names){vm.createContext(c);for(const n of names)vm.runInContext(fn(s,n),c);return c}
const Recovery={};vm.createContext(Recovery);vm.runInContext(read('qml/feria/FeriaRecoveryState.js').replace('.pragma library',''),Recovery);
let pass=0,fail=0;function test(n,f){try{f();console.log('PASS '+n);pass++}catch(e){fail++;console.log('FAIL '+n+': '+e.message.split('\n')[0])}}
function host(engine='webview2'){
 let queue=[],calls={back:0,forward:0,reload:0,stop:0,restart:0,switch:[]};
 let c={Qt:{callLater:f=>queue.push(f)},active:true,engine,qtAvailable:true,webAvailable:true,epoch:0,slowEpoch:-1,
 starting:false,slow:false,succeeded:false,stopped:false,fallbackUsed:false,actionPending:false,error:'',resumeMessage:'',
 slowTimer:{running:false,restart(){this.running=true},stop(){this.running=false}},
 locationStarted(){},locationCompleted(){},restartRequested(){calls.restart++},switchRequested:(next,remember)=>calls.switch.push({next,remember})};
 c.browser={ready:true,loadActive:false,loadProgress:engine==='webview2'?-1:37,navCanGoBack:false,navCanGoForward:false,
 pageBack(){calls.back++},pageForward(){calls.forward++},reload(){calls.reload++},stopLoad(){calls.stop++;this.loadActive=false}};
 c.recoveryState=c;
 bind(c,H,['cancel','resetVisit','beginStartup','armSlow','markSlow','started','completed','failed','resumeUnavailable','retry','stop','switchEngine','navigate']);
 for(const name of ['busy','engineAvailable','alternativeAvailable','canStop','canRetry','progress']){
  const expression=H.match(new RegExp('readonly property (?:bool|real) '+name+': (.+)'))[1];
  Object.defineProperty(c,name,{get:()=>vm.runInContext(expression,c)});
 }
 return {c,calls,flush:()=>{while(queue.length)queue.shift()()}};
}
for(const engine of ['webview2','qtwebengine']){
 test(engine+' history disabled is a no-op',()=>{let{c,calls}=host(engine);assert.equal(c.navigate(true),false);assert.equal(c.navigate(false),false);assert.equal(calls.back+calls.forward,0)});
 test(engine+' each enabled history action invokes once',()=>{let{c,calls}=host(engine);c.browser.navCanGoBack=c.browser.navCanGoForward=true;c.navigate(true);c.navigate(false);assert.equal(calls.back,1);assert.equal(calls.forward,1)});
 test(engine+' progress is wrapper-owned',()=>{let{c}=host(engine);assert.equal(c.progress,engine==='webview2'?-1:37)});
 test(engine+' slow notice is generation and busy guarded',()=>{let{c}=host(engine);c.beginStartup();c.markSlow();assert(c.slow);c.cancel();c.markSlow();assert(!c.slow);assert(!c.slowTimer.running)});
 test(engine+' failed page persists until retry and duplicate retry blocked',()=>{let{c,calls}=host(engine);c.completed('https://example.test',false);assert(c.error);assert(c.retry());assert(!c.retry());assert.equal(calls.reload,1);assert.equal(c.error,'')});
 test(engine+' stop requires actual stop capability',()=>{let{c,calls}=host(engine);assert(!c.stop());c.browser.loadActive=true;assert(c.stop());assert.equal(calls.stop,1);assert(c.stopped);assert(!c.slow)});
 test(engine+' resume failure persists across completion, clears only new visit/dismissal',()=>{let{c}=host(engine);c.resumeUnavailable();let message=c.resumeMessage;c.completed('x',true);assert.equal(c.resumeMessage,message);assert(message.includes('not restored'));c.resetVisit();assert.equal(c.resumeMessage,'')});
 test(engine+' inactive signals cannot modify feedback',()=>{let{c}=host(engine);c.active=false;c.started('x');c.completed('x',false);c.failed();c.resumeUnavailable();assert.equal(c.error,'');assert.equal(c.resumeMessage,'')});
}
test('DOM readiness does not terminate WebView2 slow loading',()=>{let{c}=host();c.browser.loadActive=true;c.started('x');c.markSlow();c.completed('x',true);assert(c.busy);assert(c.slowTimer.running);assert(c.slow)});
test('startup failure uses exactly one one-way automatic fallback',()=>{let{c,calls,flush}=host();c.failed();flush();assert.deepEqual(calls.switch,[{next:'qtwebengine',remember:false}]);c.failed();flush();assert.equal(calls.switch.length,1)});
test('queued fallback cannot change departed route',()=>{let{c,calls,flush}=host();c.failed();c.active=false;c.cancel();flush();assert.equal(calls.switch.length,0)});
test('queued fallback cannot change a new visit/engine',()=>{let{c,calls,flush}=host();c.failed();c.resetVisit();flush();assert.equal(calls.switch.length,0);c.failed();c.engine='qtwebengine';c.cancel();flush();assert.equal(calls.switch.length,0)});
test('unavailable alternative has no switch or fallback',()=>{let{c,calls,flush}=host();c.qtAvailable=false;c.failed();flush();assert.equal(calls.switch.length,0);assert(!c.switchEngine())});
test('Loader startup retry delegates without fake page history',()=>{let{c,calls}=host('qtwebengine');c.browser=null;c.failed();assert(c.retry());assert.equal(calls.restart,1);assert.equal(calls.reload,0)});
test('unavailable current engine disables retry',()=>{let{c}=host('qtwebengine');c.qtAvailable=false;c.failed();assert(!c.canRetry);assert(!c.retry())});
test('manual compatibility switch is availability gated and remembered',()=>{let{c,calls}=host();assert(c.switchEngine());assert.deepEqual(calls.switch,[{next:'qtwebengine',remember:true}]);assert(!c.switchEngine())});
for(const [label,row,kind] of [
 ['initial loading',{loading:true,items:[]},'loading'],['cached while loading',{loading:true,items:['a']},'loading'],
 ['error with cache',{error:'offline',items:['a']},'error'],['error without cache',{error:'offline',items:[]},'error'],
 ['stale',{stale:true,items:['a']},'cached'],['fallback',{fallbackReason:'cache',items:['a']},'cached'],
 ['empty',{items:[]},'empty'],['ready',{items:['a']},'ready'],['removed',null,'removed']])
 test('feed '+label,()=>assert.equal(Recovery.feedStatus(row).kind,kind));
test('cached source projection is honest and does not mutate data',()=>{let row={items:['a'],loading:true,sourceLabel:'Fixture'};let before=JSON.stringify(row);assert(Recovery.feedStatus(row).message.includes('cached'));assert.equal(JSON.stringify(row),before)});
test('aggregate and local search distinguish unloaded/loading/failed context',()=>{assert.equal(Recovery.aggregate([],false).kind,'idle');assert.equal(Recovery.aggregate([{loading:true}],true).kind,'loading');assert.equal(Recovery.aggregate([{error:'x'}],true).kind,'cached')});
for(const status of ['idle','loading','error','unmatched','ready','unsupported']){
 test('availability '+status,()=>{let text=Recovery.availabilityMessage({status,region:'IN',offers:[]},'US',status!=='unsupported',false);assert(text.length);if(status==='unsupported')assert(!text.includes('Checking'));if(status==='ready')assert(text.includes('No streaming offers'))});
}
test('nonqueryable idle title never says checking',()=>assert(!Recovery.availabilityMessage({status:'idle'},'IN',false,false).includes('Checking')));
test('offers are not an entitlement promise',()=>assert(Recovery.availabilityMessage({status:'ready',offers:[{}]},'IN',true,false).includes('not confirmed')));
test('identity mapping preserves item through reorder and deselects removal',()=>{assert.equal(Recovery.remapIndex(['a','b'],['b','a'],1),0);assert.equal(Recovery.remapIndex(['a','b'],['a'],1),-1);assert.equal(Recovery.identityIndex([{id:'one'}],'one'),0)});
function world(){
 let calls=[],c={Object,Recovery,noteNavigationInput:()=>{},liveDiscovery:true,feedRetryPending:{},aggregateRetryPending:false,discoveryStarted:true,
 catalogueFeeds:[{shelfId:'feed-a',items:[],error:'x'}],feedRetryDeadline:{restart(){}},
 discovery:{refreshFeed:id=>calls.push(['feed',id]),refresh:()=>calls.push(['all'])}};
 bind(c,W,['retryFeed','retryCatalogue','settleFeedRetry']);return {c,calls};
}
test('per-feed retry uses exact feed ID once until completion',()=>{let{c,calls}=world();assert(c.retryFeed('feed-a'));assert(!c.retryFeed('feed-a'));assert(!c.retryFeed('fake'));assert.deepEqual(calls,[['feed','feed-a']]);c.settleFeedRetry('feed-a');assert(c.retryFeed('feed-a'))});
test('busy source and aggregate suppress concurrent retry',()=>{let{c,calls}=world();c.catalogueFeeds[0].loading=true;assert(!c.retryFeed('feed-a'));assert(!c.retryCatalogue());assert.equal(calls.length,0)});
test('aggregate retry calls facade refresh, never raw refreshAll',()=>{let{c,calls}=world();assert(c.retryCatalogue());assert(!c.retryCatalogue());assert.deepEqual(calls,[['all']]);assert(!fn(W,'retryCatalogue').includes('refreshAll'))});
test('bad Continue entry is retained and never opened',()=>{let count=0,feedback='';const c={Data:{P:{ok:{}}},FeriaBrowserPolicy:{allowsNavigation:u=>u==='https://ok'},showNavigationError:m=>feedback=m,openHost:()=>count++};bind(c,W,['resumeSession']);const invalid={pk:'gone',url:''};c.resumeSession(invalid);assert(feedback);assert.equal(count,0);assert.deepEqual(invalid,{pk:'gone',url:''});c.resumeSession({pk:'ok',url:'https://ok',position:12});assert.equal(count,1);c.resumeSession({pk:'native-provider-not-in-demo-map',url:'https://ok',position:12});assert.equal(count,2)});
test('availability Retry calls force once and blocks while busy',()=>{let n=0,c={visible:true,availabilityQueryable:true,retryPending:false,availability:{status:'error'},controller:{titleActionIndex:1,requestAvailability:(it,force)=>{assert(force);n++}},it:{},retryLatch:{restart(){}}};c.root=c;bind(c,T,['retryAvailability']);assert(c.retryAvailability());assert(!c.retryAvailability());assert.equal(n,1);assert.equal(c.controller.titleActionIndex,-1)});
test('cached offers retained only for matching title and region',()=>{assert(T.includes('cachedContext === offerContext'));assert(T.includes('availability.status === "loading" || availability.status === "error"'));assert(T.includes('function onRegionChanged()'));assert(T.includes('root.cachedDoors = []'))});
const Host=read('qml/feria/PorticoDiscoveryHostView.qml');
test('first-level Previous is unique and distinct from Feria return',()=>{assert.equal((Host.match(/objectName: "feriaBrowserBack"/g)||[]).length,1);assert(Host.indexOf('objectName: "feriaBrowserBack"')<Host.indexOf('id:optionsFlow'));assert(Host.includes('label: "Back to Feria"'))});
test('recovery is laid out above native browser, suppression is bound',()=>{assert(Host.includes('top: strip.bottom'));assert(Host.includes('controller.browserSuppressed || root.optionsOpen'));assert(Host.includes('visible: !root.fullScreenActive'))});

test('real title reconciliation never silently selects a replacement provider',()=>{
 const c={Recovery,visible:false,controller:{titleActionIndex:1},offerContext:'title|IN',selectionContext:'title|IN',
 previousDoors:[{pk:'one',url:'a'},{pk:'two',url:'b'}],doors:[{pk:'two',url:'b'},{pk:'one',url:'a'}],related:[]};
 bind(c,T,['reconcileActions']);c.reconcileActions();assert.equal(c.controller.titleActionIndex,0);
 c.doors=[{pk:'one',url:'a'}];c.reconcileActions();assert.equal(c.controller.titleActionIndex,-1);
});
test('real Search selection reconciliation preserves query and app link identity',()=>{
 const S=read('qml/feria/PorticoDiscoverySearchRedesign.qml'),c={Recovery,controller:{query:'kept',searchResultIndex:1},
 previousQuery:'kept',previousHits:['a','b'],previousApps:['netflix'],hits:['b','a'],apps:['netflix']};
 bind(c,S,['retainSelection']);c.retainSelection();assert.equal(c.controller.searchResultIndex,0);assert.equal(c.controller.query,'kept');
 c.controller.searchResultIndex=2;c.hits=['a'];c.retainSelection();assert.equal(c.controller.searchResultIndex,1);
 c.hits=[];c.apps=[];c.retainSelection();assert.equal(c.controller.searchResultIndex,-1);
});
test('real native shelf projection retains data, ordering and metadata',()=>{
 const feeds=[{shelfId:'first',title:'First',sourceLabel:'A',items:[{canonicalKey:'a'}],loading:true,error:'',stale:true,fetchedAt:'today'},
 {shelfId:'second',title:'Second',sourceLabel:'B',items:[{canonicalKey:'b'}],loading:false,error:'offline'},
 {shelfId:'empty',title:'Empty',items:[],loading:true}];
 const c={liveDiscovery:true,saved:[],discovery:{revision:1,shelves:()=>feeds},contentStore:{revision:1,contains:()=>true}};
 bind(c,W,['shownShelves']);const rows=c.shownShelves();assert.equal(rows.length,2);assert.equal(rows[0].id,'first');assert.equal(rows[1].id,'second');
 assert.equal(rows[0].loading,true);assert.equal(rows[0].fetchedAt,'today');assert.equal(rows[1].error,'offline');assert.equal(rows[1].items[0],'b');
});
test('activation uses the displayed cached offer, not a newly shifted backend list',()=>{
 let url='';const c={selectedTitle:'title',titleActionIndex:0,titleView:{doors:[{pk:'netflix',url:'cached',level:'t'}]},
 titleObj:()=>({}),relatedTitles:()=>[],openHost:(pk,id,mode,u)=>url=u};
 bind(c,W,['activateTitleAction']);c.activateTitleAction();assert.equal(url,'cached');url='';c.titleActionIndex=-1;c.activateTitleAction();assert.equal(url,'');
});

test('focus handoff survives its own navigation but not a new visit',()=>{
 const queue=[];let count=0,item={};const c={Qt:{callLater:f=>queue.push(f)},visible:true,engine:'webview2',
 browserLoader:{active:true,item},recovery:{epoch:1,visitKey:'visit-a'}};c.root=c;
 bind(c,Host,['deferHost']);c.deferHost(()=>count++,false);c.recovery.epoch++;queue.shift()();assert.equal(count,1);
 c.deferHost(()=>count++,false);c.recovery.visitKey='visit-b';queue.shift()();assert.equal(count,1);
 c.deferHost(()=>count++);c.recovery.epoch++;queue.shift()();assert.equal(count,1);
});
console.log(JSON.stringify({pass,fail}));process.exitCode=fail?1:0;
