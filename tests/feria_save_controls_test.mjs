import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Execute the actual QML press/click handlers against an input-boundary spy.
// This checks the token supplied by presentation, not native Save behavior.
let source = fs.readFileSync(new URL('../qml/feria/FeriaSaveContextBar.qml', import.meta.url),'utf8');
if (process.env.FERIA_CONTEXT_CLICK_NEGATIVE === '1')
    source = source.replace(/(resourceAction|openAction|copyAction)\.contextIdAtPress/g,'root.snapshot.contextId');
if (process.env.FERIA_CONTEXT_CLICK_NEGATIVE === 'owner')
    source = source.replace(/(resourceAction|openAction|copyAction)\.toolsAtPress\.(saveFromContext|contextNativeAction)/g,'root.tools.$2');
if (process.env.FERIA_CONTEXT_CLICK_NEGATIVE === 'kind')
    source = source.replace(/(resourceAction|openAction|copyAction)\.actionAtPress/g,'contextAction');
const pressed = source.match(/^        onPressed: (.+)$/m)?.[1];
assert.ok(pressed, 'context buttons must capture their token when pressed');
for (const [objectName, id] of [['feriaContextSaveResource','resourceAction'], ['feriaContextOpenLink','openAction'], ['feriaContextCopy','copyAction']]) {
    const start = source.indexOf(`objectName: "${objectName}"`);
    const end = source.indexOf('\n            }',start);
    const block = source.slice(start,end);
    const clicked = block.match(/onClicked: ([\s\S]*)$/)?.[1];
    assert.ok(clicked, `${objectName} click handler missing`);
    const calls = [];
    const state = {kind:'link',contextAction:'link',actionAtPress:'',toolsAtPress:null,contextIdAtPress:'',ContextMenuRequest:{MediaTypeImage:1,MediaTypeCanvas:4},
        root:{snapshot:{contextId:'context-a',linkUrl:'https://fixture.test/a',mediaType:0},
            tools:{saveFromContext:(contextId,kind)=>calls.push({contextId,kind,owner:'original'}),
                   contextNativeAction:(contextId,kind)=>calls.push({contextId,kind,owner:'original'})}}};
    state[id] = {};
    vm.createContext(state);
    vm.runInContext(pressed,state);
    state[id].contextIdAtPress = state.contextIdAtPress;
    state[id].toolsAtPress = state.toolsAtPress;
    state[id].actionAtPress = state.actionAtPress;
    state.root.snapshot = {contextId:'context-b',linkUrl:'https://fixture.test/b',mediaType:0};
    state.contextAction = 'replacement-kind';
    vm.runInContext(clicked,state);
    assert.equal(calls.length,1);
    assert.equal(calls[0].contextId,'context-a',`${objectName} must dispatch the pressed context, never the replacement`);
    assert.equal(calls[0].kind,'link',`${objectName} must retain the pressed semantic action kind`);
    calls.length = 0;
    // Context IDs are local to a view. A replacement controller can legitimately
    // have the same ID; the presentation must retain the original owner too.
    state.root.snapshot = {contextId:'context-a',linkUrl:'https://fixture.test/b',mediaType:0};
    state.root.tools = {saveFromContext:(contextId,kind)=>calls.push({contextId,kind,owner:'replacement'}),
        contextNativeAction:(contextId,kind)=>calls.push({contextId,kind,owner:'replacement'})};
    vm.runInContext(clicked,state);
    assert.equal(calls[0].owner,'original',`${objectName} must retain the original context owner across equal-ID view replacement`);
}
console.log('Feria context-button press token boundary passed.');
