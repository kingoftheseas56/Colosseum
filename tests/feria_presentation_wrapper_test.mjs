import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const qml = fs.readFileSync(process.env.FERIA_WRAPPER_SOURCE || new URL('../qml/feria/FeriaQtWebEngine.qml', import.meta.url), 'utf8');
function productionFunction(name) {
    const start = qml.indexOf(`    function ${name}(`);
    assert.ok(start >= 0, name);
    return qml.slice(start, qml.indexOf('\n    }', start) + 6);
}
function fixture() {
    const state = {navigationGeneration: 7, presentationSerial: 4, closingSession: false,
        rendererLost:false, replacementLoadPending:false, documentReady:true, activitySuspended:true,
        presentationResult:{operationId:'7:3',outcome:'succeeded'}, presentationPollAttempts:0,
        presentationPollPending:false, activityResult:()=>{}, wakePage:()=>{},
        Playback:{presentationMedia:()=>'',presentationMediaResult:()=>''}};
    state.browser = state;
    const context = vm.createContext(state);
    vm.runInContext(['acceptPresentationResult','applyPresentationActivity','invalidatePresentationActivity']
        .map(productionFunction).join('\n'), context);
    return state;
}
let failures=0;
function test(name, body) {try {body();console.log('PASS',name)}catch(error){++failures;console.error('FAIL',name,error.message)}}
test('dispatch retires the prior success until the new script is observed', () => {
    const f=fixture();f.evaluateJavaScript=()=>true;f.applyPresentationActivity();
    assert.equal(f.presentationResult.operationId,'7:4');assert.equal(f.presentationResult.outcome,'pending');
});
test('missing script result is a failed observation', () => {
    const f=fixture();f.acceptPresentationResult(7,4,undefined);
    assert.equal(f.presentationResult.outcome,'failed');
});
test('denied script dispatch does not retain a previous success', () => {
    const f=fixture();f.evaluateJavaScript=()=>false;f.applyPresentationActivity();
    assert.equal(f.presentationResult.outcome,'unsupported');
});
test('old operation callback cannot replace a newer observed result', () => {
    const f=fixture();f.acceptPresentationResult(7,3,{operationId:'7:3',outcome:'blocked'});
    assert.equal(f.presentationResult.outcome,'succeeded');
});
test('renderer loss rejects otherwise matching activity acknowledgement', () => {
    const f=fixture();f.rendererLost=true;f.invalidatePresentationActivity('renderer-lost');
    f.acceptPresentationResult(7,4,{operationId:'7:4',outcome:'succeeded'});
    assert.equal(f.presentationResult.outcome,'invalidated');
});
const deck = fs.readFileSync(new URL('../qml/feria/FeriaSessionDeck.qml', import.meta.url), 'utf8');
const activityBinding = deck.match(/activitySuspended: ([^\n]+\n\s*\|\|[^\n]+\n\s*\|\|[^\n]+)/)[1];
function checkPolicy(binding) {
    for (const displayed of [false,true]) for (const mainMinimized of [false,true])
        for (const auxiliaryMinimized of [false,true]) for (const inactive of [false,true]) {
            const context = vm.createContext({Window:{Minimized:2},
                deck:{displayed,Window:{window:{visibility:mainMinimized?2:1}}},
                entry:{visible:!inactive,hostWindow:{visibility:auxiliaryMinimized?2:1}}});
            assert.equal(vm.runInContext(binding,context), !displayed || mainMinimized || auxiliaryMinimized);
        }
}
test('actual deck policy retains overlapping reasons and inactive-space playback', () => checkPolicy(activityBinding));
test('policy controls reject world/main/auxiliary reason loss and inactive-tab suspension', () => {
    assert.throws(() => checkPolicy(activityBinding.replace('!deck.displayed', 'false')));
    assert.throws(() => checkPolicy(activityBinding.replace('deck.Window.window.visibility === Window.Minimized', 'false')));
    assert.throws(() => checkPolicy(activityBinding.replace('entry.hostWindow.visibility === Window.Minimized', 'false')));
    assert.throws(() => checkPolicy('!entry.visible || ' + activityBinding));
});
if(failures)process.exitCode=1;
