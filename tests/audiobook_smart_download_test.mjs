import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const script = 'qml/AudiobookSmartDownload.js';
const resolver = {};
if (fs.existsSync(script)) {
  const biblio = {};
  const load = (file) => fs.readFileSync(file, 'utf8').replace(/^\.(pragma|import)\b.*$/gm, '');
  vm.runInNewContext(load('qml/BiblioApi.js'), biblio);
  vm.runInNewContext(load(script), Object.assign(resolver, { Biblio: biblio, qsTr: text => text }));
}

const context = {
  title: 'The Long Road', author: 'Ada Lee', pairKey: 'work-pair',
  workId: 'road', targetRecordingId: 'us',
  series: [{ name: 'The Long Roads', numberText: '4' }],
  recordings: [
    { id: 'us', workId: 'road', language: 'en', abridgement: 0, asin: 'B000000001', narrators: [{ name: 'Ben Rose' }, { name: 'Cleo Vale' }] },
    { id: 'uk', workId: 'road', language: 'eng', abridgement: 0, isbn: '9781234567890', narrators: [{ name: 'Ed Cole' }] },
    { id: 'elsewhere', workId: 'other', narrators: ['Unrelated'] }
  ]
};
const listing = (slug, values = {}) => ({ title: context.title, author: context.author, slug, ...values });
const info = (character, values = {}) => ({
  infoHash: character.repeat(40), narrator: ['Ben Rose & Cleo Vale'], language: 'English',
  abridgement: 'Unabridged', format: 'm4b', fileCount: 1, ...values
});
const probable = { verdict: 'probable', recordingKey: 'work-pair#as:us' };
const exact = { verdict: 'exact', recordingKey: 'work-pair#as:us' };

function resolve(abb, match, done) {
  assert.equal(typeof resolver.resolve, 'function', 'smart lookup should expose resolve()');
  return resolver.resolve(abb, { matchSource: match }, context, done);
}

function synchronous(rows, facts, result = () => probable) {
  let outcome;
  resolve({
    search(term, page, done) {
      assert.equal(term, 'the long road ada lee');
      assert.equal(page, 1);
      done({ rows, hasMore: false });
    },
    detail(slug, done) {
      if (facts[slug]) done(facts[slug]);
      else done(null, 'timeout');
    }
  }, result, value => { outcome = value; });
  return outcome;
}

test('the sole target recording is selected and native matcher receives normalized evidence for all same-work editions', () => {
  const outcome = synchronous([listing('first')], { first: info('a', { asin: 'B000000001' }) }, (source, editions) => {
    assert.equal(editions.map(row => row.recordingKey).join(','), 'work-pair#as:us,work-pair#as:uk');
    assert.equal(editions[1].isbn, '9781234567890');
    assert.equal(editions[0].language, source.language);
    assert.equal(editions[1].language, source.language);
    assert.equal(editions[0].abridgement, source.abridgement);
    assert.equal(source.narrators.join(','), 'Ben Rose,Cleo Vale');
    assert.equal(source.asin, 'B000000001');
    return exact;
  });
  assert.equal(outcome.automatic, outcome.rows[0]);
  assert.equal(outcome.automatic.request.audiosiloRecordingId, 'us');
  assert.equal(outcome.automatic.request.workId, 'road');
  assert.equal(outcome.automatic.request.recordingKey, 'work-pair#as:us');
  assert.equal(outcome.automatic.request.sourceRef, 'first');
});

test('multiple matches recommend exact, then one file, then highest seeder count', () => {
  const rows = [listing('a', { seeders: 10 }), listing('b', { seeders: 200 }),
    listing('c', { seeders: 500 }), listing('d', { seeders: 300 })];
  const outcome = synchronous(rows, { a: info('a'), b: info('b', { fileCount: 2 }), c: info('c'), d: info('d') },
    source => source.infoHash.startsWith('c') ? probable : exact);
  assert.equal(outcome.automatic, null);
  assert.equal(outcome.rows.map(row => row.request.sourceRef).join(','), 'd,a,b,c');
  assert.equal(outcome.rows.filter(row => row.recommended).map(row => row.request.sourceRef).join(','), 'd');
});

test('alternate edition, uncertain upload, and counted bundle require picker selection', () => {
  const outcome = synchronous([listing('other'), listing('uncertain'), listing('all', { title: 'The Long Road Books 1–3', bundle: true })], {
    other: info('a', { narrator: ['Ed Cole'] }),
    uncertain: info('b', { narrator: ['Full Cast'] }),
    all: info('c')
  }, source => source.infoHash.startsWith('a') ? { verdict: 'exact', recordingKey: 'work-pair#as:uk' }
    : { verdict: 'none', reason: 'no_match' });
  assert.equal(outcome.automatic, null);
  const [other, uncertain, bundle] = ['other', 'uncertain', 'all'].map(slug =>
    outcome.rows.find(row => row.request.sourceRef === slug));
  assert.equal(other.reason, 'Different narration');
  assert.equal(other.request.audiosiloRecordingId, 'uk');
  assert.equal(uncertain.reason, 'Unconfirmed');
  assert.equal(uncertain.request.recordingKey, 'work-pair#src:' + 'b'.repeat(40));
  assert.equal(bundle.bundleCount, 3);
  assert.equal(bundle.request.recordingKey, 'bundle#src:' + 'c'.repeat(40));
  assert.equal(bundle.request.pairKey, undefined);
  assert.equal(bundle.request.workId, undefined);
  assert.equal(bundle.request.audiosiloRecordingId, undefined);
});

test('unreliable work evidence leaves a playable source independent of catalogue work', () => {
  const outcome = synchronous([listing('other', { title: 'The Long Road to Mars' })],
    { other: info('a') }, () => ({ verdict: 'none', reason: 'no_match' }));
  assert.equal(outcome.automatic, null);
  assert.equal(outcome.rows[0].request.workId, undefined);
  assert.notEqual(outcome.rows[0].request.pairKey, context.pairKey);
});

test('a catalogue-verified series suffix preserves title evidence and audio sidecars do not count as files', () => {
  const outcome = synchronous([listing('first', { title: 'The Long Road (Long Roads #4)' })],
    { first: info('a', { fileCount: 3, audioFileCount: 1 }) }, (source, editions) => {
      assert.equal(source.title, 'the long road');
      assert.equal(source.title, editions[0].workTitle.toLowerCase());
      return probable;
    });
  assert.equal(outcome.automatic.fileCount, 1);
});

test('a missing detail keeps the usable result in the picker', () => {
  const outcome = synchronous([listing('first'), listing('failed')], { first: info('a') });
  assert.equal(outcome.automatic, null);
  assert.equal(outcome.rows.length, 1);
});

test('an upload with unresolved edition evidence prevents automatic selection beside a confident upload', () => {
  const outcome = synchronous([listing('first'), listing('uncertain')],
    { first: info('a'), uncertain: info('b', { narrator: [] }) },
    source => source.infoHash.startsWith('a') ? probable : { verdict: 'none', reason: 'no_match' });
  assert.equal(outcome.automatic, null);
  assert.equal(outcome.rows.length, 2);
});

test('a distinct verified alternate edition does not prevent a sole target match', () => {
  const outcome = synchronous([listing('first'), listing('other')],
    { first: info('a'), other: info('b', { narrator: ['Ed Cole'] }) },
    source => source.infoHash.startsWith('a') ? probable : { verdict: 'probable', recordingKey: 'work-pair#as:uk' });
  assert.equal(outcome.automatic.request.sourceRef, 'first');
});

test('cancel aborts all active requests and ignores late callbacks', () => {
  const handles = [];
  const search = (term, page, done) => {
    done({ rows: Array.from({ length: 22 }, (_, i) => listing(String(i))), hasMore: false });
    return { cancel() { assert.fail('completed search should have been released'); } };
  };
  const detail = (slug, done) => {
    const handle = { done, aborted: false, cancel() { this.aborted = true; } };
    handles.push(handle);
    return handle;
  };
  const cancel = resolve({ search, detail }, () => probable, () => assert.fail('cancel should suppress completion'));
  assert.equal(handles.length, 3);
  cancel();
  assert.ok(handles.every(handle => handle.aborted));
  handles.forEach(handle => handle.done(info('a')));
  assert.equal(handles.length, 3);
});

test('candidate cap prevents an automatic pick even when one completed result is confident', () => {
  const rows = Array.from({ length: 20 }, (_, i) => listing(String(i)));
  const visited = [];
  let outcome;
  resolve({
    search(term, page, done) { done({ rows }); },
    detail(slug, done) {
      visited.push(slug);
      done(info(slug === '0' ? 'a' : 'b', { narrator: ['Ed Cole'] }));
    }
  }, source => source.infoHash.startsWith('a') ? probable : { verdict: 'probable', recordingKey: 'work-pair#as:uk' },
  value => { outcome = value; });
  assert.equal(visited.length, 12);
  assert.equal(outcome.automatic, null);
});

test('search reads a bounded second page and suppresses automatic selection when more pages remain', () => {
  const pages = [];
  let outcome;
  resolve({
    search(term, page, done) {
      pages.push(page);
      done({ rows: page === 1 ? [listing('first')] : [], hasMore: true });
    },
    detail(slug, done) { done(info('a')); }
  }, () => probable, value => { outcome = value; });
  assert.deepEqual(pages, [1, 2]);
  assert.equal(outcome.rows.length, 1);
  assert.equal(outcome.automatic, null);
});

test('stale search waits for refresh and search timeout completes once', () => {
  let searchDone;
  const outcomes = [];
  resolve({
    search(term, page, done) { searchDone = done; },
    detail() { assert.fail('interim stale results must not start detail requests'); }
  }, () => probable, result => outcomes.push(result));
  searchDone({ rows: [], stale: true });
  assert.equal(outcomes.length, 0);
  searchDone({ rows: [], stale: true }, 'timeout');
  searchDone({ rows: [] });
  assert.equal(outcomes.length, 1);
  assert.equal(outcomes[0].error, 'timeout');
});
