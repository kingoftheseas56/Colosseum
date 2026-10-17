const fs=require('fs'),vm=require('vm'),assert=require('assert');
const root=process.argv[2];
const source=p=>fs.readFileSync(root+'/'+p,'utf8');
const W=source('qml/feria/FeriaWorld.qml'),H=source('qml/feria/PorticoDiscoveryHome.qml');
function block(s,start){let b=s.indexOf('{',start),depth=0;for(let i=b;i<s.length;i++){if(s[i]==='{')depth++;if(s[i]==='}'&&!--depth)return s.slice(b,i+1)}throw Error('unclosed');}
function fn(s,n){const at=s.indexOf('function '+n+'(');assert(at>=0,'missing '+n);return s.slice(at,s.indexOf('{',at))+block(s,at);}
const keys=['Left','Right','Up','Down','Return','Enter','Space','BracketLeft','BracketRight','Slash','1','9'];
const Qt=Object.fromEntries(keys.map((k,i)=>['Key_'+k,i+100]));
Object.assign(Qt,{ControlModifier:1,TabFocusReason:1,BacktabFocusReason:2,callLater:f=>f()});
let pass=0,fail=0;function test(name,f){try{f();console.log('PASS '+name);pass++}catch(e){console.log('FAIL '+name+': '+e.message.split('\n')[0]);fail++}}
function world(cols=3,count=8){let c={Qt,Math,Data:{},focusArea:'app',focusIndex:0,activeApps:Array.from({length:count},(_,i)=>''+i),movingApp:false,viewState:'home',shelfIndex:0,shelfCardIndex:0,selectedApp:'0',titleActionIndex:0, homeView:{appColumnCount:cols,revealApp:i=>c.revealed=i,revealFeature:()=>c.revealed='feature',focusLens:()=>{c.focusArea='lens'},revealShelf:i=>c.revealed='shelf'+i,focusBelowLens:()=>c.focusArea=c.hasContinue?'continue':'shelf',focusAboveShelves:()=>c.focusArea=c.hasContinue?'continue':'lens'}, titleView:{revealCurrentAction:()=>c.titleRevealed=c.titleActionIndex},shownShelves:()=>[{}],shelfItems:()=>[1,2],titleActionCount:()=>8,moveApp:d=>c.reordered=d,keyboardTarget:{forceActiveFocus:()=>{}},selectApp:null};
c.shell=c;
Object.assign(c,{navigationEpoch:0,navigationSuspended:false,visible:true,lens:'all',
 applyingNavigationSnapshot:false,seeAllResetTimer:{stop(){}},seeAllView:{cancelRestoration(){}}});
c.homeView.cancelRestoration=()=>{};
vm.createContext(c);
for(const n of ['noteNavigationInput','deferNavigation']) vm.runInContext(fn(W,n),c);
for(const n of ['selectApp','appColumns','moveAppSelection']){if(W.includes('function '+n+'('))vm.runInContext(fn(W,n),c);}
let a=W.indexOf('Keys.onPressed: function(event)');vm.runInContext('function press(event) '+block(W,a),c);c.pressKey=k=>c.press({key:Qt['Key_'+k],accepted:false,text:''});return c;}
test('grid horizontal boundaries',()=>{let c=world();c.focusIndex=2;c.pressKey('Right');assert.equal(c.focusIndex,2);c.focusIndex=3;c.pressKey('Left');assert.equal(c.focusIndex,3)});
test('grid vertical columns and reveal',()=>{let c=world();c.focusIndex=1;c.pressKey('Down');assert.equal(c.focusIndex,4);assert.equal(c.revealed,4);c.pressKey('Up');assert.equal(c.focusIndex,1);c.pressKey('Up');assert.equal(c.focusArea,'feature')});
test('management tile and bottom exit',()=>{let c=world();c.focusIndex=5;c.pressKey('Down');assert.equal(c.focusIndex,8);c.pressKey('Down');assert.equal(c.focusArea,'lens')});
test('ragged next row clamps to final cell',()=>{let c=world(4,9);c.focusIndex=7;c.pressKey('Down');assert.equal(c.focusIndex,9);c.pressKey('Up');assert.equal(c.focusIndex,5)});
test('responsive one-column and resize',()=>{let c=world(1,3);c.focusIndex=1;c.pressKey('Right');assert.equal(c.focusIndex,1);c.pressKey('Down');assert.equal(c.focusIndex,2);c.homeView.appColumnCount=2;c.pressKey('Up');assert.equal(c.focusIndex,0)});
test('reorder lateral behavior preserved',()=>{let c=world();c.movingApp=true;c.pressKey('Right');assert.equal(c.reordered,1);c.pressKey('Left');assert.equal(c.reordered,-1)});
test('lens and first shelf use Continue bridge',()=>{let c=world();c.hasContinue=true;c.focusArea='lens';c.pressKey('Down');assert.equal(c.focusArea,'continue');c.focusArea='shelf';c.pressKey('Up');assert.equal(c.focusArea,'continue')});
test('without Continue lens/shelf fallback',()=>{let c=world();c.hasContinue=false;c.focusArea='lens';c.pressKey('Down');assert.equal(c.focusArea,'shelf');c.pressKey('Up');assert.equal(c.focusArea,'lens')});
test('title arrows request reveal repeatedly',()=>{let c=world();c.viewState='title';for(let i=0;i<12;i++){c.pressKey('Down');assert.equal(c.titleRevealed,c.titleActionIndex)}for(let i=0;i<12;i++){c.pressKey('Up');assert.equal(c.titleRevealed,c.titleActionIndex)}});
test('snapshot restore includes Continue and management tile',()=>{assert.match(W, /\["feature", "app", "lens", "continue", "shelf"\]/);assert.match(W,/Math.min\(activeApps.length, Number\(snapshot.focusIndex/);});


function home(has=true,shelves=1){
 const c={Qt,Math,controller:{viewState:'home',focusArea:'lens',shelfIndex:3,shelfCardIndex:4,
  shownShelves:()=>Array(shelves).fill({}),keyboardTarget:{forceActiveFocus:()=>{}}},
  snapReveal:false,continueRevealTimer:{restart(){}},
  hasContinue:has,continueRow:{ordered:[{id:'a'},{id:'b'}]},pendingNavigationPosition:null,pendingContinueSelection:null,
  continueKeyboardRail:{currentIndex:0,keyboardIndexForIdentity:id=>['a','b'].indexOf(id),
  keyboardIdentityForIndex:i=>['a','b'][i]||'',keyboardRevealIndex:i=>c.continueKeyboardRail.currentIndex=i,
  forceActiveFocus:()=>c.railFocused=true},
  ensureVisible:()=>c.revealed='continue',revealShelf:()=>c.revealed='shelf',
  focusLens:()=>c.controller.focusArea='lens'};
 c.home=c;vm.createContext(c);
 for(const n of ['revealContinue','continueSelection','focusContinue','restoreContinueFocus','focusFirstShelf','focusBelowLens','focusAboveShelves','leaveContinue'])vm.runInContext(fn(H,n),c);
 return c;
}
test('real Home bridges with Continue',()=>{let c=home();c.focusBelowLens();assert.equal(c.controller.focusArea,'continue');assert(c.railFocused);assert.equal(c.revealed,'continue');c.leaveContinue({key:Qt.Key_Up});assert.equal(c.controller.focusArea,'lens');c.focusContinue();c.leaveContinue({key:Qt.Key_Down});assert.equal(c.controller.focusArea,'shelf');assert.equal(c.controller.shelfIndex,0);assert.equal(c.revealed,'shelf');c.focusAboveShelves();assert.equal(c.controller.focusArea,'continue')});
test('real Home fallback and empty shelves',()=>{let c=home(false);c.focusBelowLens();assert.equal(c.controller.focusArea,'shelf');c.focusAboveShelves();assert.equal(c.controller.focusArea,'lens');c=home(true,0);c.focusContinue();c.leaveContinue({key:Qt.Key_Down});assert.equal(c.controller.focusArea,'continue');c=home(false,0);c.focusBelowLens();assert.equal(c.controller.focusArea,'lens')});
test('Continue identity restoration and removed entry fallback',()=>{let c=home();c.controller.focusArea='continue';c.pendingContinueSelection={identity:'b',index:0};c.restoreContinueFocus();assert.equal(c.continueKeyboardRail.currentIndex,1);assert.equal(c.continueSelection().identity,'b');c.pendingContinueSelection={identity:'gone',index:20};c.restoreContinueFocus();assert.equal(c.continueKeyboardRail.currentIndex,1);c.hasContinue=false;c.restoreContinueFocus();assert.equal(c.controller.focusArea,'lens')});
test('Continue restore waits for layout, ignores other routes, leaves lateral/context keys alone',()=>{let c=home();c.controller.focusArea='continue';c.pendingNavigationPosition={};c.restoreContinueFocus();assert(!c.railFocused);c.pendingNavigationPosition=null;c.controller.viewState='title';c.restoreContinueFocus();assert(!c.railFocused);for(const key of [Qt.Key_Left,Qt.Key_Right,Qt.Key_Enter,Qt.Key_Space]){const e={key,accepted:false};c.leaveContinue(e);assert.equal(e.accepted,false)}});
const T=source('qml/feria/PorticoDiscoveryTitleView.qml');
test('title reveal geometry under repeated vertical/horizontal moves',()=>{
 let c={Math,u:16,scroll:{contentItem:{},contentX:0,contentY:0,width:800,height:600,contentWidth:1800,contentHeight:3000}};
 vm.createContext(c);vm.runInContext(fn(T,'revealItem'),c);
 for(const [x,y,w,h] of [[200,200,250,80],[200,1700,250,80],[1200,2100,148,240],[200,300,250,80],[200,200,250,80]]){
  c.revealItem({width:w,height:h,mapToItem:()=>({x,y})});
  assert(y>=c.scroll.contentY+112);assert(y+h<=c.scroll.contentY+600);
  assert(x>=c.scroll.contentX);assert(x+w<=c.scroll.contentX+800);
 }
});
test('title action lookup preserves provider/Save/related ordering',()=>{
 const provider=[{},{}],save={},related=[{},{}];
 const c={doors:provider,saveAction:save,offerRepeater:{count:2,itemAt:g=>({actionItem:i=>i===g?provider[i]:null})},relatedRepeater:{itemAt:i=>related[i]}};
 vm.createContext(c);vm.runInContext(fn(T,'actionItem'),c);
 [...provider,save,...related].forEach((item,i)=>assert.strictEqual(c.actionItem(i),item));
});
test('Retry uses shared keyboard action and exact forced request',()=>{
 const at=T.indexOf('objectName: "feriaAvailabilityRetry"');assert(at>0);
 const b=T.slice(at,T.indexOf('\n                        }',at));
 assert(b.includes('onTriggered: controller.requestAvailability(root.it, true)'));
 assert.equal(T.slice(0,at).trim().split('\n').at(-1).trim(),'Colosseum.KeyboardAction {');
 const K=source('qml/KeyboardAction.qml');assert(K.includes('focusPolicy: Qt.TabFocus'));assert(K.includes('Accessible.onPressAction: action.activate'));assert(K.includes('event.key === Qt.Key_Return || event.key === Qt.Key_Enter'));
});

test('grid all row boundaries for 1..8 columns and 0..20 apps',()=>{
 for(let cols=1;cols<=8;cols++)for(let count=0;count<=20;count++)for(let i=0;i<=count;i++){
  let c=world(cols,count);c.focusIndex=i;c.pressKey('Right');
  assert.equal(c.focusIndex,(i<count&&Math.floor((i+1)/cols)===Math.floor(i/cols))?i+1:i);
  c.focusIndex=i;c.pressKey('Left');assert.equal(c.focusIndex,i%cols>0?i-1:i);
  c.focusIndex=i;c.pressKey('Down');
  if(Math.floor(i/cols)===Math.floor(count/cols))assert.equal(c.focusArea,'lens');
  else assert.equal(c.focusIndex,Math.min(count,i+cols));
 }
});
test('actual snapshot and restore preserve new location with old snapshots still valid',()=>{
 let c=world();Object.assign(c,{JSON,Number,String,lens:'watch',chipSelection:{},selectedTitle:'',
  titleHistory:[],query:'',searchReturnState:'home',seeAllKind:'shelf',seeAllShelfId:'',
  seeAllTitle:'',seeAllReturnLink:'',seeAllReturnSnapshot:null,searchResultIndex:-1,
  appManagerIndex:0,content:{forceActiveFocus:()=>{}},seeAllView:{returnSnapshot:()=>null,restoreReturn:()=>{},cancelRestoration:()=>{}},
  homeView:{...c.homeView,navigationSnapshot:()=>({continueSelection:{identity:'a',index:0}}),
  restoreNavigationSnapshot:s=>c.restoredHome=s}});
 for(const n of ['cloneNavigationValue','navigationSnapshot','restoreNavigationSnapshot'])
  vm.runInContext(fn(W,n),c);
 c.focusArea='continue';c.focusIndex=c.activeApps.length;
 let snap=c.navigationSnapshot();assert.equal(snap.focusArea,'continue');assert.equal(snap.home.continueSelection.identity,'a');
 c.focusArea='app';c.focusIndex=0;assert.equal(c.restoreNavigationSnapshot(snap),true);
 assert.equal(c.focusArea,'continue');assert.equal(c.focusIndex,c.activeApps.length);
 assert.equal(c.restoredHome.continueSelection.identity,'a');
 snap.focusArea='shelf';delete snap.home.continueSelection;
 assert.equal(c.restoreNavigationSnapshot(snap),true);assert.equal(c.focusArea,'shelf');
});

test('Retry Enter traverses actual shared activation function and requests once',()=>{
 const K=source('qml/KeyboardAction.qml');
 const at=T.indexOf('objectName: "feriaAvailabilityRetry"');assert(at>=0,'Retry missing');
 const triggered=T.slice(at).match(/onTriggered: (.+)/)[1];
 let calls=0,title={id:'fixture'};
 const c={Qt,controller:{requestAvailability:(it,forced)=>{assert.strictEqual(it,title);assert.strictEqual(forced,true);calls++}},
 root:{it:title},action:{enabled:true,visible:true,focusEnabled:true,focusOnPointer:true,
 forceActiveFocus:()=>{},command:null,spaceActivates:false,contextEnabled:false}};
 vm.createContext(c);c.action.triggered=()=>vm.runInContext(triggered,c);
 vm.runInContext(fn(K,'activate'),c);c.action.activate=c.activate;
 const atKey=K.indexOf('Keys.onPressed:');vm.runInContext('function press(event) '+block(K,atKey),c);
 const event={key:Qt.Key_Return,accepted:false,modifiers:0};c.press(event);assert.equal(calls,1);assert(event.accepted);
});
console.log(JSON.stringify({pass,fail}));process.exitCode=fail?1:0;
