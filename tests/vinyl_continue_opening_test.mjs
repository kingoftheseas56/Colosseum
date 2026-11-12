import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const main=fs.readFileSync(new URL('../qml/Main.qml',import.meta.url),'utf8');
const adapter=fs.readFileSync(new URL('../qml/vinyl/VinylLiveSession.qml',import.meta.url),'utf8');
function load(source,name,context) {
    const match=source.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(match, name);
    return vm.runInNewContext('(function('+match[1]+'){'+match[2]+'\n})',context);
}
function fixture() {
    const session={profileActive:true,shuttingDown:false,browseError:'',paused:true,
        queueIndex:0,queue:[{trackId:'old'}],playingEdition:{id:'old'},playingDisc:0,
        plainData:v=>v,refreshLibrary(){},editionSelected(){},snapshot(){},queueSerial:0,
        qsTr:s=>s,progressStore:{get:()=>({}),recent:()=>[]}};
    session.backend={
        checkpointSummary:r=>r.valid ? {row:{}} : {},
        restoreCheckpoint(r) {
            if(!r.valid || r.rejected) return false;
            session.queue=[{trackId:r.trackId}];session.playingEdition={id:r.trackId};session.paused=true;
            return true;
        },
        restoreLegacy(rows,index) {session.queue=rows;session.queueIndex=index;session.paused=true;return true;}
    };
    session.rowFor=(edition,discIndex,trackIndex)=>({edition,discIndex,trackIndex,trackId:edition.discs[discIndex][trackIndex].id});
    session.restore=load(adapter,'restore',session);
    const win={vinylEnabled:true,vinylProfileActive:true,vinylShellGeneration:4,vinylResumeSerial:0,pendingVinylResume:null,
        vinylWorld:{session,page:'home',canEnterVinyl:true,enforceTasteGate(){this.gated=true;}},qsTr:s=>s,
        openWorld(){}};
    win.win=win;
    for(const name of ['traceVinylResume','openVinylResume','hasPendingVinylResume','holdVinylResumePresentation','applyVinylResume'])win[name]=load(main,name,win);
    return {session,win};
}
const entry={title:'Requested song',resume:{version:3,valid:true,trackId:'requested'}};
{
    const {win}=fixture();win.pendingVinylResume={entry,play:true,generation:4};
    assert.equal(win.holdVinylResumePresentation(null),true,'incubation keeps the opening surface');
    win.vinylWorld.canEnterVinyl=false;win.vinylWorld.resumeRequiresArtistSelection=false;
    win.applyVinylResume();assert.equal(win.holdVinylResumePresentation(win.vinylWorld),true,'ready but admission-pending never presents Home');
    win.vinylWorld.resumeRequiresArtistSelection=true;
    assert.equal(win.holdVinylResumePresentation(win.vinylWorld),false,'confirmed onboarding remains visible');
    win.vinylWorld.resumeRequiresArtistSelection=false;win.vinylWorld.canEnterVinyl=true;
    let pending=win.pendingVinylResume;const releasePages=[];
    Object.defineProperty(win,'pendingVinylResume',{get:()=>pending,set:value=>{pending=value;if(!value)releasePages.push(win.vinylWorld.page);}});
    win.applyVinylResume();assert.deepEqual(releasePages,['song'],'presentation releases only after committing requested destination');
    assert.equal(win.holdVinylResumePresentation(win.vinylWorld),false);
    pending={entry:{resume:{version:99}},play:true,generation:4};win.applyVinylResume();
    assert.equal(win.holdVinylResumePresentation(win.vinylWorld),false);assert.ok(win.vinylWorld.session.browseError);
    pending={entry,play:true,generation:4};win.pendingVinylResume=null;
    assert.equal(win.holdVinylResumePresentation(win.vinylWorld),false,'cancellation releases the hold');
}
for(const resume of [{version:3,valid:false},{version:3,valid:true,rejected:true},null,{version:99}]) {
    const {session,win}=fixture();win.openVinylResume({resume},true);
    assert.equal(session.paused,true);assert.equal(session.queue[0].trackId,'old');
    assert.equal(win.vinylWorld.page,'home');assert.ok(session.browseError);
    assert.equal(win.pendingVinylResume,null,'failed request resolves visibly rather than starting old audio');
}
for(const play of [false,true]) {
    const {session,win}=fixture();win.openVinylResume(entry,play);
    assert.equal(session.queue[0].trackId,'requested');assert.equal(session.paused,!play);
    assert.equal(win.vinylWorld.page,play?'song':'album');assert.equal(session.browseError,'');
}
{
    const {win}=fixture();const world=win.vinylWorld;
    Object.assign(world,{browsingSong:true,searchOpen:true,artistSearchOpen:true});
    let pending=null,releaseState;
    Object.defineProperty(win,'pendingVinylResume',{get:()=>pending,set:value=>{
        pending=value;if(!value)releaseState=[world.browsingSong,world.searchOpen,world.artistSearchOpen,world.page];
    }});
    win.openVinylResume({resume:{version:99}},true);
    assert.deepEqual(releaseState,[true,true,true,'home'],'restore failure preserves prior browse/search state');
    win.openVinylResume(entry,true);
    assert.deepEqual(releaseState,[false,false,false,'song'],'successful restore clears stale browse/search context before presentation release');
}
{
    const {session,win}=fixture();
    win.pendingVinylResume={entry,play:true,generation:3};win.applyVinylResume();
    assert.equal(session.queue[0].trackId,'old');assert.equal(session.paused,true);
    win.pendingVinylResume={entry,play:true,generation:4};win.vinylProfileActive=false;win.applyVinylResume();
    assert.equal(session.paused,true);assert.equal(session.queue[0].trackId,'old');
    session.profileActive=false;assert.equal(session.restore(entry),false);
    session.profileActive=true;session.shuttingDown=true;assert.equal(session.restore(entry),false);
}
{
    const {session,win}=fixture();win.vinylWorld.canEnterVinyl=false;
    win.openVinylResume({resume:{version:3,valid:true,trackId:'first'}},true);
    assert.equal(win.hasPendingVinylResume(),true);assert.equal(session.queue[0].trackId,'old');
    win.openVinylResume(entry,false);win.vinylWorld.canEnterVinyl=true;win.applyVinylResume();
    assert.equal(session.queue[0].trackId,'requested');assert.equal(session.paused,true);
}
// Execute the production Loader initial-property setup and its live binding while
// simulating incubation: a request can arrive after setSource and before completion.
{
    const {win}=fixture();let initial;
    const thatObject={kind:'fixture-backdrop'};
    win.worldSourceFor=()=> 'VinylLiveWorld.qml';
    const setup=main.match(/                    var initialProperties = \{([\s\S]*?)                    active = true/);
    assert.ok(setup);
    vm.runInNewContext('(function(){var initialProperties = {'+setup[1]+'})',{
        win,worldStack:{current:'Vinyl'},mode:'Vinyl',wall:thatObject,Qt:{binding:f=>f},
        setSource:(_url,props)=>initial=props,active:false
    })();
    assert.strictEqual(initial.backdrop,thatObject);
    assert.equal(initial.homeLanding,true);assert.equal(initial.deferInitialRestore(),false);
    assert.equal(initial.resumePresentationPending(),false);
    win.vinylWorld=null;win.openVinylResume(entry,true);
    assert.equal(initial.deferInitialRestore(),true);
    assert.equal(initial.resumePresentationPending(),true,'presentation hold is provided before component completion');
    let restored=0;
    const completed=adapter.match(/Component.onCompleted: \{([^\n]*)\}/);
    const startup={restore(){restored++;},refreshLibrary(){},get deferInitialRestore(){return initial.deferInitialRestore();}};
    const finish=vm.runInNewContext('(function(){'+completed[1]+'})',startup);
    finish();assert.equal(restored,0,'explicit request defers the old session at completion');
    win.pendingVinylResume=null;assert.equal(initial.deferInitialRestore(),false,'cancellation releases startup defer');
    assert.equal(initial.resumePresentationPending(),false,'cancellation releases initial presentation binding');
    finish();assert.equal(restored,1,'ordinary completion still restores the default');
}
// Exercise the legacy migration and ordinary default restore production paths.
{
    const {session}=fixture();
    session.progressStore.get=()=>({kind:'music-session',id:'vinyl',resume:entry.resume});
    assert.equal(session.restore(),true);assert.equal(session.queue[0].trackId,'requested');
    const edition={discs:[[{id:'legacy'}]]};
    assert.equal(session.restore({resume:{version:1,queue:[0],edition,queueIndex:0,trackId:'legacy',position:8}}),true);
    assert.equal(session.queue[0].trackId,'legacy');assert.equal(session.paused,true);
    session.progressStore.get=()=>({kind:'music-session',id:'vinyl',resume:{version:2,queue:[],queueIndex:-1,position:0}});
    assert.equal(session.restore(),true);assert.equal(session.queue.length,0);
}
{
    const {win}=fixture();win.pendingVinylResume={entry,play:true,generation:4};
    Object.assign(win,{androidHost:false,guardNamedCollectionsNavigation:()=>false,cancelCatalogueRoute(){},
        parkFeria(){},refreshWallpaper(){},armHomeIntroWidgets(){},armHomeContinueRail(){},
        worldStack:{current:'Vinyl'},setGuiStallContext(){},currentSurface:'Vinyl'});
    load(main,'closeWorld',win)();assert.equal(win.pendingVinylResume,null,'Back cancels an incubating request');
    assert.equal(win.worldStack.current,'');
}
{
    const {win,session}=fixture();const messages=[],contexts=[];
    win.CatalogueTraceEnabled=true;win.console={info:m=>messages.push(m)};
    win.setGuiStallContext=(operation,surface)=>contexts.push([operation,surface]);
    win.openVinylResume(entry,false);
    assert.equal(messages.length,3);assert.match(messages[0],/phase=pending serial=1 entryId=unavailable track=requested/);
    assert.deepEqual(contexts,[['vinyl-pending','Vinyl'],['vinyl-restore','Vinyl'],['vinyl-commit','Vinyl']]);
    // Real v3 checkpoints omit top-level trackId: pending identifies the entry,
    // while commit identifies the song actually selected by the native backend.
    messages.length=0;
    session.playingTrack={id:'old-track'};
    session.backend.restoreCheckpoint=()=>{session.playingTrack={id:'actual-requested-track'};return true;};
    win.openVinylResume({id:'requested-entry',resume:{version:3,valid:true,backendState:'native-checkpoint'}},false);
    assert.match(messages[0],/phase=pending serial=2 entryId=requested-entry track=unavailable/);
    assert.match(messages[1],/phase=restore serial=2 entryId=requested-entry track=unavailable/);
    assert.match(messages[2],/phase=commit serial=2 entryId=requested-entry track=actual-requested-track/);
    session.root=session;session.CatalogueTraceEnabled=true;session.console=win.console;
    session.playingTrack={id:'requested'};session.sourceState='resolving';session.loaded=false;
    const phase=load(adapter,'tracePlaybackPhase',session);phase('source-state');
    assert.match(messages.at(-1),/phase=source-state track=requested sourceState=resolving loaded=false paused=true atMs=\d+/);
    session.playingTrack={id:'https://invalid.test/private'};phase('active-song');
    assert.match(messages.at(-1),/track=unavailable/);assert.ok(!messages.at(-1).includes('https:'));
}
console.log('PASS Continue failure/profile fences, selected song/play intent, newest taste-gated request, live incubation startup binding, default/legacy restoration');
