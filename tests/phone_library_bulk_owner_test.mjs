import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
const effects=[];
for (const [file,world] of [['TheatreWorld.qml','theatre'],['BiblioWorld.qml','biblio'],['TankobanWorld.qml','tankoban']]) {
    const source=readFileSync(new URL('../qml/'+file,import.meta.url),'utf8');
    const body=/removeSelectionEntry:\s*function\(entry\)\s*\{([\s\S]*?)\n\s*\}/.exec(source)?.[1];
    assert.ok(body, 'Missing actual owner callback: '+file);
    const run=new Function('entry','StremioActions','Collection',body);
    const actions={removeTheatreItem:(...args)=>{effects.push([world,...args]);return true}};
    const collection={remove:(...args)=>{effects.push(args);return true}};
    assert.equal(run({id:'fixture-'+world,type:'series'},actions,collection),true);
}
assert.deepEqual(effects,[['theatre','fixture-theatre','series',false],['biblio','fixture-biblio'],['tankoban','fixture-tankoban']]);
console.log('PHONE_BULK_OWNER_PASS canonical local-only Theatre action and Collection membership owners');
