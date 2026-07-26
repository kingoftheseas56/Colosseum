// universe_payload_test.mjs — the shipped payloads still say what the locked plans say.
// Both plans were verified entry-by-entry against live providers on 2026-07-25; five DCAU
// IDs were WRONG rather than missing, one of them an adult film in a Batman row. This file
// is what stops a careless edit undoing that work.
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

console.log('One Piece — plan section 4');
{
  const raw = load('one-piece');
  const v = mod.validate(raw);
  eq(v.title, 'One Piece', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'movies', 'specials', 'manga', 'novels'], 'section order');
  eq(v.sections.map(s => s.entries.length), [5, 15, 17, 13, 4], 'entries per section');
  // Every entry the plan pinned must SURVIVE validation — a drop here means a bad payload.
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');
  // Hemanth's ruling: the coloured edition sits at position 2, beside the main manga.
  const manga = v.sections.find(s => s.id === 'manga');
  eq(manga.entries[1].manual, true, 'the manual coloured edition is at position 2');
  eq(manga.entries[0].id, '30013', 'and the main manga is first');
  // The one confirmed art hole must carry its override or it renders empty.
  const tv = v.sections.find(s => s.id === 'tv');
  eq(!!tv.entries.find(e => e.id === 'tt33992229').poster, true, 'Fish-Man Island Log has a poster override');
}

console.log('\nDCAU — plan section 4');
{
  const raw = load('dcau');
  const v = mod.validate(raw);
  eq(v.title, 'DC Animated Universe', 'title');
  eq(v.sections.map(s => s.id), ['tv', 'shorts', 'movies', 'comics'], 'section order');
  eq(v.sections.map(s => s.entries.length), [8, 2, 7, 14], 'entries per section');
  const rawCount = raw.universe.sections.reduce((a, s) => a + s.entries.length, 0);
  eq(v.sections.reduce((a, s) => a + s.entries.length, 0), rawCount, 'nothing dropped by validation');
  // The amendment: comics pin post IDs. A tag imports same-named mainline books silently.
  const comics = v.sections.find(s => s.id === 'comics');
  eq(comics.entries.every(e => Array.isArray(e.posts) && e.posts.length > 0), true,
     'every comic entry pins explicit post IDs');
  eq(comics.entries.some(e => e.tag), false, 'no comic entry carries a tag');
  // Four IDs corrected during verification — pin them so a "tidy-up" cannot revert them.
  const tv = v.sections.find(s => s.id === 'tv');
  eq(tv.entries.find(e => e.title.indexOf('Unlimited') >= 0).id, 'tt6025022', 'JLU keeps its verified id');
  const shorts = v.sections.find(s => s.id === 'shorts');
  eq(shorts.entries.map(e => e.id), ['tt6075386', 'tt0337763'], 'the two web shorts keep their corrected ids');
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
  const comics = mod.validate(load('dcau')).sections.find(s => s.id === 'comics');
  eq(comics.entries.map(e => e.posts[0]),
     [11366, 153724, 80956, 50187, 14615, 48881, 15941,
      190572, 163954, 282726, 10563, 10470, 183948, 8823],
     'post ids in plan order');
  eq(comics.entries.every(e => e.posts.every(p => Number.isInteger(p) && p > 0)), true,
     'every post id is a positive integer, never a string');
}

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
