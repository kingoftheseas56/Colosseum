// Stream native parity expectations from the accepted I01 TypeScript oracle.
// Usage: node tests/static_catalogue_cases.mjs PUBLICATION I01_REPORT OUTPUT.jsonl [--live]
import assert from 'node:assert/strict';
import {readFileSync, openSync, writeSync, closeSync, writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {prebuiltCatalogue} from '../server/content-api/src/catalogue-prebuilt.ts';

const [root, priorPath, output, liveFlag] = process.argv.slice(2);
assert(root && priorPath && output?.endsWith('.jsonl'));
const load = path => JSON.parse(readFileSync(join(root, path)));
const entry = load('catalogue/current.json'), index = load(entry.index.path);
const assets = {async fetch(request) {
    try { return new Response(readFileSync(join(root, new URL(request.url).pathname))); }
    catch (error) { if (error.code === 'ENOENT') return new Response('', {status:404}); throw error; }
}};
const prior = JSON.parse(readFileSync(priorPath));
assert.equal(prior.parity.length, 83);
const fd = openSync(output, 'w'), groups = {}, seen = new Set();
let count = 0;
async function emit(group, dataset, method, args, label = '', duplicate = false) {
    const key = JSON.stringify([dataset, method, args]);
    if (!duplicate && seen.has(key)) return;
    seen.add(key);
    const result = dataset === 'content' && method === 'bundle'
        ? {result:load(index.content.bundle.path).result}
        : dataset === 'comics' && method === 'search' ? null
        : await prebuiltCatalogue(assets, entry.sourceRevision, dataset, method, args);
    const spec = {group, dataset, method, args, label:label || key};
    if (result === null) spec.notPrepared = true; else spec.expected = result.result;
    // genreEntries has no offset: an unbounded -1 prefix can exceed the existing
    // transport's result budget. Keep this as a separate resource-boundary test,
    // never as a successful empty result or a claimed deep-equality pass.
    if (result && Buffer.byteLength(JSON.stringify(result.result)) > 4*1024*1024)
        spec.expectedFailure = 'STATIC_RESULT_TOO_LARGE';
    writeSync(fd, JSON.stringify(spec) + '\n');
    groups[group] = (groups[group] || 0) + 1;
    if (++count % 1000 === 0) console.log('GENERATED', count);
}
try {
    for (const c of prior.parity) await emit('i01-83', c.dataset, c.method, c.args, '', true);
    const lists = {...index.lists};
    for (const path of Object.values(index.listBuckets || {})) Object.assign(lists, load(path));
    for (const [key, list] of Object.entries(lists)) {
        const [dataset, method, selected] = JSON.parse(key);
        const offsets = [0, Math.floor(list.length / 48) * 24,
            Math.max(0, Math.floor((list.length - 1) / 24) * 24)];
        for (let page = 0; page < 3; ++page) {
            const args = method === 'genreEntries'
                ? [...selected, page === 0 ? 24 : page === 1 ? Math.max(24, Math.ceil(list.length / 2)) : -1]
                : [...selected, offsets[page], 24];
            await emit('all-list-pages', dataset, method, args, key + '/' + ['first','middle','last'][page], true);
        }
    }
    for (const key of Object.keys(index.fixed)) {
        const [dataset, method, args] = JSON.parse(key); await emit('fixed', dataset, method, args);
    }
    const tankobanLookupKeys = new Set();
    for (const path of Object.values(index.lookups || {})) {
        for (const key of Object.keys(load(path))) {
            const [dataset, method, args] = JSON.parse(key); await emit('all-lookups', dataset, method, args);
            if (dataset === 'tankoban' && method === 'series') tankobanLookupKeys.add(key);
        }
    }
    // The additive fine ranges must exactly shadow the coarse tankoban keys;
    // emit the first/middle/last key of every range as boundary lookups.
    const fineKeys = [];
    let finePreviousLast = null;
    for (const ref of index.lookupRanges?.ranges ?? []) {
        if (finePreviousLast !== null) assert.ok(finePreviousLast < ref.first, 'lookupRanges stay sorted and disjoint');
        finePreviousLast = ref.last;
        const keys = Object.keys(load(ref.path));
        assert.ok(keys.length, 'range file carries keys');
        for (const key of keys) assert.ok(key >= ref.first && key <= ref.last, 'key inside its declared range');
        for (const n of [0, Math.floor(keys.length / 2), keys.length - 1]) {
            const [dataset, method, args] = JSON.parse(keys[n]); await emit('tankoban-ranges', dataset, method, args, '', true);
        }
        fineKeys.push(...keys);
    }
    if (index.lookupRanges) assert.deepEqual([...new Set(fineKeys)].sort(), [...tankobanLookupKeys].sort(), 'fine ranges cover exactly the coarse tankoban keys');
    for (const [family, directory] of Object.entries(index.search)) {
        const dataset = ['anime','manga'].includes(family) ? 'mal' : family;
        for (const ref of directory.names) {
            const names = load(ref.path);
            for (const n of [0, Math.floor(names.length/2), names.length-1]) {
                const name = names[n]?.[0]; if (!name) continue;
                for (const width of [1,3,8]) {
                    const prefix = Array.from(name).slice(0,width).join('');
                    await emit('search-prefixes', dataset, family === 'comics' ? 'exactMatches' : 'search',
                        family === 'comics' ? [name] : dataset === 'mal' ? [prefix,20,family] : [prefix,20]);
                }
            }
        }
        for (const ref of directory.keys) {
            const keys = load(ref.path);
            for (const n of [0, Math.floor(keys.length/2), keys.length-1]) {
                const id = keys[n]?.[0]; if (id == null) continue;
                if (family === 'manga') await emit('id-lookups', 'mal', 'mangaById', [id]);
                if (family === 'imdb') {
                    await emit('id-lookups', 'imdb', 'titlePresentation', [id]);
                    await emit('id-lookups', 'imdb', 'titleFacts', [[id]]);
                }
            }
        }
    }
    // Rare combined filters must remain genuinely unprepared, not successful empty pages.
    for (const dataset of ['mal','comics']) await emit('unprepared', dataset, 'discoverPageFiltered',
        ['popular',[{axis:'genre',key:'action'},{axis:dataset==='mal'?'demographic':'publisher',key:dataset==='mal'?'shounen':'marvel'}],false,0,24]);
    await emit('content', 'content', 'bundle', []);
    if (liveFlag === '--live') {
        const origin = 'https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev';
        const guest = await fetch(origin+'/v1/guest/session', {method:'POST', signal:AbortSignal.timeout(15000),
            headers:{'Content-Type':'application/json'}, body:JSON.stringify({appId:'colosseum',apiVersion:1})});
        assert.equal(guest.status,200); const {token} = await guest.json();
        const samples = [...[13,1,2,1706,44347].map(id=>['mal','mangaById',[id]]),
            ...['tt0103359','tt0111161','tt0068646','tt0468569','tt0137523'].map(id=>['imdb','titlePresentation',[id]]),
            ...['one piece','naruto','bleach','death note','berserk'].map(t=>['mal','search',[t,20]]),
            ['comics','curatedReady',[]],['mal','genreCounts',['anime']],['mal','genreCounts',['manga']],
            ['mal','discoverFilters',['genre',false]],['mal','discoverFilters',['demographic',false]]];
        const receipts = []; let rowsRead = 0;
        for (const [dataset,method,args] of samples) {
            const response = await fetch(`${origin}/v1/catalogue/${dataset}/${method}`, {method:'POST',
                signal:AbortSignal.timeout(15000), headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},
                body:JSON.stringify({args})});
            assert.equal(response.status,200,`${dataset}/${method}: HTTP ${response.status}`);
            const body = await response.json(); assert.equal(body.revision,entry.sourceRevision);
            const expected = await prebuiltCatalogue(assets,entry.sourceRevision,dataset,method,args);
            assert.deepEqual(body.result,expected.result);
            const cost = response.headers.get('X-Catalogue-D1-Rows');
            if (cost !== null) rowsRead += Number(cost);
            assert(rowsRead < 100000,'Stop live comparisons before meaningful free-quota consumption');
            receipts.push({dataset,method,args,result:body.result,pass:true,rowsHeader:cost});
            await emit('live-20',dataset,method,args,'live/'+dataset+'/'+method,true);
        }
        writeFileSync(output+'.live.json', JSON.stringify({comparisons:receipts.length,guestRequests:1,
            queryRequests:20,reportedRowsRead:rowsRead,allRowsHeadersPresent:receipts.every(x=>x.rowsHeader!==null),
            d1Writes:0,deployed:false,receipts},null,2));
    }
    writeFileSync(output+'.manifest.json',JSON.stringify({cases:count,groups,publication:entry.revision,
        sourceRevision:entry.sourceRevision,allListSelections:Object.keys(lists).length,
        comicsContains:'intentional Worker fallback',deployed:false},null,2));
    console.log(JSON.stringify({cases:count,groups}));
} finally { closeSync(fd); }
