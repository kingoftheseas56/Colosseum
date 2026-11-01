// Exact native-key delivery contract. Optional external frozen originals support RED controls.
import fs from 'node:fs';
import {join} from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root = new URL('../', import.meta.url);
const original = process.env.DISCOVER_ORIGINAL_SOURCE_DIR;
function load(name) {
    const path = name === 'TankobanDiscoverApi.js' && original
        ? join(original, name)
        : new URL('qml/' + name, root);
    return fs.readFileSync(path, 'utf8').replace(/^\.(pragma|import).*$/gm, '');
}
function makeAdapter(type) {
    const delivery = vm.createContext({console});
    vm.runInContext(load('CatalogDelivery.js'), delivery);
    const policy = vm.createContext({console});
    vm.runInContext(load('ExplicitContentPolicy.js'), policy);
    const context = vm.createContext({console, CatalogDelivery: delivery, Policy: policy});
    vm.runInContext(load('TankobanDiscoverApi.js'), context);
    const catalog = newCatalog();
    const adapter = type === 'comics'
        ? context.create(null, catalog, [], false, null)
        : context.create(catalog, null, [], false, null);
    return {catalog, adapter};
}
function signal() {
    const callbacks = new Set();
    return {
        connect(fn) { callbacks.add(fn); }, disconnect(fn) { callbacks.delete(fn); },
        emit(...args) { for (const fn of [...callbacks]) fn(...args); },
        get size() { return callbacks.size; }
    };
}
function nativeArguments(catalog='popular', filters=[], offset=0) {
    return [catalog, filters, false, offset, 24];
}
function newCatalog() {
    const entries = new Map(), calls = [];
    const address = a => JSON.stringify(a);
    function get(a) { const e = entries.get(address(a)); if (!e) throw Error('Unregistered query: ' + address(a)); return e; }
    const catalog = {
        remoteEnabled: true, calls,
        remoteQueryChanged: signal(), revisionChanged: signal(), readyChanged: signal(), requestFailed: signal(),
        given(a, key, page=null, stale=false) {
            const e = {key, page, pending:false, error:'', stale, failed:false};
            entries.set(address(a), e); return e;
        },
        discoverPageFiltered(...a) {
            calls.push(a);
            const e=get(a);
            if (!e.page || e.stale) { if (!e.failed) e.pending=true; }
            return e.page || {items:[],nextOffset:a[3],exhausted:true};
        },
        discoverPageFilteredState(...a) {
            const e=get(a);
            return {source:'remote',key:e.key,enabled:true, cached:!!e.page,
                status:e.pending?'pending':e.failed?'failure':e.page?'success':'missing',
                error:e.error,retryable:e.failed&&!e.pending};
        },
        retryDiscoverPageFiltered(...a) {
            const e=get(a); if (!e.failed) return false;
            e.failed=false; e.pending=true; return true;
        },
        succeed(a, page) {
            const e=get(a); e.page=page; e.stale=false; e.pending=false; e.failed=false;
            catalog.remoteQueryChanged.emit(e.key); catalog.revisionChanged.emit();
        },
        fail(a, error) {
            const e=get(a); e.pending=false; e.failed=true; e.error=error;
            catalog.remoteQueryChanged.emit(e.key); catalog.requestFailed.emit('discoverPageFiltered',error);
        },
    };
    return catalog;
}
const mangaRows = n => Array.from({length:n}, (_,i) => ({mal_id: 101+i,title:'manga '+i,explicit:false}));
const comicsRows = n => Array.from({length:n}, (_,i) => ({locgId:'locg:'+i,title:'comic '+i,explicit:false}));
const page = (rows, offset=0, exhausted=true) => ({items:rows,nextOffset:offset+rows.length,exhausted});
const state = (type, catalogKey='popular', filters={}) => ({type,catalogKey,filters,filterGroup:'',filterKey:''});
function subscribe(adapter, st, cursor, generation=1, trace=true) {
    const arrived=[], phases=[];
    if (trace) st._tracePhase=(...e) => phases.push(e);
    const cancel=adapter.fetchPage(st,cursor,generation,(gen,p)=>arrived.push({gen,page:p}));
    return {arrived,phases,cancel};
}

test('Manhwa keeps the opaque native query identity for its mandatory publication facet', () => {
    const {catalog,adapter}=makeAdapter('manhwa');
    const args=nativeArguments('popular',[{axis:'format',key:'manhwa'}]);
    const opaque='NATIVE-Manhwa-Q#A (not JS(args))';
    catalog.given(args,opaque);
    const result=subscribe(adapter,state('manhwa'),null);
    assert.equal(result.arrived.length,0);
    catalog.succeed(args,page([{mal_id:121496,title:'Solo Leveling',type:'Manhwa',explicit:false}]));
    assert.equal(result.arrived[0].page._traceQueryKey,opaque);
    assert.equal(result.arrived[0].page.items[0].type,'manga');
    assert.ok(result.phases.length>=2&&result.phases.every(phase=>phase[1]===opaque));
});

for (const type of ['manga','comics']) {
    const rows = type === 'manga' ? mangaRows : comicsRows;
    test(type+': success carries EXACT opaque native key through normalized page', () => {
        const {catalog,adapter}=makeAdapter(type), args=nativeArguments();
        const opaque='NATIVE-'+type+'-Q#A (not JS(args))';
        catalog.given(args,opaque);
        const s=subscribe(adapter,state(type),null,7);
        assert.equal(s.arrived.length,0);
        catalog.succeed(args,page(rows(3),0,false));
        assert.equal(s.arrived.length,1);
        assert.equal(s.arrived[0].gen,7);
        assert.equal(s.arrived[0].page._traceQueryKey,opaque);
        assert.deepEqual(s.phases.map(p=>p[0]),['qml_conversion_begin','qml_conversion_end']);
        assert.equal(s.phases[0][1],opaque);
        assert.equal(s.arrived[0].page.items.length,3);
    });
    test(type+': valid empty accepted, and first-page no-data failure has no successful key', () => {
        const {catalog,adapter}=makeAdapter(type), args=nativeArguments();
        catalog.given(args,'native-empty-'+type);
        const s=subscribe(adapter,state(type),null);
        catalog.succeed(args,page([],0,true));
        assert.equal(s.arrived[0].page._traceQueryKey,'native-empty-'+type);
        assert.equal(s.arrived[0].page.exhausted,true);
        const a2=nativeArguments('top-rated');
        catalog.given(a2,'native-failed-'+type);
        const f=subscribe(adapter,state(type,'top-rated'),null);
        catalog.fail(a2,'HTTP_503');
        assert.equal(f.arrived[0].page.failed,true);
        assert.equal('_traceQueryKey' in f.arrived[0].page,false);
        assert.equal(f.phases.length,0);
    });
    test(type+': cached failure paints offline rows with owning key; retry stays exact', () => {
        const {catalog,adapter}=makeAdapter(type),args=nativeArguments();
        catalog.given(args,'native-cached-'+type,page(rows(2)),true);
        const s=subscribe(adapter,state(type),null);
        assert.equal(s.arrived.length,0);
        catalog.fail(args,'HTTP_502');
        assert.equal(s.arrived[0].page._traceQueryKey,'native-cached-'+type);
        assert.equal(s.arrived[0].page.warning,'Showing offline catalogue');
        assert.equal(s.arrived[0].page.items.length,2);
        assert.equal(adapter.retryPage(state(type),null),true);
        const retry=subscribe(adapter,state(type),null);
        assert.equal(retry.arrived.length,0);
        catalog.succeed(args,page(rows(3)));
        assert.equal(retry.arrived[0].page._traceQueryKey,'native-cached-'+type);
        assert.equal(retry.arrived[0].page.warning,'');
    });
    test(type+': no trace gate leaves page metadata and normal semantics untouched', () => {
        const {catalog,adapter}=makeAdapter(type),args=nativeArguments();
        catalog.given(args,'opaque-off-'+type);
        const s=subscribe(adapter,state(type),null,1,false);
        catalog.succeed(args,page(rows(2)));
        assert.equal('_traceQueryKey' in s.arrived[0].page,false);
        assert.equal(s.arrived[0].page.items.length,2);
        assert.equal(s.phases.length,0);
    });
    test(type+': filter/catalogue supersession and cancellation cannot carry old key', () => {
        const {catalog,adapter}=makeAdapter(type),old=nativeArguments(),newArgs=nativeArguments('top-rated');
        catalog.given(old,'opaque-OLD-'+type);
        catalog.given(newArgs,'opaque-NEW-'+type);
        const first=subscribe(adapter,state(type),null,11);
        first.cancel();
        const second=subscribe(adapter,state(type,'top-rated'),null,12);
        catalog.succeed(old,page(rows(1)));
        assert.equal(first.arrived.length,0);
        assert.equal(second.arrived.length,0);
        catalog.succeed(newArgs,page(rows(2)));
        assert.equal(second.arrived[0].page._traceQueryKey,'opaque-NEW-'+type);
        assert.equal(first.phases.length,0);
        assert.equal(catalog.remoteQueryChanged.size,0);
    });
    test(type+': missing native key is explicit failure, never model success', () => {
        const {catalog,adapter}=makeAdapter(type),args=nativeArguments();
        catalog.given(args,'');
        const s=subscribe(adapter,state(type),null);
        assert.equal(s.arrived.length,1);
        assert.equal(s.arrived[0].page.failed,true);
        assert.equal('_traceQueryKey' in s.arrived[0].page,false);
    });
    test(type+': immediate cached success completes synchronously with exact key', () => {
        const {catalog,adapter}=makeAdapter(type),args=nativeArguments();
        catalog.given(args,'native-sync-'+type,page(rows(1)));
        const s=subscribe(adapter,state(type),null);
        assert.equal(s.arrived.length,1);
        assert.equal(s.arrived[0].page._traceQueryKey,'native-sync-'+type);
    });
    test(type+': reentrant synchronous completion closes each conversion under its own native key', () => {
        const {catalog,adapter}=makeAdapter(type),a=nativeArguments(),b=nativeArguments('new-releases');
        catalog.given(a,'REENTER-A-'+type,page(rows(1)));
        catalog.given(b,'REENTER-B-'+type,page(rows(2)));
        const observed=[];
        const s1=state(type),s2=state(type,'new-releases');
        s1._tracePhase=(event,key)=>observed.push('phase-1:'+event+':'+key);
        s2._tracePhase=(event,key)=>observed.push('phase-2:'+event+':'+key);
        adapter.fetchPage(s1,null,21,(gen,p) => {
            observed.push('done-1:'+p._traceQueryKey);
            adapter.fetchPage(s2,null,22,(g2,p2)=>{
                observed.push('done-2:'+p2._traceQueryKey);
            });
        });
        assert.deepEqual(observed,[
            'phase-1:qml_conversion_begin:REENTER-A-'+type,
            'phase-1:qml_conversion_end:REENTER-A-'+type,
            'done-1:REENTER-A-'+type,
            'phase-2:qml_conversion_begin:REENTER-B-'+type,
            'phase-2:qml_conversion_end:REENTER-B-'+type,
            'done-2:REENTER-B-'+type
        ]);
    });
}
