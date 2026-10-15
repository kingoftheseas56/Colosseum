import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const scope = vm.createContext({});
vm.runInContext(fs.readFileSync(new URL('../qml/feria/FeriaPlayback.js', import.meta.url), 'utf8'), scope);
assert.equal(scope.sameDestination('https://provider.test/watch?id=1', 'https://provider.test/watch?id=1'), true);
assert.equal(scope.sameDestination('https://provider.test/login', 'https://provider.test/watch?id=1'), false);
assert.equal(scope.sameDestination('https://provider.test/watch?id=2', 'https://provider.test/watch?id=1'), false);
assert.equal(scope.sameDestination('https://provider.test/#/episode/2', 'https://provider.test/#/episode/1'), false);
const paused = {paused:true, readyState:4, duration:1000, currentTime:0};
const playing = {paused:false, readyState:4, duration:100, currentTime:5};
scope.document = {querySelectorAll:() => [paused, playing], querySelector:() => null};
assert.equal(vm.runInContext(scope.seek(40), scope), true);
assert.equal(playing.currentTime, 40);
assert.equal(paused.currentTime, 0);
scope.document.querySelector = () => ({});
assert.equal(vm.runInContext(scope.seek(60), scope), false);
assert.equal(playing.currentTime, 40);
console.log('Feria resume destination, active-player selection, and ad guard checks pass');
assert.equal(scope.sameDestination('https://www.netflix.com/watch/123?trackId=9', 'https://www.netflix.com/watch/123'), true);
assert.equal(scope.sameDestination('https://youtube.com/watch?v=abc&utm_source=test', 'https://youtube.com/watch?v=abc'), true);
assert.equal(scope.sameDestination('https://provider.test/read?page=2', 'https://provider.test/read?page=1'), false);
assert.equal(scope.sameDestination('https://other.test/watch?id=1', 'https://provider.test/watch?id=1'), false);
assert.equal(scope.sameDestination('https://provider.test/watch?id=1&edition=B', 'https://provider.test/watch?id=1&edition=A'), false);
scope.document.querySelector = () => null;
const primed = {paused:true, readyState:1, duration:10749, currentTime:0};
scope.document.querySelectorAll = () => [primed];
assert.equal(vm.runInContext(scope.seek(621), scope), false, 'Do not seek Prime\'s metadata-only player before playback starts');
assert.equal(primed.currentTime, 0);
primed.paused = false;
assert.equal(vm.runInContext(scope.seek(621), scope), false, 'Wait for playable data, even after play is requested');
primed.readyState = 4;
assert.equal(vm.runInContext(scope.seek(621), scope), true);
assert.equal(primed.currentTime, 621);
assert.equal(scope.canResume({paused:true, readyState:1, duration:10749, ad:false}), false);
assert.equal(scope.canResume({paused:false, readyState:4, duration:15, ad:false}), false);
assert.equal(scope.canResume({paused:false, readyState:4, duration:10749, ad:true}), false);
assert.equal(scope.canResume({paused:false, readyState:4, duration:10749, ad:false}), true);
const nested = {paused:false, readyState:4, duration:120, currentTime:5, tagName:'VIDEO', ended:false};
const shadow = {querySelectorAll:s => s === 'video,audio' ? [nested] : [], querySelector:() => null};
scope.document.querySelectorAll = s => s === '*' ? [{shadowRoot:shadow}] : [];
assert.equal(vm.runInContext(scope.seek(55), scope), true);
assert.equal(nested.currentTime, 55);
scope.document.querySelectorAll = s => s === 'iframe,frame' ? [{contentDocument:shadow}] : [];
assert.equal(vm.runInContext(scope.seek(65), scope), true);
assert.equal(nested.currentTime, 65);
scope.location = {href:'https://music.test/track/1'};
scope.navigator = {};
assert.equal(vm.runInContext(scope.observation('listen'), scope).kind, 'audio');
scope.document.querySelectorAll = s => s === 'iframe,frame' ? [{get contentDocument(){throw new Error('cross origin');}}] : [];
assert.equal(vm.runInContext(scope.seek(65), scope), false);
scope.location = {href:'https://reader.test/chapter/1'};
scope.navigator = {};
scope.window = {addEventListener:() => {}};
scope.document = {title:'Chapter one', visibilityState:'visible', querySelector:() => null,
    querySelectorAll:() => [], scrollingElement:{scrollTop:500,scrollHeight:2000,clientHeight:1000},
    addEventListener:() => {}};
let reading = vm.runInContext(scope.observation('read'), scope);
assert.equal(reading.kind, 'book');
assert.equal(reading.locator.fraction, 0.5);
assert.equal(vm.runInContext(scope.restoreReading({type:'scroll',selector:'',fraction:0.7}), scope), true);
assert.equal(scope.document.scrollingElement.scrollTop, 700);
scope.location.href = 'https://reader.test/catalog';
assert.notEqual(vm.runInContext(scope.observation('read'), scope).kind, 'book');
console.log('Feria tracking URLs, nested players, reading observation and restore checks pass');

// Model the live reader's nested surface and delayed image geometry, rather
// than the document scroll range (which is zero on MANGA Plus).
scope.location.href = 'https://mangaplus.shueisha.co.jp/viewer/1000486';
const image = {complete:true, naturalWidth:800};
let pageHeight = 1200;
const surface = {scrollTop:4400, scrollHeight:11100, clientHeight:700,
    getBoundingClientRect:() => ({top:0}),
    querySelectorAll:s => s === '.zao-container' ? pages : []};
const pages = Array.from({length:9}, (_, index) => ({
    getBoundingClientRect:() => ({top:30+index*(pageHeight+30)-surface.scrollTop, height:pageHeight}),
    querySelectorAll:s => s === 'img' ? [image] : []
}));
scope.document = {title:'[#001] One Piece | MANGA Plus', visibilityState:'visible',
    body:{innerText:'3.7k\n\n4 / 58\n\nOne Piece'},
    scrollingElement:{scrollTop:0,scrollHeight:900,clientHeight:900},
    querySelector:s => s === '.zao-surface' ? surface : null,
    querySelectorAll:() => []};
reading = vm.runInContext(scope.observation('read'), scope);
assert.equal(reading.locator.type, 'mangaplus-page', 'Capture the nested MANGA Plus reader');
assert.equal(reading.locator.page, 3);
assert.equal(reading.locator.total, 58);
assert.ok(Math.abs(reading.locator.offset-(4400-3720)/1200) < 0.0001);
assert.equal(reading.paused, true, 'Observation and programmatic restore do not invent user input');
const place = JSON.parse(JSON.stringify(reading.locator));
surface.scrollHeight = 18000;
assert.equal(vm.runInContext(scope.observation('read'), scope).locator.offset, place.offset,
    'Appending lazy pages must not change the captured place');
surface.scrollTop = 0;
image.complete = false;
assert.equal(vm.runInContext(scope.restoreReading(place), scope), false, 'Wait for page images before claiming restore');
image.complete = true;
pageHeight = 800; // Different window/image size on reopening.
assert.equal(vm.runInContext(scope.restoreReading(place), scope), false, 'First placement must be checked on a later probe');
assert.ok(Math.abs(surface.scrollTop-(30+3*830+place.offset*800)) < 0.001);
assert.equal(vm.runInContext(scope.restoreReading(place), scope), true, 'Confirm a stable page position');
pageHeight = 1000;
assert.equal(vm.runInContext(scope.restoreReading(place), scope), false, 'Reposition when late images change geometry');
assert.equal(vm.runInContext(scope.restoreReading(place), scope), true);
const distantPlace = {...place,page:20};
surface.scrollTop = 0;
assert.equal(vm.runInContext(scope.restoreReading(distantPlace), scope), false,
    'A not-yet-loaded page requires another lazy batch');
assert.equal(surface.scrollTop, surface.scrollHeight-surface.clientHeight);
assert.equal(vm.runInContext(scope.restoreReading({...place,total:57}), scope), false,
    'A changed chapter length must not restore an unrelated page');
for (const invalid of [{...place,page:-1},{...place,page:3.5},{...place,offset:NaN},{...place,offset:2}]) {
    assert.equal(vm.runInContext(scope.restoreReading(invalid), scope), false);
}
surface.scrollHeight = surface.clientHeight;
assert.equal(vm.runInContext(scope.restoreReading(place), scope), false, 'Do not claim horizontal reader support');
assert.equal(vm.runInContext(scope.observation('read'), scope).locator.type, 'url');
scope.location.href = 'https://reader.test/chapter/1';
assert.equal(vm.runInContext(scope.restoreReading(place), scope), false, 'Never apply a provider page locator to another site');
console.log('MANGA Plus page anchors, delayed images, resized restore, and origin guards pass');
