import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const source=fs.readFileSync(new URL('../qml/vinyl/VinylLiveWorld.qml',import.meta.url),'utf8').replaceAll('\r\n','\n');
const hud=source.slice(source.indexOf('    VinylPlayerBar {'));
const rootSignals=source.match(/    Connections \{\n        target: root\n([\s\S]*?)\n    \}/)[1];
const sessionSignals=source.match(/    Connections \{\n        target: session\n([\s\S]*?)\n    \}/)[1];
function fixture(page='album',browsingSong=false) {
    const c={page,browsingSong,pendingVideoOpen:null,visible:true,lifecycleActive:true,canEnterVinyl:true,
        videoFullscreen:false,searchOpen:false,artistSearchOpen:false,routeStack:[],namedCollectionReturn:null,
        worldVisible:false,pendingAlbumRoute:null,pendingDiscoverItem:null,Qt:{callLater(){}},pages:[],
        browseSong:{isCurrent:false,album:{},editionId:'',trackIndex:0},
        world:{rememberCurrentTab(){},restoreRouteState(){}},worldData:{},artistData:{open(){}},artist:{},
        worldScroll:{},album:{},searchSnapshot:null,searchOriginCatalogue:null,
        captureRoute:()=>{c.capturedPending=c.pendingVideoOpen;return {page:c.page,browsingSong:c.browsingSong,edition:{},disc:0}},parkFocus(){},
        enforceTasteGate(){},openArtistOnboarding(){},exitVideoFullscreen(){},revealVideoHud(){},
        videoHudTimer:{stop(){}},videoDisplayed:false,videoHudVisible:true,
        session:{playingIndex:0,activeQueueRow:{queueId:'occurrence-1'},profileScope:'profile-a',profileGeneration:3,
            videoShown:false,videoBusy:false,videoSwitchCancellable:false,hasVideo:true,loaded:true,
            browsing:false,browseOperation:'',paused:false,toggles:0,starts:0,cancels:0,stops:0,
            cancelBrowse(){this.browsing=false},refreshLibrary(){},
            toggleVideo(){
                this.toggles++;
                if(this.videoBusy) {if(this.videoSwitchCancellable){this.videoBusy=false;this.videoSwitchCancellable=false;this.cancels++}return}
                if(this.videoShown){this.videoShown=false;this.stops++;return}
                if(!this.hasVideo || !this.loaded)return;
                this.starts++;this.videoSwitchCancellable=true;this.videoBusy=true;
            }}};
    c.root=c;
    function method(name) {
        const match=source.match(new RegExp('^    function '+name+'\\(([^)]*)\\) \\{([^\\n]*)\\}[ \\t]*$','m'))
            || source.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
        if(match)c[name]=vm.runInNewContext('(function('+match[1]+'){'+match[2]+'\n})',c);
    }
    for(const name of ['cancelPendingVideoOpen','requestHudVideo','finishPendingVideoOpen','pushRoute','showSong','goBack'])method(name);
    const contextBinding=source.match(/readonly property string videoOpenContextKey: ([^\n]+)/);
    Object.defineProperty(c,'videoOpenContextKey',{get:()=>contextBinding?vm.runInNewContext('('+contextBinding[1]+')',c):undefined});
    function runSignal(block,name) {
        const handler=block.match(new RegExp('function on'+name+'Changed\\(\\)\\{([^\\n]*)\\}'));
        if(handler)vm.runInNewContext('(function(){'+handler[1]+'})',c)();
    }
    function runProperty(name) {
        const line=source.match(new RegExp('    on'+name+'Changed: ([^\\n]+)'));
        if(!line)return;
        const code=line[1].trim()==='{' ? source.match(new RegExp('    on'+name+'Changed: \\{([\\s\\S]*?)\\n    \\}'))[1] : line[1];
        vm.runInNewContext('(function(){'+code+'})',c)();
    }
    function watch(owner,key,changed) {
        let value=owner[key];Object.defineProperty(owner,key,{get:()=>value,set:next=>{if(value===next)return;value=next;changed(next)}});
    }
    for(const key of ['page','searchOpen','artistSearchOpen','visible','canEnterVinyl'])watch(c,key,value=>{
        if(key==='page')c.pages.push({page:value,video:c.session.videoShown});runSignal(rootSignals,key[0].toUpperCase()+key.slice(1));
    });
    watch(c,'lifecycleActive',()=>runProperty('LifecycleActive'));
    watch(c,'routeStack',()=>runProperty('RouteStack'));
    for(const key of ['videoShown','videoBusy'])watch(c.session,key,()=>runSignal(sessionSignals,key[0].toUpperCase()+key.slice(1)));
    for(const key of ['activeQueueRow','profileScope','profileGeneration'])watch(c.session,key,()=>{
        runProperty('VideoOpenContextKey');
        if(key==='profileGeneration')runSignal(sessionSignals,'ProfileGeneration');
    });
    const handler=hud.match(/onVideoRequested: \{([^\n]+)\}/);assert.ok(handler,'actual HUD request handler');
    c.clickVideo=vm.runInNewContext('(function(){'+handler[1]+'})',c);
    c.completeVideo=()=>{c.session.videoShown=true;c.session.videoBusy=false;c.session.videoSwitchCancellable=false};
    c.failVideo=()=>{c.session.videoBusy=false;c.session.videoSwitchCancellable=false};
    return c;
}
for(const page of ['album','home','library','artist'])test('lookup stays on '+page+' until native video commit, then enters video directly',()=>{
    const c=fixture(page);c.clickVideo();
    assert.equal(c.page,page,'lookup must not show the cover screen');assert.equal(c.session.starts,1);assert.equal(c.routeStack.length,0);
    c.completeVideo();assert.equal(c.page,'song');assert.equal(c.pages.length,1);assert.equal(c.pages[0].video,true);
    assert.equal(c.routeStack.length,1);assert.equal(c.routeStack[0].page,page);assert.equal(c.pendingVideoOpen,null);
    assert.equal(c.capturedPending,null,'pending intent is cleared before committing the route');
    c.clickVideo();assert.equal(c.page,'song');assert.equal(c.session.videoShown,false);assert.equal(c.routeStack.length,1);
    c.goBack();assert.equal(c.page,page);assert.equal(c.routeStack.length,0,'Back restores the preceding route exactly once');
});
test('a different browsed song stays visible until the playing video commits',()=>{
    const c=fixture('song',true);c.clickVideo();assert.equal(c.browsingSong,true);assert.equal(c.routeStack.length,0);
    c.completeVideo();assert.equal(c.browsingSong,false);assert.equal(c.routeStack.length,1);assert.equal(c.routeStack[0].browsingSong,true);
});
test('ordinary song-room toggles never push history',()=>{
    const c=fixture('song');c.clickVideo();assert.equal(c.routeStack.length,0);c.completeVideo();
    c.clickVideo();assert.equal(c.session.stops,1);assert.equal(c.routeStack.length,0);assert.equal(c.page,'song');
});
test('failure or missing match stays on the original page with no pending navigation',()=>{
    const c=fixture();c.clickVideo();assert.equal(c.page,'album');c.failVideo();assert.equal(c.pendingVideoOpen,null);assert.equal(c.page,'album');
    c.session.hasVideo=false;c.clickVideo();assert.equal(c.pendingVideoOpen,null);assert.equal(c.page,'album');
});
test('repeated busy click cancels lookup and ignores a late commit',()=>{
    const c=fixture();c.clickVideo();assert.equal(c.page,'album');c.clickVideo();
    assert.equal(c.session.cancels,1);assert.equal(c.pendingVideoOpen,null);assert.equal(c.page,'album');
    c.completeVideo();assert.equal(c.page,'album');assert.equal(c.routeStack.length,0);
});
test('stopping an already active video from another page never opens the room',()=>{
    const c=fixture();c.session.videoShown=true;c.clickVideo();assert.equal(c.session.stops,1);assert.equal(c.page,'album');assert.equal(c.routeStack.length,0);
});
for(const field of ['activeQueueRow','profileScope','profileGeneration'])test('late video cannot navigate after '+field+' changes or toggle the new owner',()=>{
    const c=fixture();c.clickVideo();assert.equal(c.page,'album');
    c.session[field]=field==='activeQueueRow'?{queueId:'occurrence-2'}:field==='profileScope'?'profile-b':4;
    assert.equal(c.pendingVideoOpen,null);assert.equal(c.session.toggles,1,'identity change must not toggle the replacement player');
    c.completeVideo();assert.equal(c.page,'album');
});
const navigation=[
    ['pushRoute',c=>c.pushRoute()],['direct page change',c=>{c.page='artist'}],
    ['route change without page change',c=>{c.routeStack=c.routeStack.concat([{page:'album'}])}],
    ['Back on the same page',c=>{c.session.browsing=true;c.session.browseOperation='album';c.goBack()}],
    ...['searchOpen','artistSearchOpen'].map(key=>[key,c=>{c[key]=true}]),
    ...['visible','lifecycleActive','canEnterVinyl'].map(key=>[key,c=>{c[key]=false}])
];
for(const [name,navigate] of navigation)test(name+' cancels pending auto-open before late completion',()=>{
    const c=fixture();c.clickVideo();assert.equal(c.page,'album');navigate(c);
    assert.equal(c.pendingVideoOpen,null);assert.equal(c.session.cancels,1);
    const destination=c.page;c.completeVideo();assert.equal(c.page,destination);
});
test('noncancellable lookup drops navigation intent without toggling playback',()=>{
    const c=fixture();c.clickVideo();assert.equal(c.page,'album');c.session.videoSwitchCancellable=false;c.searchOpen=true;
    assert.equal(c.pendingVideoOpen,null);assert.equal(c.session.toggles,1);c.completeVideo();assert.equal(c.page,'album');
});

test('synchronous late commit during cancellation cannot reclaim the route',()=>{
    const c=fixture();c.clickVideo();
    c.session.toggleVideo=()=>{c.session.toggles++;c.completeVideo()};
    c.searchOpen=true;
    assert.equal(c.pendingVideoOpen,null);assert.equal(c.page,'album');assert.equal(c.routeStack.length,0);
});
