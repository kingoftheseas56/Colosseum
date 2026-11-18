// Deterministic, offline contract tests for the ABB source adapter. No HTTP or audio.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync('qml/AbbApi.js', 'utf8')
  .replace(/^\.pragma library\s*$/m, '');
const fixture = (name) => fs.readFileSync('tests/fixtures/abb/' + name + '.html', 'utf8');
const home = fixture('latest-live');
const page2 = fixture('latest-page2');
const categoryHtml = fixture('category-fantasy');
const languageHtml = fixture('language-english');
const searchHtml = fixture('search-page2');
const fullHtml = fixture('detail-full');
const dramatizedHtml = fixture('detail-dramatized');

function environment() {
  const requests = [];
  const timers = new Map();
  let nowMs = 1000000000;
  let nextTimer = 0;
  class FakeXHR {
    static DONE = 4;
    constructor() { this.status = 0; this.readyState = 0; this.responseText = ''; this.aborted = false; requests.push(this); }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader() {}
    send() { this.sent = true; }
    abort() { this.aborted = true; if (this.onabort) this.onabort(); }
    respond(status, html) {
      this.status = status; this.responseText = html || ''; this.readyState = 4;
      if (this.onreadystatechange) this.onreadystatechange();
    }
    fail() { if (this.onerror) this.onerror(); else this.respond(0, ''); }
  }
  const sandbox = {
    XMLHttpRequest: FakeXHR,
    Date: { now: () => nowMs },
    setTimeout: (fn, delay) => { const id = ++nextTimer; timers.set(id, { fn, delay }); return id; },
    clearTimeout: (id) => timers.delete(id),
  };
  vm.runInNewContext(source, sandbox, { filename: 'qml/AbbApi.js' });
  return {
    api: sandbox, requests, timers,
    newest: () => requests[requests.length - 1],
    advance: (ms) => { nowMs += ms; },
    fireTimers: () => {
      const jobs = Array.from(timers.values()); timers.clear();
      for (const job of jobs) job.fn();
    },
  };
}

let passed = 0;
let failed = 0;
function check(name, fn) {
  try { fn(); passed++; console.log('PASS ' + name); }
  catch (error) { failed++; console.error('FAIL ' + name + ': ' + error.message); }
}
function onePage(env, invoke, expectedPath, html, page) {
  const values = [];
  invoke((value, reason) => values.push({ value, reason }));
  assert.equal(env.newest().url, 'https://audiobookbay.lu' + expectedPath);
  env.newest().respond(200, html);
  assert.equal(values.length, 1);
  assert.equal(values[0].value.page, page);
  assert.equal(values[0].value.hasMore, true);
  assert.ok(values[0].value.rows.length > 0);
  return values[0].value;
}

check('latest pages use ABB pagination and return parsed rows', () => {
  const env = environment();
  onePage(env, (done) => env.api.latest(1, done), '/', home, 1);
  onePage(env, (done) => env.api.latest(2, done), '/page/2/', page2, 2);
});

check('category uses real type path, including page 2', () => {
  const env = environment();
  onePage(env, (done) => env.api.category('fantasy', 1, done),
    '/audio-books/type/fantasy/', categoryHtml, 1);
  env.api.category('fantasy', 2, () => {});
  assert.equal(env.newest().url, 'https://audiobookbay.lu/audio-books/type/fantasy/page/2/');
});

check('language uses real tag path, including page 2', () => {
  const env = environment();
  onePage(env, (done) => env.api.language('english', 1, done),
    '/audio-books/tag/english/', languageHtml, 1);
  env.api.language('english', 2, () => {});
  assert.equal(env.newest().url, 'https://audiobookbay.lu/audio-books/tag/english/page/2/');
});

check('paged search remains lowercase and parses search fixture', () => {
  const env = environment();
  onePage(env, (done) => env.api.search('Rhythm of War', 2, done),
    '/page/2/?s=rhythm+of+war', searchHtml, 2);
});

check('last page without a next link has hasMore=false', () => {
  const env = environment();
  const final = home.replace(/<div class="wp-pagenavi">[\s\S]*?<\/div>/,
    '<div class="wp-pagenavi"><span class="current">500</span></div>');
  const values = [];
  env.api.latest(500, (value) => values.push(value));
  env.newest().respond(200, final);
  assert.equal(values.length, 1);
  assert.ok(values[0].rows.length > 0);
  assert.equal(values[0].hasMore, false);
});

check('full Rhythm of War recording has explicit two-person narrator list', () => {
  const d = environment().api.parseDetail(fullHtml);
  assert.equal(d.infoHash, '1d32f8b449ebe0d63b9d08ba6e86525ff17baa3d');
  assert.deepEqual(Array.from(d.narrator), ['Kate Reading', 'Michael Kramer']);
  assert.equal(d.format, 'M4B');
  assert.equal(d.bitrate, '64 Kbps');
  assert.equal(d.abridgement, 'unabridged');
  assert.equal(d.language, 'English');
  assert.equal(d.postedDate, '2020-11-17');
  assert.equal(d.fileCount, 3);
  assert.ok(d.contents.includes('1 × .m4b'));
  assert.ok(d.description.includes('The Stormlight Archive saga continues'));
  assert.equal(/<[^>]+>/.test(d.description), false);
});

check('GraphicAudio part preserves dramatization and separate narrator strings', () => {
  const d = environment().api.parseDetail(dramatizedHtml);
  assert.deepEqual(Array.from(d.narrator), ['GraphicAudio', 'Graphic Audio']);
  assert.equal(d.format, 'MP3');
  assert.equal(d.bitrate, '256 Kbps');
  assert.equal(d.abridgement, 'dramatized');
  assert.equal(d.language, 'English');
  assert.equal(d.postedDate, '2021-05-01');
  assert.equal(d.fileCount, 9); // six MP3 files plus three non-audio files
  assert.ok(d.contents.includes('.mp3'));
});

check('unsupported detail facts remain empty, never inferred from title', () => {
  const d = environment().api.parseDetail('<h1>Amazing Unabridged Series 1-3</h1>');
  assert.deepEqual(Array.from(d.narrator), []);
  for (const key of ['infoHash', 'contents', 'format', 'bitrate', 'abridgement', 'language', 'description', 'postedDate'])
    assert.equal(d[key], '', key);
  assert.equal(d.fileCount, 0);
});

check('bundle hints mark packs but not single books or part numbers', () => {
  const api = environment().api;
  const assemblyTitle = /<h1[^>]*>([^<]+)<\/h1>/.exec(fixture('assembly-bundle'))[1];
  const mini = '<div class="post"><h2><a href="/abss/the-assembly-1-4-steve-mchugh/">' + assemblyTitle +
    '</a></h2></div><div class="post"><h2><a href="/abss/rhythm-part-03-01/">' +
    'Rhythm of War [03-01] - Brandon Sanderson</a></h2></div>';
  const rows = api.parseSearch(mini);
  assert.equal(rows[0].bundle, true);
  assert.equal(rows[1].bundle, false);
  for (const name of ['Books 1-3', 'Box Set', 'Complete Collection', '1-4']) {
    const one = '<div class="post"><h2><a href="/abss/example/">' + name + '</a></h2></div>';
    assert.equal(api.parseSearch(one)[0].bundle, true, name);
  }
  for (const name of ['Parts 1-3', 'Part 1-3', 'Episode 1-3']) {
    const one = '<div class="post"><h2><a href="/abss/example/">' + name + '</a></h2></div>';
    assert.equal(api.parseSearch(one)[0].bundle, false, name);
  }
});

check('timeout aborts and returns one timeout reason', () => {
  const env = environment();
  const values = [];
  env.api.requestText('https://audiobookbay.lu/', (html, reason) =>
    values.push([html, reason]), { timeoutMs: 25 });
  assert.equal(Array.from(env.timers.values())[0].delay, 25);
  env.fireTimers();
  assert.equal(env.newest().aborted, true);
  assert.deepEqual(values, [[null, 'timeout']]);
  env.newest().respond(200, home);
  assert.equal(values.length, 1);
});

check('explicit cancel suppresses callback, opt-in cancellation reports reason', () => {
  const env = environment();
  const values = [];
  const handle = env.api.requestText('/one', (html, reason) => values.push(reason));
  handle.cancel();
  env.newest().respond(200, home);
  env.fireTimers();
  assert.equal(values.length, 0);
  const second = env.api.requestText('/two', (html, reason) => values.push(reason));
  second.cancel(true);
  assert.deepEqual(values, ['cancelled']);
});

check('HTTP and network errors expose actionable reasons', () => {
  const env = environment();
  const values = [];
  env.api.requestText('/bad', (html, reason) => values.push([html, reason]));
  env.newest().respond(429, 'rate limited');
  env.api.requestText('/offline', (html, reason) => values.push([html, reason]));
  env.newest().fail();
  assert.deepEqual(values, [[null, 'http-429'], [null, 'offline']]);
});

check('cache fresh hit skips network; expired value is shown then refreshed', () => {
  const env = environment();
  const storage = new Map();
  env.api.setAbbCacheStore({ get: (k) => storage.get(k), put: (k, v) => storage.set(k, v) });
  const results = [];
  env.api.latest(1, v => results.push(v));
  env.newest().respond(200, home);
  assert.equal(results.length, 1);
  env.api.latest(1, v => results.push(v));
  assert.equal(env.requests.length, 1);
  assert.equal(results.length, 2);
  assert.ok(results[1].rows.length > 0);
  env.advance(21601000);
  env.api.latest(1, v => results.push(v));
  assert.equal(env.requests.length, 2);
  assert.equal(results[2].stale, true);
  env.newest().respond(200, page2);
  assert.equal(results.length, 4);
  assert.equal(results[3].stale, false);
  const cached = env.api.abbCacheGet('https://audiobookbay.lu/');
  assert.equal(cached.stale, false);
});

check('offline returns stale cached rows with offline=true', () => {
  const env = environment();
  const storage = new Map();
  env.api.setAbbCacheStore({ get: k => storage.get(k), put: (k, v) => storage.set(k, v) });
  env.api.latest(1, () => {});
  env.newest().respond(200, home);
  env.advance(21601000);
  const updates = [];
  env.api.latest(1, (value) => updates.push(value));
  assert.equal(updates[0].stale, true);
  env.newest().fail();
  assert.equal(updates[1].offline, true);
  assert.equal(updates[1].rows.length, updates[0].rows.length);
});

check('invalid browse slugs fail closed without a URL request', () => {
  const env = environment();
  const results = [];
  env.api.category('../foo', 1, v => results.push(v));
  assert.equal(env.requests.length, 0);
  assert.deepEqual(Array.from(results[0].rows), []);
});

check('uncached browse failure keeps its page contract and marks offline', () => {
  const env = environment();
  const values = [];
  env.api.latest(1, (value, reason) => values.push({ value, reason }));
  env.newest().fail();
  assert.equal(values.length, 1);
  assert.equal(values[0].reason, 'offline');
  assert.deepEqual(Array.from(values[0].value.rows), []);
  assert.equal(values[0].value.page, 1);
  assert.equal(values[0].value.hasMore, false);
  assert.equal(values[0].value.offline, true);
});

check('stale cache HTTP errors do not masquerade as offline', () => {
  const env = environment();
  const storage = new Map();
  env.api.setAbbCacheStore({ get: k => storage.get(k), put: (k, v) => storage.set(k, v) });
  env.api.latest(1, () => {});
  env.newest().respond(200, home);
  env.advance(21601000);
  const values = [];
  env.api.latest(1, (value, reason) => values.push({ value, reason }));
  env.newest().respond(503, '');
  assert.equal(values.length, 2);
  assert.equal(values[1].reason, 'http-503');
  assert.equal(values[1].value.stale, true);
  assert.equal(values[1].value.offline, false);
  assert.ok(values[1].value.rows.length > 0);
});

check('detail lookup exposes provider timeout and supports aborting the request', () => {
  const env = environment();
  const values = [];
  const request = env.api.fetchDetail('rhythm-of-war', (value, reason) => values.push([value, reason]));
  assert.equal(env.newest().url, 'https://audiobookbay.lu/abss/rhythm-of-war/');
  request.cancel();
  assert.equal(env.newest().aborted, true);
  assert.equal(values.length, 0);
  env.api.fetchDetail('rhythm-of-war', (value, reason) => values.push([value, reason]));
  env.fireTimers();
  assert.deepEqual(values, [[null, 'timeout']]);
});

check('structured detail credits and seeder counts are available for comparison', () => {
  const env = environment();
  const html = fullHtml.replace('<td>Info Hash:</td>', '<td>Seeders:</td><td>42</td></tr><tr><td>Info Hash:</td>');
  env.api.fetchDetail('rhythm-of-war', (value) => {
    assert.equal(value.author, 'Brandon Sanderson');
    assert.equal(value.seeders, 42);
    assert.equal(value.audioFileCount, 1);
  });
  env.newest().respond(200, html);
});

console.log('SUMMARY ' + passed + ' passed, ' + failed + ' failed');
if (failed) process.exitCode = 1;
