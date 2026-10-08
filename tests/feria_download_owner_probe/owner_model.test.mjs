// Executes real QML-owner JavaScript with modeled Qt boundaries. This is not a
// native engine/source-lifetime qualification. Runtime fixtures are separate.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';
const file = new URL('../../qml/feria/FeriaBrowserDownloads.qml', import.meta.url);
const text = fs.readFileSync(process.env.FERIA_DOWNLOAD_OWNER || file, 'utf8');
function ownerFunction(name) {
    const start = text.indexOf('    function ' + name + '(');
    assert.notEqual(start, -1, `missing owner operation ${name}`);
    const lineEnd = text.indexOf('\n', start);
    const line = text.slice(start, lineEnd);
    const end = line.trimEnd().endsWith('}') ? lineEnd : text.indexOf('\n    }', lineEnd) + 6;
    return text.slice(start, end);
}
function receiver() {
    const scheduled=[];
    const root = {
        active: true, retired: false, items: [], profile: {offTheRecord:false},
        privateBrowsing: false, accountStore: null, profileGeneration: 1,
        ownerBinding: 'owner-model', requestSerial: 0, decisionSerial:0,
        seenNativeIds: {}, sourceResolver: null, destinationPolicy:'autoSave',
        policy: null, panelOpen:false, rows:[], events:[],
        message(v) { root.events.push(v); },
        publish() {}, pruneFinished() {}, registerOwner() {}, nativeSaveStateChanged() {}, requestAvailable:r=>!!r, watchRequest:()=>true,
        sourceForRequest() { return {}; },
        createEntry(request, source) { const entry = {request, source, downloadId:'id-'+root.items.length,
            acceptDecision() { request.accept(); }, scheduleUpdate() {},
            snapshot() { return {}; }}; return entry; }
    };
    const request = {id:7, downloadFileName:'fixture.bin', downloadDirectory:'/fixture', url:'http://fixture/download',
        state:0, isFinished:false, accepts:0, cancels:0,
        accept(){this.accepts++;this.state=1;}, cancel(){this.cancels++;this.state=3;this.isFinished=true;}};
    Object.defineProperty(request,'view',{get(){throw new Error('UNSAFE_VIEW_GETTER_EVALUATED');}});
    const context = vm.createContext({...root, root, WebEngineDownloadRequest:{DownloadRequested:0,DownloadCompleted:2,DownloadCancelled:3},
        qsTr:s=>s, downloadEntry:{createObject:(_,p)=>root.createEntry(p.request,p.source)},
        Qt:{callLater:fn=>scheduled.push(fn)}, console});
    // Existing and candidate function free variables refer to QML root properties.
    for (const key of Object.keys(root)) Object.defineProperty(context,key,{get:()=>root[key],set:v=>{root[key]=v;},configurable:true});
    vm.runInContext(ownerFunction('receive'),context);
    return {root,request,receive:context.receive,context,scheduled,flush(){while(scheduled.length)scheduled.shift()();}};
}
test('native admission never invokes the raw-backed Quick source getter',()=>{
    const {root,request,receive}=receiver();
    receive(request);
    assert.equal(request.accepts,1);
    assert.equal(root.items.length,1);
});
test('duplicate delivery of the same native request is admitted once',()=>{
    const {root,request,receive}=receiver();
    receive(request);receive(request);
    assert.equal(request.accepts,1);
    assert.equal(root.items.length,1);
});
test('retired owner cancels without reading source or admitting',()=>{
    const {root,request,receive}=receiver();root.active=false;
    receive(request);assert.equal(request.accepts,0);assert.equal(request.cancels,1);
});
function entryFunction(name) {
    const start = text.indexOf('            function ' + name + '(');
    assert.notEqual(start, -1, `missing entry operation ${name}`);
    const endOfLine = text.indexOf('\n', start);
    const line = text.slice(start,endOfLine);
    const end = line.trimEnd().endsWith('}') ? endOfLine : text.indexOf('\n            }',endOfLine)+14;
    return text.slice(start,end);
}
function entryRuntime(overrides={}) {
    const scheduled=[];
    const request={receivedBytes:16,totalBytes:100,mimeType:'application/octet-stream',accepts:0,cancels:0,pauses:0,resumes:0,
        accept(){this.accepts++;},cancel(){this.cancels++;},pause(){this.pauses++;},resume(){this.resumes++;}};
    const store={attempts:0,releases:0,rows:[],outcome:'recorded',
        recordBrowserDownloadResult(row){this.attempts++;if(this.outcome==='recorded')this.rows.push(row);return {status:this.outcome};},
        cancelBrowserDownload(){this.releases++;}};
    const root={profile:{},accountStore:store,active:true,retired:false,policy:null,publish(){},pruneFinished(){},settleNativeSave(){},message(){},requestAvailable:r=>!!r};
    const e={request,root,metadataStore:store,owningProfile:root.profile,invalidated:false,recorded:false,completed:false,
        cancelledTerminal:false,nativeState:1,nativeFinished:false,admission:{},metadataReason:'',metadataState:'pending',downloadToken:'grant',privateDownload:false,downloadId:'p1:7',profileId:'p1',nativeId:'7',sourceUrl:'http://fixture/file',
        path:'/fixture/file',name:'file',sessionId:'space-a',visitToken:'visit-a',fileSnapshot:{},notified:true,settlePending:false,
        state:1,canPause:true,canResume:false,canCancel:true,actionResults:{},cancellationRequested:false,...overrides};
    const ctx=vm.createContext({root,entry:e,Qt:{callLater:fn=>scheduled.push(fn)},qsTr:s=>s,WebEngineDownloadRequest:{DownloadRequested:0,DownloadInProgress:1,DownloadCompleted:2,DownloadCancelled:3,DownloadInterrupted:4}});
    for(const key of Object.keys(e))Object.defineProperty(ctx,key,{get:()=>e[key],set:v=>{e[key]=v;},configurable:true});
    for(const name of ['releaseAuthorization','observeNativeState','acceptDecision','destinationMatchesAdmission','commitMetadata','update','scheduleUpdate','performAction']) {
        vm.runInContext(entryFunction(name),ctx);e[name]=ctx[name];
    }
    return {e,request,store,root,scheduled,flush(){while(scheduled.length)scheduled.shift()();}};
}
test('completion signal coalesces final native byte counters before one-use history write',()=>{
    const r=entryRuntime();r.e.completed=true;r.e.scheduleUpdate();
    assert.equal(r.store.attempts,0);
    r.request.receivedBytes=100;r.request.totalBytes=100;r.e.scheduleUpdate();
    r.flush();assert.equal(r.store.rows[0].receivedBytes,100);assert.equal(r.store.attempts,1);
    r.e.scheduleUpdate();r.flush();assert.equal(r.store.attempts,1);
});
test('retryable write retains same grant; explicit retry consumes once',()=>{
    const r=entryRuntime({completed:true});r.store.outcome='retryable';r.e.commitMetadata();
    assert.equal(r.e.downloadToken,'grant');assert.equal(r.e.metadataState,'retryable');assert.equal(r.store.releases,0);
    r.e.update();assert.equal(r.store.attempts,1,'a native callback must not automatically retry disk failure');
    r.store.outcome='recorded';r.e.commitMetadata();assert.equal(r.store.rows.length,1);assert.equal(r.e.downloadToken,'');
});
test('invalidated and rejected metadata do not retain retry grants',()=>{
    for(const status of ['invalidated','rejected']) {
        const r=entryRuntime({completed:true});r.store.outcome=status;r.e.commitMetadata();
        assert.equal(r.e.metadataState,status);assert.equal(r.e.downloadToken,'');r.e.update();assert.equal(r.store.attempts,1);
    }
});
test('unattributed completion explicitly reports blocked source identity',()=>{
    const r=entryRuntime({completed:true,downloadToken:''});r.e.commitMetadata();
    assert.equal(r.e.metadataState,'blockedSourceIdentity');assert.equal(r.store.attempts,0);
});
test('private completion never writes ordinary metadata',()=>{
    const r=entryRuntime({completed:true,privateDownload:true,downloadToken:''});r.e.commitMetadata();
    assert.equal(r.e.metadataState,'private');assert.equal(r.store.attempts,0);
});
test('action operation replay is one native attempt and cannot change action',()=>{
    const r=entryRuntime();assert.equal(r.e.performAction('op-a','pause').status,'dispatched');
    assert.equal(r.e.performAction('op-a','pause').status,'dispatched');assert.equal(r.request.pauses,1);
    assert.equal(r.e.performAction('op-a','cancel').status,'invalidated');assert.equal(r.request.cancels,0);
});
test('unsupported resume stays blocked and dispatched resume is not success',()=>{
    const r=entryRuntime({canResume:false});assert.equal(r.e.performAction('op-a','resume').status,'blocked');
    r.e.canResume=true;assert.equal(r.e.performAction('op-b','resume').status,'dispatched');assert.equal(r.request.resumes,1);
    assert.equal(r.e.completed,false);
});
function rootRuntime(initial,names) {
    const state={items:[],draining:[],rows:[],retired:false,active:true,profile:{},seenNativeIds:{},profileGeneration:1,
        pageSaveIntent:null,nativeSaveIntents:{},nativeSaveOutcome:{},panelOpen:true,idleProfileDetached:false,
        publish(){},registerOwner(){},requestAvailable:r=>!!r,nativeSaveStateChanged(){},downloadDependenciesChanged(){},...initial};
    const ctx=vm.createContext({root:state,qsTr:s=>s,WebEngineDownloadRequest:{DownloadRequested:0,DownloadInProgress:1,DownloadCompleted:2,DownloadCancelled:3,DownloadInterrupted:4}});
    for(const key of Object.keys(state))Object.defineProperty(ctx,key,{get:()=>state[key],set:v=>{state[key]=v;},configurable:true});
    for(const name of names){vm.runInContext(ownerFunction(name),ctx);state[name]=ctx[name];}
    return state;
}
test('settled private row survives idle profile retirement but account retirement clears it',()=>{
    const item={downloadId:'p1:7',completed:true,fileSnapshot:{path:'/fixture/private'},request:{},owningProfile:{},detached:false,
        dependsOnProfile:()=>false,releaseAuthorization(){},destroy(){},snapshot(){return {downloadId:this.downloadId};}};
    const r=rootRuntime({items:[item]},['findEntry','resolveDownloadFile','retire','resetProfile','detachCompletedProfile']);
    assert.equal(r.detachCompletedProfile(),true);r.profile=null;r.active=false;r.resetProfile();
    assert.equal(r.items.length,1);assert.equal(r.panelOpen,true);assert.equal(item.request,null);assert.equal(item.owningProfile,null);
    assert.equal(r.resolveDownloadFile('p1:7').path,'/fixture/private');
    r.retire('account-retired');assert.equal(r.items.length,0);assert.deepEqual(Object.keys(r.resolveDownloadFile('p1:7')),[]);
});
test('pending settled snapshot prevents private profile detachment',()=>{
    const r=rootRuntime({items:[{dependsOnProfile:()=>true,releaseAuthorization(){},completed:false,request:{},destroy(){}}]},['detachCompletedProfile']);
    assert.equal(r.detachCompletedProfile(),false);assert.equal(r.idleProfileDetached,false);
});
test('wrong profile parent cannot be admitted',()=>{
    const {root,request,receive}=receiver();root.policy={requestBelongsToProfile:()=>false};
    receive(request);assert.equal(request.accepts,0);assert.equal(root.items.length,0);assert.equal(request.cancels,1);
});
test('unknown byte total is retained through metadata rather than fabricated zero',()=>{
    const r=entryRuntime({completed:true});r.request.totalBytes=-1;r.e.commitMetadata();assert.equal(r.store.rows[0].totalBytes,-1);
});
test('invalidated PageSave slot cannot be reused by a newer operation',()=>{
    const profile={};const browser={profile,navigationGeneration:3,accountSessionId:'a',accountVisitToken:'v'};
    const r=rootRuntime({profile,pageSaveIngressExclusive:true,nativeSaveBlockedReason:'',ownerBinding:'owner',requestSerial:0},
        ['findEntry','beginNativeSave','invalidateNativeSave']);
    const old=r.beginNativeSave(browser,{kind:'page',operationId:'old',binding:{spaceId:'a'}});assert.ok(old);
    r.invalidateNativeSave(browser,'navigation');browser.navigationGeneration=4;
    assert.equal(r.beginNativeSave(browser,{kind:'page',operationId:'new'}),'');assert.equal(r.pageSaveIntent.token,old);
});
test('account scope rebind requires current empty profile and never relabels existing rows',()=>{
    let registered=0;
    const policy={storageRoot:'/account-b/browser',registerDownloadOwner(){registered++;return true;}};
    const r=rootRuntime({retired:true,profile:{accountRoot:'/account-a/browser'},policy,accountStore:{}},['rebindAccountScope']);
    assert.equal(r.rebindAccountScope(),false);assert.equal(registered,0);
    r.profile={accountRoot:policy.storageRoot};r.items=[{downloadId:'old'}];
    assert.equal(r.rebindAccountScope(),false);assert.equal(registered,0);
    r.items=[];assert.equal(r.rebindAccountScope(),true);assert.equal(registered,1);assert.equal(r.retired,false);
});

test('post-dispatch PageSave rehost preserves document ownership but replacement invalidates it',()=>{
    const binding={accountEpoch:1,profileId:'profile',spaceId:'space',pageGeneration:1,documentGeneration:2,hostId:'embedded',hostGeneration:1};
    const profile={};const browser={profile,navigationGeneration:2,saveBinding:{...binding,hostId:'window',hostGeneration:2}};
    const r=rootRuntime({profile,pageSaveIntent:{browser,profile,navigationGeneration:2,source:binding,invalidated:false}},['pageSaveSourceCurrent']);
    assert.equal(r.pageSaveSourceCurrent(),true);
    browser.saveBinding.documentGeneration=3;assert.equal(r.pageSaveSourceCurrent(),false);
});
test('rejected page offer waits for observed cancellation then releases slot without replay',()=>{
    const r=receiver();r.root.pageSaveIngressExclusive=true;r.root.pageSaveIntent={token:'old',source:{operationId:'old'},downloadId:'',invalidated:true};
    r.root.rejectedPageRequest=null;r.root.rejectedPageReason='';r.root.nativeSaveOutcome={};r.root.nativeSaveStateChanged=()=>{};
    r.root.pageSaveSourceCurrent=()=>false;r.request.isSavePageDownload=true;r.request.savePageFormat=0;
    for(const key of ['pageSaveIngressExclusive','pageSaveIntent','rejectedPageRequest','rejectedPageReason','nativeSaveOutcome','nativeSaveStateChanged','pageSaveSourceCurrent'])
        Object.defineProperty(r.context,key,{get:()=>r.root[key],set:v=>{r.root[key]=v;},configurable:true});
    for(const name of ['rejectPageOffer','settleRejectedPageOffer']){vm.runInContext(ownerFunction(name),r.context);r.root[name]=r.context[name];}
    r.receive(r.request);assert.equal(r.request.cancels,1);assert.ok(r.root.pageSaveIntent);assert.equal(r.root.items.length,0);
    r.flush();assert.equal(r.root.pageSaveIntent,null);assert.equal(r.root.nativeSaveOutcome.status,'invalidated');
    r.root.pageSaveIntent={token:'new',source:{operationId:'new'},downloadId:'',invalidated:false};r.root.pageSaveSourceCurrent=()=>true;
    r.receive(r.request);assert.equal(r.root.pageSaveIntent.token,'new');assert.equal(r.root.pageSaveIntent.downloadId,'');assert.equal(r.request.accepts,0);
});
test('preterminal request loss is failed while completed intentional detachment remains successful',()=>{
    const r=entryRuntime({completed:false,finished:false,detached:false,requestLost:false});
    r.root.failNativeSaveDownload=()=>{};
    const ctx=vm.createContext({root:r.root,entry:r.e});
    for(const key of Object.keys(r.e))Object.defineProperty(ctx,key,{get:()=>r.e[key],set:v=>{r.e[key]=v;},configurable:true});
    vm.runInContext(entryFunction('markRequestLost'),ctx);ctx.markRequestLost();
    assert.equal(r.e.requestLost,true);assert.equal(r.e.request,null);assert.equal(r.e.metadataState,'invalidated');assert.equal(r.e.completed,false);
    const done=entryRuntime({completed:true,finished:true,detached:true,requestLost:false});
    const dctx=vm.createContext({root:done.root,entry:done.e});
    for(const key of Object.keys(done.e))Object.defineProperty(dctx,key,{get:()=>done.e[key],set:v=>{done.e[key]=v;},configurable:true});
    vm.runInContext(entryFunction('markRequestLost'),dctx);dctx.markRequestLost();assert.equal(done.e.requestLost,false);assert.equal(done.e.completed,true);
});
test('observed cancellation never resurrects Open or metadata on late native completion',()=>{
    const r=entryRuntime({cancelledTerminal:false,state:1,nativeState:1,nativeFinished:false});
    Object.defineProperty(r.e,'completed',{get(){return r.e.state===2 && !r.e.cancelledTerminal;},configurable:true});
    r.root.policy={completedDownloadFile(){throw new Error('cancelled transfer must not mint file authority');}};
    r.request.state=3;r.request.isFinished=true;r.e.update();
    assert.equal(r.e.cancelledTerminal,true);assert.equal(r.e.metadataState,'cancelled');
    r.request.state=2;r.request.receivedBytes=100;r.e.update();
    assert.equal(r.e.state,3);assert.equal(r.e.nativeState,2);assert.equal(r.e.completed,false);
    assert.equal(r.store.attempts,0);assert.deepEqual(Object.keys(r.e.fileSnapshot),[]);
});

test('flagged unknown-format resource cannot consume a pending native page package',()=>{
    const r=receiver();r.root.pageSaveIngressExclusive=true;r.root.pageSaveIntent={token:'package',source:{operationId:'save'},downloadId:'',invalidated:false};
    r.root.pageSaveSourceCurrent=()=>true;r.root.nativeSaveBlockedReason='';r.request.isSavePageDownload=true;r.request.savePageFormat=-1;
    for(const key of ['pageSaveIngressExclusive','pageSaveIntent','pageSaveSourceCurrent','nativeSaveBlockedReason'])
        Object.defineProperty(r.context,key,{get:()=>r.root[key],set:v=>{r.root[key]=v;},configurable:true});
    r.receive(r.request);assert.equal(r.request.accepts,0);assert.equal(r.request.cancels,1);
    assert.equal(r.root.pageSaveIntent.token,'package');assert.equal(r.root.pageSaveIntent.downloadId,'');
    assert.equal(r.root.items.length,0);assert.equal(r.request.savePageFormat,-1);
});

test('destination decisions are one-use and distinct from metadata grants',()=>{
    const r=entryRuntime({decisionToken:'destination-token',decisionConsumed:false,downloadToken:'',admission:{}});
    r.request.state=0;r.request.downloadDirectory='/fixture';r.request.downloadFileName='file';
    r.store.mints=0;r.store.authorizeBrowserDownload=function(){this.mints++;return 'metadata-grant';};
    assert.equal(r.e.acceptDecision('wrong-token',{}).status,'invalidated');assert.equal(r.store.mints,0);assert.equal(r.request.accepts,0);
    assert.equal(r.e.acceptDecision('destination-token',{}).status,'dispatched');assert.equal(r.store.mints,1);assert.equal(r.request.accepts,1);
    assert.equal(r.e.downloadToken,'metadata-grant');assert.notEqual(r.e.downloadToken,r.e.decisionToken);
    assert.equal(r.e.acceptDecision('destination-token',{}).status,'invalidated');assert.equal(r.request.accepts,1);
});
test('source envelope is immutable after admission capture',()=>{
    const source={accountEpoch:1,profileId:'canonical',spaceId:'original',documentGeneration:3};
    const r=rootRuntime({sourceResolver:()=>source},['sourceForRequest']);
    const captured=r.sourceForRequest({isSavePageDownload:false});source.spaceId='selected-other';source.documentGeneration=4;
    assert.equal(captured.spaceId,'original');assert.equal(captured.documentGeneration,3);
});
test('rejected page slot stays pending while native cancellation remains unfinished',()=>{
    const request={isFinished:false,state:1,cancel(){}};
    const r=rootRuntime({pageSaveIngressExclusive:true,pageSaveIntent:{source:{operationId:'save'},downloadId:'',invalidated:false},
        rejectedPageRequest:request,rejectedPageReason:'destination'},['settleRejectedPageOffer']);
    r.settleRejectedPageOffer();assert.ok(r.pageSaveIntent);
    request.state=3;request.isFinished=true;r.settleRejectedPageOffer();assert.equal(r.pageSaveIntent,null);
});
test('rejected page authority cannot become succeeded on a late Completed wrapper',()=>{
    for(const reason of ['source-invalidated','destination-rejected']) {
        const request={isFinished:true,state:2};
        const r=rootRuntime({pageSaveIntent:{source:{operationId:'old'},downloadId:'',invalidated:false},
            rejectedPageRequest:request,rejectedPageReason:reason},['settleRejectedPageOffer']);
        r.settleRejectedPageOffer();assert.equal(r.nativeSaveOutcome.status,reason==='source-invalidated'?'invalidated':'rejected');
        assert.equal(r.nativeSaveOutcome.nativeState,2);
    }
});
test('settled native destination mismatch cannot mint file or history authority',()=>{
    const r=entryRuntime({completed:true,admission:{path:'/fixture/approved.bin',directory:'/fixture',fileName:'approved.bin'}});
    let seals=0;r.root.policy={completedDownloadFile(){seals++;return {path:'/fixture/approved.bin'};}};
    r.request.state=2;r.request.isFinished=true;r.request.downloadDirectory='/fixture';r.request.downloadFileName='different.bin';
    r.e.update();assert.equal(seals,0);assert.equal(r.store.attempts,0);assert.equal(r.e.completed,true);
    assert.equal(r.e.metadataState,'rejected');assert.equal(r.e.metadataReason,'native-destination-changed');
    assert.deepEqual(Object.keys(r.e.fileSnapshot),[]);
});
test('native destruction callback clears draining handle without affecting newer profile row',()=>{
    const old={downloadId:'profile-1:7',request:{},invalidated:true,markRequestLost(){assert.equal(this.invalidated,true);}};
    const current={downloadId:'profile-2:7',request:{},markRequestLost(){this.failed=true;}};
    const currentHandle=current.request;
    const r=rootRuntime({items:[current],draining:[old]},['nativeRequestDestroyed']);
    r.nativeRequestDestroyed('profile-1:7');assert.equal(old.request,null);assert.equal(current.request,currentHandle);
    r.draining=[];r.nativeRequestDestroyed('profile-1:7');assert.equal(current.request,currentHandle);
    r.nativeRequestDestroyed('profile-2:7');assert.equal(current.request,null);assert.equal(current.failed,true);
});
