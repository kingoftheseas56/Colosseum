import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const world=fs.readFileSync(new URL('../qml/vinyl/VinylRuntime.qml',import.meta.url),'utf8').replaceAll('\r\n','\n');
function expression(source,pattern,label) { const match=source.match(pattern);assert.ok(match,label);return new vm.Script('('+match[1]+')'); }
function method(name,context) {
    const match=world.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(match,name);return vm.runInNewContext('(function('+match[1]+'){'+match[2]+'\n})',context);
}
const song=world.slice(world.indexOf('    VinylSongPage {'));
const hud=world.slice(world.indexOf('    VinylPlayerBar {'));
const pageY=expression(song,/id:song;x:0;y:([^;]+);width:/,'actual song y binding');
const pageHeight=expression(song,/\n        height:([^\n]+)/,'actual song height binding');
const contentBottom=expression(world,/readonly property real contentBottom: ([\s\S]*?)\n    readonly property bool worldVisible:/,'actual body boundary');
const opacity=expression(hud,/\n        opacity:([^\n]+)/,'actual HUD opacity');
const controlsVisible=expression(song,/\n        videoControlsVisible: ([^\n]+)/,'actual page controls visibility');
const timer=world.slice(world.indexOf('        id: videoHudTimer'));
const timerRunning=expression(timer,/\n        running: ([\s\S]*?)\n        onTriggered:/,'actual idle timer policy');
function fixture() {
    const context={height:720,top:{y:0,height:100},bar:{y:598,visible:true},statusText:{text:''},
        shellDockBottomExtent:0,theme:{margin:54},videoHudHover:{hovered:false},
        videoHudTimer:{restarts:0,stops:0,restart(){this.restarts++},stop(){this.stops++}},
        session:{paused:false,playingIndex:0,cancelBrowse(){},toggleVideo(){this.videoToggles=(this.videoToggles||0)+1}},
        Qt:{callLater(){}},
        videoDisplayed:true,videoFullscreen:false,videoHudVisible:false,visible:true,lifecycleActive:true,
        searchOpen:false,artistSearchOpen:false,page:'song',browsingSong:false,canEnterVinyl:true,
        pushRoute(){this.pushes=(this.pushes||0)+1},parkFocus(){},focusInside(){return false}};
    context.root=context;
    context.pushRoute=()=>{context.pushes=(context.pushes||0)+1};
    Object.defineProperty(context,'y',{get:()=>pageY.runInNewContext(context)});
    Object.defineProperty(context,'contentBottom',{get:()=>contentBottom.runInNewContext(context)});
    context.showSong=method('showSong',context);
    return context;
}
test('normal video keeps the same body above the fixed HUD; fullscreen alone expands',()=>{
    const c=fixture();c.videoDisplayed=false;
    const cover={y:pageY.runInNewContext(c),height:pageHeight.runInNewContext(c)};
    c.videoDisplayed=true;
    assert.equal(pageY.runInNewContext(c),cover.y,'video retains the song origin');
    assert.equal(pageHeight.runInNewContext(c),cover.height,'video retains the song body');
    assert.ok(pageY.runInNewContext(c)+pageHeight.runInNewContext(c)<=c.bar.y-14);
    c.videoFullscreen=true;assert.equal(pageY.runInNewContext(c),0);assert.equal(pageHeight.runInNewContext(c),c.height);
});
test('normal video never arms idle hide, while explicit fullscreen does',()=>{
    const c=fixture(),reveal=method('revealVideoHud',c);
    reveal();assert.equal(c.videoHudVisible,true);assert.equal(c.videoHudTimer.restarts,0);
    assert.equal(timerRunning.runInNewContext(c),false);
    c.videoFullscreen=true;reveal();assert.equal(c.videoHudTimer.restarts,1);
    assert.equal(timerRunning.runInNewContext(c),true);
    c.session.paused=true;reveal();assert.equal(timerRunning.runInNewContext(c),false);
});
test('a stale hidden flag cannot hide normal-video HUD or Back controls',()=>{
    const c=fixture();assert.equal(opacity.runInNewContext(c),1);
    assert.equal(controlsVisible.runInNewContext(c),true);
    c.videoFullscreen=true;assert.equal(opacity.runInNewContext(c),0);
    assert.equal(controlsVisible.runInNewContext(c),false);
});
test('a late fullscreen idle callback cannot hide controls after returning to normal video',()=>{
    const c=fixture();c.videoHudVisible=true;
    const handler=timer.match(/\n        onTriggered: ([^\n]+)/);assert.ok(handler);
    const trigger=new vm.Script(handler[1]);
    trigger.runInNewContext(c);assert.equal(c.videoHudVisible,true);
    c.videoFullscreen=true;trigger.runInNewContext(c);assert.equal(c.videoHudVisible,false);
    c.videoFullscreen=false;method('revealVideoHud',c)();trigger.runInNewContext(c);
    assert.equal(c.videoHudVisible,true);assert.equal(timerRunning.runInNewContext(c),false);
});
test('the actual HUD delegates video opening without eager route or playback changes',()=>{
    const c=fixture();c.page='album';let requests=0;
    c.requestHudVideo=()=>{requests++};
    const handler=hud.match(/onVideoRequested: \{([^\n]+)\}/);assert.ok(handler);
    const toggle=vm.runInNewContext('(function(){'+handler[1]+'})',c);
    toggle();toggle();
    assert.equal(requests,2);assert.equal(c.page,'album');assert.equal(c.pushes||0,0);
    assert.equal(c.session.videoToggles||0,0,'the request owner decides native toggle timing');
});
