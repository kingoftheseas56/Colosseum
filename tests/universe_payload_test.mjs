// universe_payload_test.mjs — the shipped payloads still say what the locked plans say.
// Both plans were verified entry-by-entry against live providers on 2026-07-25; five DCAU
// IDs were WRONG rather than missing, one of them an adult film in a Batman row. This file
// is what stops a careless edit undoing that work.
//
// Every screen ID below is pinned BY VALUE, in order, transcribed from the plan documents
// (Task 2/Task 3 of docs/superpowers/plans/2026-07-26-universe-extensions-implementation.md,
// cross-checked against the One Piece and DCAU addon plans §4/§7/§9) — never derived from the
// payload file under test. Counting entries is not a guard: any 1:1 substitution of one valid
// id for another still counts right and still passes a length check.
import { readFileSync } from 'node:fs';
let src = readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.validate=validate;')(mod);

let failed = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq  = (a, b, m) => JSON.stringify(a) === JSON.stringify(b) ? ok(`${m} → ${JSON.stringify(a)}`)
                                                                 : bad(`${m} → ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);

const load = f => JSON.parse(readFileSync(`assets/universes/${f}.json`, 'utf8'));
// Total lookup: a renamed/removed section must fail an assertion, never throw and abort the
// rest of the file (a crash here would hide every assertion after it, see below).
const sect = (v, id) => v.sections.find(s => s.id === id) || { entries: [] };

console.log('One Piece — plan section 4 (ids pinned by value, transcribed from the plan)');
{
  const raw = load('one-piece');
  const v = mod.validate(raw);
  eq(v.title, 'One Piece', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'movies', 'specials', 'manga', 'novels'], 'section order');
  // One Piece plan §9 counts 55 (53 pinned + 2 manual: One Pace TV row + coloured manga).
  // This payload ships 54 — the One Pace manual TV row is not carried here, only the
  // coloured manga is. That is a known, deliberate gap from §9, not a regression.
  eq(v.sections.map(s => s.entries.length), [5, 15, 17, 13, 4],
     'entries per section (54 — plan §9\'s 55 includes a One Pace TV row this payload omits)');
  // Every entry the plan pinned must SURVIVE validation — a drop here means a bad payload.
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');

  eq(sect(v, 'tv').entries.map(e => e.id),
     ['tt0388629', 'tt11737520', 'tt33992229', 'tt36600601', 'tt30476502'],
     'TV ids, in plan §4.1 order');
  eq(sect(v, 'movies').entries.map(e => e.id),
     ['tt0814243', 'tt0832449', 'tt0997084', 'tt1006926', 'tt1010435', 'tt1018764', 'tt1059950',
      'tt1037116', 'tt1206326', 'tt1485763', 'tt1865467', 'tt2375379', 'tt5251328', 'tt9430698', 'tt16183464'],
     'movie ids, in plan §4.2 order');
  eq(sect(v, 'specials').entries.map(e => e.id),
     ['tt1012788', 'tt0975705', 'tt1003286', 'tt1010037', 'tt1012787', 'tt7947592', 'tt2598466',
      'tt3354344', 'tt3354352', 'tt2893336', 'tt5098548', 'tt6597356', 'tt6609162', 'tt6425816',
      'tt11757066', 'tt11744496', 'tt33998607'],
     'special ids, in plan §4.3 order');
  // Row 2 is the digital-coloured edition — now a real WeebCentral source (Hemanth 2026-07-28:
  // use the WeebCentral series for poster/name/contents, distinct from the main One Piece manga).
  eq(sect(v, 'manga').entries.map(e => e.id || '(manual)'),
     ['30013', '01J76XYAQSGEJPXCSCVPQ3MHZM', '44414', '47152', '82353', '102533', '110258', '110233',
      '103252', '110232', '110715', '117802', '154266'],
     'manga ids, in plan §4.4 order — position 2 is the WeebCentral digital-coloured series');
  eq(sect(v, 'novels').entries.map(e => e.id),
     ['1509329459', '1528233153', '6741084754', '6736634886'],
     'novel ids, in plan §4.5 order');

  // The one confirmed art hole must carry its override or it renders empty. Total lookup —
  // filter, never find()+dereference — so a missing row FAILS this line instead of throwing
  // and aborting every assertion after it.
  const fishman = sect(v, 'tv').entries.filter(e => e.id === 'tt33992229');
  eq(fishman.length, 1, 'the Fish-Man Island Log entry is present exactly once');
  eq(!!(fishman[0] || {}).poster, true, 'Fish-Man Island Log has a poster override');
}

console.log('\nDCAU — plan section 4 (ids pinned by value; §7 corrected 3, dropped 2)');
{
  const raw = load('dcau');
  const v = mod.validate(raw);
  eq(v.title, 'DC Animated Universe', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'shorts', 'movies', 'comics'], 'section order');
  eq(v.sections.map(s => s.entries.length), [8, 2, 7, 14], 'entries per section');
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');

  // DCAU §7: three ids were CORRECTED (Fatal Five, Lobo, Gotham Girls) and two were DROPPED
  // as wrong (Chase Me = an adult film; tt3702720 = Full Contact). A wrong id is syntactically
  // perfect — only a value pin catches a revert. JLU (tt6025022) was never a corrected id:
  // §7 records it as doubted, then confirmed RIGHT — not fixed.
  eq(sect(v, 'tv').entries.map(e => e.id),
     ['tt0103359', 'tt0115378', 'tt0118266', 'tt0147746', 'tt0247729', 'tt0260662', 'tt0275137', 'tt6025022'],
     'TV ids, in plan §4.1 order');
  eq(sect(v, 'shorts').entries.map(e => e.id),
     ['tt6075386', 'tt0337763'], 'web short ids (both CORRECTED §7), in plan §4.2 order');
  eq(sect(v, 'movies').entries.map(e => e.id),
     ['tt0106364', 'tt0143127', 'tt0231237', 'tt0233298', 'tt0346578', 'tt6556890', 'tt8752474'],
     'movie ids, in plan §4.3 order — tt8752474 is the CORRECTED Fatal Five id');
  // The two ids killed in §7 must never reappear anywhere in the payload.
  const allIds = v.sections.flatMap(s => s.entries).map(e => e.id);
  eq(['tt0428284', 'tt3702720'].filter(x => allIds.indexOf(x) >= 0), [], 'no dropped-as-wrong id resurfaces');

  // The amendment: comics pin post IDs. A tag imports same-named mainline books silently.
  const comics = sect(v, 'comics');
  eq(comics.entries.every(e => Array.isArray(e.posts) && e.posts.length > 0), true,
     'every comic entry pins explicit post IDs');
  eq(comics.entries.some(e => e.tag), false, 'no comic entry carries a tag');
}

console.log('\nevery video entry can reach Theatre safely');
for (const f of ['one-piece', 'dcau']) {
  const v = mod.validate(load(f));
  const badEntries = v.sections.filter(s => s.kind === 'video')
    .flatMap(s => s.entries).filter(e => !e.id || !(e.type === 'movie' || e.type === 'series'));
  eq(badEntries.length, 0, `${f}: no video entry missing id or type`);
}

console.log('\nthe 14 GetComics posts stay exactly as verified 2026-07-26');
{
  const comics = sect(mod.validate(load('dcau')), 'comics');
  // Full arrays, not posts[0] — a bogus id appended after the verified one must fail too.
  eq(comics.entries.map(e => e.posts),
     [[11366], [153724], [80956], [50187], [14615], [48881], [15941],
      [190572], [163954], [282726], [10563], [10470], [183948], [8823]],
     'comic posts, full arrays in plan order');
  eq(comics.entries.every(e => e.posts.every(p => Number.isInteger(p) && p > 0)), true,
     'every post id is a positive integer, never a string');
}

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
