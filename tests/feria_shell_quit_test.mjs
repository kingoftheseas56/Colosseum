// Execute the real shell quit functions; no copied production state machine.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import test from 'node:test';
const source = readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
function body(name) {
    const start = source.indexOf('function ' + name + '(');
    assert.notEqual(start, -1, 'Main.qml must define ' + name);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    for (; depth && end < source.length; ++end) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
    }
    assert.equal(depth, 0);
    return source.slice(start, end);
}
function fixture(available=true, loaded=true) {
    const deferred = [];
    const context = {
        applicationQuitPending:false, applicationQuitReady:false, applicationQuitError:'',
        feriaAvailable:available, calls:[], exits:0,
        console:{info(){},warn(){}},
        Qt:{callLater(fn){deferred.push(fn);}},
        feriaSessionDeck:{item:null}
    };
    context.Qt.quit=()=>context.exits++;
    context.win=context;
    if (loaded) context.feriaSessionDeck.item={startQuiescence(value){context.calls.push(value);}};
    for (const name of ['requestApplicationQuit','completeApplicationQuit','failApplicationQuit'])
        runInNewContext(body(name),context);
    context.flush=()=>{while(deferred.length)deferred.shift()();};
    return context;
}
test('desktop quit waits for deck quiescence, then defers final quit',()=>{
    const c=fixture(); c.requestApplicationQuit();
    assert.equal(c.applicationQuitPending,true);assert.equal(c.exits,0);
    assert.deepEqual(c.calls,[true]);c.flush();assert.equal(c.exits,0);
    c.completeApplicationQuit();assert.equal(c.applicationQuitReady,true);
    assert.equal(c.exits,0);c.flush();assert.equal(c.exits,1);
});
test('duplicate requests and duplicate completion do not double drain or quit',()=>{
    const c=fixture();c.requestApplicationQuit();c.requestApplicationQuit();
    assert.deepEqual(c.calls,[true]);c.completeApplicationQuit();c.completeApplicationQuit();
    c.flush();assert.equal(c.exits,1);
});
test('unsolicited completion cannot quit the application',()=>{
    const c=fixture();c.completeApplicationQuit();c.flush();assert.equal(c.exits,0);
});
test('a deck that finishes synchronously still defers final quit',()=>{
    const c=fixture();
    c.feriaSessionDeck.item.startQuiescence=()=>c.completeApplicationQuit();
    c.requestApplicationQuit();assert.equal(c.exits,0);
    c.flush();assert.equal(c.exits,1);
});
test('failure keeps shell open and permits a fresh retry',()=>{
    const c=fixture();c.requestApplicationQuit();c.failApplicationQuit('dependent page');
    c.flush();assert.equal(c.exits,0);assert.equal(c.applicationQuitPending,false);
    assert.equal(c.applicationQuitError,'dependent page');
    c.requestApplicationQuit();assert.equal(c.applicationQuitError,'');
    assert.deepEqual(c.calls,[true,true]);c.completeApplicationQuit();c.flush();assert.equal(c.exits,1);
});
test('unrelated cleanup failure does not change quit state',()=>{
    const c=fixture();c.failApplicationQuit('other cleanup');assert.equal(c.applicationQuitError,'');
});
test('Android without a loaded deck can quit without browser services',()=>{
    const c=fixture(false,false);c.requestApplicationQuit();c.flush();assert.equal(c.exits,1);
    assert.deepEqual(c.calls,[]);
});
test('unloaded desktop deck can quit without instantiating Feria',()=>{
    const c=fixture(true,false);c.requestApplicationQuit();c.flush();assert.equal(c.exits,1);
});
test('shell uses lazy deck signals and routes all final quit calls through the gate',()=>{
    assert.match(source,/target:\s*feriaSessionDeck\.item[\s\S]*?function onQuiesced\(\)\s*\{\s*win\.completeApplicationQuit\(\)/);
    assert.match(source,/function onQuiescenceFailed\(reason\)\s*\{\s*win\.failApplicationQuit\(reason\)/);
    assert.match(source,/onClosing:\s*function\(event\)[\s\S]*?event\.accepted = false[\s\S]*?win\.requestApplicationQuit\(\)/);
    assert.equal((source.match(/Qt\.quit\(\)/g)||[]).length,1);
    assert.match(source,/setSource\("feria\/FeriaSessionDeck.qml"/);
    assert.doesNotMatch(source,/import "feria"/);
});
test('lazy deck receives required services before Loader creates the component',()=>{
    const c={active:true,item:null,Sessions:{},FeriaAccount:{},FeriaBrowserPolicy:{},loads:[]};
    c.setSource=(url,props)=>c.loads.push({url,props});
    runInNewContext(body('loadBrowserDeck'),c);
    c.loadBrowserDeck();
    assert.equal(c.loads.length,1);
    assert.equal(c.loads[0].url,'feria/FeriaSessionDeck.qml');
    assert.equal(c.loads[0].props.sessions,c.Sessions);
    assert.equal(c.loads[0].props.accountStore,c.FeriaAccount);
    assert.equal(c.loads[0].props.policy,c.FeriaBrowserPolicy);
    c.loads=[];c.active=false;c.loadBrowserDeck();assert.equal(c.loads.length,0);
    c.active=true;c.item={};c.loadBrowserDeck();assert.equal(c.loads.length,0);
});
test('updater requests the same shell shutdown gate',()=>{
    const native=readFileSync(new URL('../native/main.cpp',import.meta.url),'utf8');
    const hook=native.slice(native.indexOf('updateHooks.requestShutdown ='),native.indexOf('updateHooks.fetchArtwork ='));
    assert.match(hook,/Colosseum::Bootstrap::requestApplicationShutdown\(app, engine\)/);
    assert.doesNotMatch(hook,/QCoreApplication::quit|app\.quit\(/);
});

test('lazy deck open facade refuses unloaded calls and forwards to its owner',()=>{
    const c={item:null};
    runInNewContext(body('open'),c);
    assert.equal(c.open({url:'https://example.com'},true,false),'');
    const descriptor={url:'https://example.com'};let called;
    c.item={open(...args){called=args;return 'owned-browser';}};
    assert.equal(c.open(descriptor,true,false),'owned-browser');
    assert.deepEqual(called,[descriptor,true,false]);
});
