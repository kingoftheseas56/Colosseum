import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = new URL('../qml/feria/FeriaAppMode.js', import.meta.url);
assert.ok(fs.existsSync(source), 'Provider app layouts and spatial navigation must exist');
const adapter = vm.createContext({});
vm.runInContext(fs.readFileSync(source, 'utf8'), adapter);
function page(host = 'www.youtube.com', path = '/') {
    const elements = new Map(), attributes = new Map(), listeners = new Map();
    const nodes = [];
    const dom = {
        documentElement: {setAttribute:(k,v) => attributes.set(k,v), removeAttribute:k => attributes.delete(k)},
        head: {appendChild:node => elements.set(node.id,node)},
        createElement:() => ({remove() { elements.delete(this.id); }}),
        getElementById:id => elements.get(id),
        querySelector:() => dom.password ? {} : null,
        querySelectorAll:selector => selector === 'video,audio' ? (dom.playing ? [{paused:false,ended:false}] : [])
            : selector.startsWith('input[type="password"]') ? (dom.password ? [{getBoundingClientRect:() => ({width:dom.hiddenPassword ? 0 : 100,height:50}),closest:() => null}] : []) : nodes,
        addEventListener:(name,handler) => listeners.set(name,handler),
        removeEventListener:(name,handler) => { if (listeners.get(name) === handler) listeners.delete(name); },
        activeElement:null,
        password:false, playing:false
    };
    const context = vm.createContext({document:dom, location:{hostname:host,pathname:path}, window:{},
        innerWidth:1280, innerHeight:720, getComputedStyle:() => ({visibility:'visible',display:'block'}),
        Math, JSON});
    function button(x,y,kind = '') {
        const node = {tagName:kind || 'BUTTON',disabled:false,isContentEditable:false,
            closest:() => kind ? {} : null, getAttribute:() => null,
            getBoundingClientRect:() => ({x,y,left:x,top:y,right:x+100,bottom:y+60,width:100,height:60}),
            focus() { dom.activeElement = this; }, scrollIntoView() { this.revealed = true; }};
        nodes.push(node); return node;
    }
    function run(layout=true,keys=true,provider='youtube') {
        return vm.runInContext(adapter.script(provider,layout,keys,'#f2c94c'),context);
    }
    function key(value) {
        const event = {key:value,target:dom.activeElement,defaultPrevented:false,altKey:false,ctrlKey:false,metaKey:false,shiftKey:false,
            preventDefault() { this.defaultPrevented = true; }, stopPropagation() {}};
        listeners.get('keydown')?.(event); return event;
    }
    return {run,key,button,dom,context,elements,attributes,listeners};
}
for (const [provider,host] of Object.entries({netflix:'www.netflix.com',prime:'www.primevideo.com',disney:'www.disneyplus.com',
    hbomax:'play.hbomax.com',appletv:'tv.apple.com',crunchyroll:'www.crunchyroll.com',youtube:'www.youtube.com',
    hulu:'www.hulu.com',mubi:'mubi.com',spotify:'open.spotify.com',ytmusic:'music.youtube.com',applemusic:'music.apple.com',
    kindle:'read.amazon.com',playbooks:'play.google.com',mangaplus:'mangaplus.shueisha.co.jp',viz:'www.viz.com',
    webtoon:'www.webtoons.com',dcui:'www.dcuniverseinfinite.com',kobo:'www.kobo.com'})) {
    assert.equal(page(host).run(true,true,provider).adapted,true,`${provider} gets an app layout`);
}
assert.equal(page('youtube.com.evil.test').run().adapted,false,'Host suffix boundary');
assert.equal(page('accounts.google.com').run().adapted,false,'External sign-in stays unchanged');
assert.equal(page('www.youtube.com','/signin').run().adapted,false,'Sign-in route stays unchanged');
assert.equal(page('www.netflix.com','/payment').run(true,true,'netflix').adapted,false,'Payment stays unchanged');
const p = page();
const first=p.button(20,20), right=p.button(150,20), below=p.button(20,140);
first.focus(); p.run();
assert.equal(p.key('ArrowRight').defaultPrevented,true);
assert.equal(p.dom.activeElement,right,'Right moves to the closest control to the right');
first.focus(); p.key('ArrowDown');
assert.equal(p.dom.activeElement,below,'Down follows visible geometry');
assert.equal(below.revealed,true,'Selected control is revealed');
assert.equal(p.key('Enter').defaultPrevented,false,'Native activation is preserved');
const input=p.button(20,240,'INPUT'); input.focus();
assert.equal(p.key('ArrowLeft').defaultPrevented,false,'Typing and input caret remain native');
first.focus(); p.dom.playing=true; p.context.location.pathname='/watch';
assert.equal(p.key('ArrowRight').defaultPrevented,false,'Media seek/volume keys remain native');
p.context.location.pathname='/';
assert.equal(p.key('ArrowRight').defaultPrevented,true,'Autoplay previews do not disable catalogue navigation');
p.dom.playing=false; p.run(true,false);
assert.equal(p.key('ArrowRight').defaultPrevented,false,'Keyboard adaptation can be disabled');
assert.ok(p.elements.size > 0,'App layout survives keyboard opt-out');
p.run(false,false);
assert.equal(p.elements.size,0,'Original site layout is restored');
assert.equal(p.attributes.size,0,'App marker is removed');
assert.equal(p.listeners.size,0,'Navigation handler is removed');
p.run(); p.dom.password=true; p.run();
assert.equal(p.elements.size,0,'SPA sign-in removes app styling');
assert.equal(p.listeners.size,0,'SPA sign-in removes keyboard adaptation');
p.dom.hiddenPassword=true; p.run();
assert.ok(p.elements.size > 0,'Hidden sign-in templates do not disable app layouts');
const reader=page('www.webtoons.com','/en/series/viewer');
reader.button(20,20).focus(); reader.button(150,20); reader.run(true,true,'webtoon');
assert.equal(reader.key('ArrowRight').defaultPrevented,false,'Reader chapter/page keys remain native');
console.log('Feria app mode: 19 provider scopes, authentication, restoration and spatial navigation passed');
