import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const policy = new Function(readFileSync(new URL('../qml/ShellBackPolicy.js',import.meta.url),'utf8')
    .replace(/^\.pragma library\s*$/m,'') + '\nreturn {actionFor};')();
assert.equal(policy.actionFor({androidHost:true,librarySelectionActive:true}), 'librarySelection');
assert.equal(policy.actionFor({androidHost:true,keyboardVisible:true,librarySelectionActive:true}), 'dismissKeyboard');
assert.equal(policy.actionFor({androidHost:true,accountFlyoutVisible:true,librarySelectionActive:true}), 'accountFlyout');
assert.equal(policy.actionFor({androidHost:true,playerOpen:true,librarySelectionActive:true}), 'player');
const main=readFileSync(new URL('../qml/Main.qml',import.meta.url),'utf8').replace(/\r\n/g,'\n');
const body=/function handleEscape\(\) \{([\s\S]*?)\n    \}\n/.exec(main)[1];
const effects=[];
const win={shellEscapeState:()=>({androidHost:true,librarySelectionActive:true}),
    phoneLibrarySelectionOwner:{handleBack:()=>effects.push('selectionBack')},
    cancelCatalogueRoute:()=>effects.push('cancelRoute')};
new Function('win','ShellBackPolicy',body)(win,policy);
assert.deepEqual(effects,['selectionBack']);
console.log('LIBRARY_SELECTION_BACK_PASS IME modal session and selection precedence; no hidden navigation');
