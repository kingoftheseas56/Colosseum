import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
const source = readFileSync(process.argv[2] || new URL('../qml/Main.qml', import.meta.url), 'utf8');
const names = ['detailContinue', 'resumeContinue', 'openVaultComic', 'openBookSession'];
const functions = names.map(name => {
    const match = source.match(new RegExp('    function ' + name + '\\([^]*?\\n    }'));
    assert.ok(match, name);
    return match[0];
}).join('\n');
const calls = [];
const context = {
    Sessions: {openOrSwitch: target => calls.push(JSON.parse(JSON.stringify(target)))},
    VideoPresentation: {forEntry: () => ({})},
    setGuiStallContext() {}, reopenPinnedUniverseComic: () => false,
    openBook: book => calls.push({detailBook: book}),
    openSeries: (title, id) => calls.push({series: id}),
    canonicalMalId: id => id,
    console: {log() {}}
};
context.win = context;
runInNewContext(functions, context);
function details(entry) {
    calls.length = 0;
    const before = JSON.stringify(entry);
    context.detailContinue(entry);
    assert.equal(JSON.stringify(entry), before, 'Routing must not rewrite saved progress');
    return calls;
}
let count = 0;
for (const prefix of ['vault:', 'vault-file:']) {
    for (const kind of ['comic', 'manga']) {
        const id = prefix + 'local-archive';
        const result = details({kind, id, title:'Local comic', resume:{chapterId:'/tmp/local.cbz',page:2}});
        assert.deepEqual(result, [{appType:'tankoban',contentKind:'comic',title:'Local comic',
            target:{vaultPath:'/tmp/local.cbz',id,title:'Local comic'}}],
            'Local archive Details must reopen its existing reader, not silently ignore or resolve a provider');
        count++;
    }
    const book = {id:prefix+'local-book', title:'Local book'};
    assert.deepEqual(details({kind:'book',id:book.id,title:book.title,resume:{path:'/tmp/local.epub',book}}),
        [{appType:'biblio',contentKind:'book',title:'Local book',target:{path:'/tmp/local.epub',book,id:'/tmp/local.epub'}}],
        'Loose local book Details must preserve the known path through its existing reader');
    count++;
}
const catalogueBook = {id:'123456',title:'Catalogue book'};
assert.deepEqual(details({kind:'book',id:'123456',resume:{path:'/tmp/download.epub',book:catalogueBook}}),
    [{detailBook:catalogueBook}], 'Catalogue-associated downloaded books retain their detail route'); count++;
assert.deepEqual(details({kind:'manga',id:'13',title:'One Piece',resume:{chapterId:'/tmp/provider-chapter'}}),
    [{series:'13'}], 'A provider item is not made local by a chapter path'); count++;
assert.deepEqual(details({kind:'comic',id:'retired:123',resume:{chapterId:'/tmp/unknown'}}),[],
    'Unknown retired comic IDs must not become a local or provider route'); count++;
assert.deepEqual(details({kind:'comic',id:'vault:missing',resume:{}}),[], 'Missing local path must not open a provider'); count++;
assert.deepEqual(details(null),[]); count++;
console.log(`PASS ${count} actual Continue Details routing cases`);
