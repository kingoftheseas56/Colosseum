const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=process.argv[2],read=p=>fs.readFileSync(path.join(root,p),'utf8');
const H=read('qml/feria/PorticoDiscoveryHome.qml'),S=read('qml/feria/FeriaSeeAllView.qml'),W=read('qml/feria/FeriaWorld.qml');
function fn(s,n){let a=s.indexOf('function '+n+'(');assert(a>=0,'missing '+n);let b=s.indexOf('{',a),d=0;for(let i=b;i<s.length;i++){if(s[i]==='{')d++;if(s[i]==='}'&&!--d)return s.slice(a,i+1)}throw Error('unclosed '+n)}
function bind(c,s,names){vm.createContext(c);for(const n of names)vm.runInContext(fn(s,n),c);return c}
const timer=()=>({running:false,stop(){this.running=false},restart(){this.running=true}});
function see(){let c={Math,Number,Qt:{OtherFocusReason:1},controller:{viewState:'seeAll'},visible:true,entries:[1,2,3,4,5,6],
 pendingReturnSnapshot:null,returnRestoreAttempts:0,returnRestoreStablePasses:0,hoverSelectionEnabled:true,
 restoreReturnPosition:timer(),armHoverSelection:timer(),
 grid:{count:6,currentIndex:0,contentY:0,originY:0,topMargin:0,bottomMargin:0,contentHeight:1200,height:500,forceLayout(){},forceActiveFocus(){c.focuses++}},focuses:0};
 c.root=c;return bind(c,S,['focusGrid','returnSnapshot','applyReturn','restoreReturn','cancelRestoration','restoreReturnStep']);}
function home(){let c={Math,Number,JSON,Qt:{},controller:{lens:'watch',viewState:'home',focusArea:'app',focusIndex:0,activeApps:['a'],shelfIndex:0,shelfCardIndex:0,shownShelves:()=>[],shelfItems:()=>[]},visible:true,
 pendingNavigationPosition:null,pendingLensPosition:null,pendingContinueSelection:null,lensPositions:{},navigationRestoreAttempts:0,navigationRestoreStablePasses:0,
 restoreNavigationPosition:timer(),restorePosition:timer(),continueRevealTimer:timer(),resizeReveal:timer(),
 page:{contentY:0,contentHeight:1800,height:700},pageGlide:{cancelGlide(){}},homeColumn:{forceLayout(){}},shelfRepeater:{count:0,itemAt:()=>null},continueKeyboardRail:null,
 restoreContinueFocus(){c.focused=true},focused:false};
 c.home=c;return bind(c,H,['rememberLens','navigationSnapshot','continueSelection','restoreNavigationSnapshot','boundedContentY','applyPosition','boundFocusSelection','cancelRestoration','suspendRestoration','resumeRestoration','restoreNavigationStep']);}
let pass=0,fail=0;function test(n,f){try{f();console.log('PASS '+n);pass++}catch(e){console.log('FAIL '+n+': '+e.message.split('\n')[0]);fail++}}
test('See All fresh selection wins next timer pass',()=>{let c=see();c.restoreReturn({index:5,y:300});c.restoreReturnStep();c.cancelRestoration();c.grid.currentIndex=4;c.restoreReturnStep();assert.equal(c.grid.currentIndex,4);assert.equal(c.pendingReturnSnapshot,null)});
test('See All scroll and hidden route cannot reclaim focus',()=>{let c=see();c.restoreReturn({index:5,y:300});c.cancelRestoration();c.grid.contentY=111;c.restoreReturnStep();assert.equal(c.grid.contentY,111);c.restoreReturn({index:3,y:200});c.controller.viewState='title';c.restoreReturnStep();assert.equal(c.focuses,0)});
test('See All removed items clamp and finish',()=>{let c=see();c.restoreReturn({index:100,y:9000});for(let i=0;i<16;i++)c.restoreReturnStep();assert.equal(c.grid.currentIndex,5);assert.equal(c.grid.contentY,700);assert.equal(c.pendingReturnSnapshot,null)});
test('See All empty model is valid and terminates',()=>{let c=see();c.grid.count=0;c.grid.contentHeight=0;c.restoreReturn({index:5,y:800});for(let i=0;i<16;i++)c.restoreReturnStep();assert.equal(c.grid.currentIndex,-1);assert.equal(c.grid.contentY,0);assert.equal(c.pendingReturnSnapshot,null)});
test('Home fresh input cancels saved position before reveal',()=>{let c=home();c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{}}}});c.restoreNavigationStep();c.cancelRestoration();c.page.contentY=0;c.restoreNavigationStep();assert.equal(c.page.contentY,0);assert.equal(c.pendingNavigationPosition,null)});
test('Home quick repark keeps pending desired position and Continue identity',()=>{let c=home();c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{r:200}}},continueSelection:{identity:'wanted',index:2}});c.page.contentY=20;let snap=c.navigationSnapshot();assert.equal(snap.lensPositions.watch.y,800);assert.equal(snap.continueSelection.identity,'wanted')});
test('Home no-input restore survives internal coordinate updates',()=>{let c=home();c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{}}}});for(let i=0;i<10;i++)c.restoreNavigationStep();assert.equal(c.page.contentY,800);assert.equal(c.pendingNavigationPosition,null)});
test('Home shorter layout and removed rail clamp and settle',()=>{let c=home();c.page.contentHeight=900;c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{removed:1000}}}});for(let i=0;i<16;i++)c.restoreNavigationStep();assert.equal(c.page.contentY,200);assert.equal(c.pendingNavigationPosition,null)});
test('Home hidden initialization retains intent but does not apply or focus',()=>{let c=home();c.controller.viewState='title';c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{}}}});c.restoreNavigationStep();assert.equal(c.page.contentY,0);assert(!c.focused);assert.equal(c.navigationSnapshot().lensPositions.watch.y,800);c.controller.viewState='home';c.resumeRestoration();for(let i=0;i<10;i++)c.restoreNavigationStep();assert.equal(c.page.contentY,800)});
test('Home new lens/input clears old restoration work',()=>{let c=home();c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{}}}});c.cancelRestoration();c.controller.lens='read';c.page.contentY=125;c.restoreNavigationStep();assert.equal(c.page.contentY,125);assert.equal(c.pendingContinueSelection,null)});
test('deferred focus rejects changed epoch, route, lens and hidden shell',()=>{const q=[];let c={Qt:{callLater:f=>q.push(f)},navigationEpoch:0,viewState:'home',lens:'watch',visible:true,navigationSuspended:false};c.shell=c;bind(c,W,['deferNavigation']);let n=0;c.deferNavigation(()=>n++);c.navigationEpoch++;q.shift()();assert.equal(n,0);c.deferNavigation(()=>n++);c.viewState='title';q.shift()();assert.equal(n,0);c.deferNavigation(()=>n++);c.lens='read';q.shift()();assert.equal(n,0);c.deferNavigation(()=>n++);c.visible=false;q.shift()();assert.equal(n,0);c.visible=true;c.deferNavigation(()=>n++);q.shift()();assert.equal(n,1)});

test('fresh input invalidates deferred work and cancels only the active owner',()=>{
 let calls=[];let c={navigationEpoch:0,viewState:'home',seeAllResetTimer:{stop:()=>calls.push('reset')},
 homeView:{cancelRestoration:()=>calls.push('home')},seeAllView:{cancelRestoration:()=>calls.push('grid')}};
 bind(c,W,['noteNavigationInput']);c.noteNavigationInput();assert.equal(c.navigationEpoch,1);assert.deepEqual(calls,['reset','home']);
 c.viewState='seeAll';calls=[];c.noteNavigationInput();assert.deepEqual(calls,['reset','grid']);
});
test('existing shell park/return, explicit Home clear and profile invalidation',()=>{
 const M=read('qml/Main.qml');let snap={version:1,viewState:'home'},received=null;
 let c={activeBrowserSessionId:"",feriaReturnSnapshot:null,feriaLayer:{open:true,item:{navigationSnapshot:()=>snap,restoreNavigationSnapshot:s=>{received=s;return true}}}};
 c.win=c;bind(c,M,['parkFeria','restoreFeriaIfReady']);
 c.parkFeria(true);assert.strictEqual(c.feriaReturnSnapshot,snap);assert.equal(c.feriaLayer.open,false);
 assert.equal(c.restoreFeriaIfReady(),false);c.feriaLayer.open=true;assert.equal(c.restoreFeriaIfReady(),true);assert.strictEqual(received,snap);assert.equal(c.feriaReturnSnapshot,null);
 c.feriaReturnSnapshot=snap;c.parkFeria(false);assert.equal(c.feriaReturnSnapshot,null);
 c.feriaReturnSnapshot=snap;
 const profile=M.match(/function onProfileChanged\(\) \{ win\.feriaReturnSnapshot = null \}/)[0];
 vm.runInContext(profile,c);c.onProfileChanged();assert.equal(c.feriaReturnSnapshot,null);
 assert(fn(M,'openWorld').includes('win.parkFeria(true)'));
});
test('browser world parking preserves snapshot and does not restore over active browser',()=>{
 const M=read('qml/Main.qml');const snap={version:1,viewState:'title'};let parked=0;
 const c={activeBrowserSessionId:'a',parkingBrowserForWorld:false,feriaReturnSnapshot:null,
 feriaLayer:{open:true,item:{navigationSnapshot:()=>snap,restoreNavigationSnapshot:()=>{throw Error('must not restore over browser')}}}};
 c.win=c;c.Sessions={switchTo:id=>{assert.equal(id,'');assert.equal(c.parkingBrowserForWorld,true);parked++}};
 bind(c,M,['parkFeria','restoreFeriaIfReady']);assert.equal(c.restoreFeriaIfReady(),false);
 c.parkFeria(true);assert.equal(parked,1);assert.strictEqual(c.feriaReturnSnapshot,snap);assert.equal(c.parkingBrowserForWorld,false);
});
test('browser switch tears down outgoing fullscreen only and preserves both pages',()=>{
 const M=read('qml/Main.qml');let a=0,b=0;
 const old={immersive:true,browser:{fullScreenActive:true,runJavaScript:()=>a++}};
 const next={immersive:true,browser:{fullScreenActive:true,runJavaScript:()=>b++}};
 const c={feriaLayer:{open:true},feriaSessionDeck:{entries:{a:old,b:next},activeEntry:next,activeBrowser:next.browser}};
 bind(c,M,['teardownSession']);c.teardownSession({id:'a',contentKind:'browser'});
 assert.equal(a,1);assert.equal(b,0);assert.equal(old.immersive,false);assert.equal(next.immersive,true);
 assert.strictEqual(c.feriaSessionDeck.entries.a,old);assert.strictEqual(c.feriaSessionDeck.entries.b,next);assert.equal(c.feriaLayer.open,false);
});

test('input wiring preserves native handlers and passive pointer/wheel delivery',()=>{
 const I=read('qml/feria/FeriaRestorationInput.qml');
 assert(I.includes('PointHandler {'));assert(I.includes('WheelHandler {'));assert(I.includes('blocking: false'));
 assert(I.includes('event.accepted = false'));assert(I.includes('point.scenePosition'));
 assert(S.includes('Keys.forwardTo: [restorationInput]'));assert(H.includes('home.continueKeyboardRail.Keys.forwardTo = [restorationInput]'));
 assert(!H.includes('onContentYChanged: controller.noteNavigationInput'));
});
test('clamped shelf selections and live rail offsets settle without self-cancel',()=>{
 let c=home();let rail={modelData:{id:'one'},railContentX:0,boundedRailX:x=>Math.min(150,Math.max(0,x)),restoreRail(x){this.railContentX=this.boundedRailX(x)}};
 c.shelfRepeater={count:1,itemAt:()=>rail};c.controller.shownShelves=()=>[{}];c.controller.shelfItems=()=>['one'];
 c.controller.shelfIndex=99;c.controller.shelfCardIndex=99;c.controller.focusIndex=99;
 c.restoreNavigationSnapshot({lensPositions:{watch:{y:800,rails:{one:800,gone:100}}}});
 for(let i=0;i<10;i++)c.restoreNavigationStep();
 assert.equal(rail.railContentX,150);assert.equal(c.controller.shelfIndex,0);assert.equal(c.controller.shelfCardIndex,0);assert.equal(c.controller.focusIndex,1);assert.equal(c.pendingNavigationPosition,null);
});
test('quick See All repark preserves intended snapshot until fresh input',()=>{
 let c=see();c.restoreReturn({index:5,y:300});c.grid.currentIndex=0;c.grid.contentY=0;
 let snap=c.returnSnapshot();assert.equal(snap.index,5);assert.equal(snap.y,300);
 c.cancelRestoration();c.grid.currentIndex=4;c.grid.contentY=100;snap=c.returnSnapshot();assert.equal(snap.index,4);assert.equal(snap.y,100);
});
console.log(JSON.stringify({pass,fail}));process.exitCode=fail?1:0;
