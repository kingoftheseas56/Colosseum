// MangaVolumes.js — pure shelf paging + batch selection.
//
// Loads the .pragma library the anime_episode_presentation_test.mjs way (strip
// the pragma, evaluate with new Function) and proves the paging groups and the
// "next N I don't own, walking forward" rule from the 2026-07-30 design.
//
// NOTE on row shape: MangaTankobanService::volumeMap publishes `number` as a
// QString ("1", "10.5", "Extra" — MangaTankobanTypes.h:18), NOT a JS number.
// The suite therefore drives the STRING shape the app really produces; the
// numeric shape is covered too so neither caller can regress.
import fs from 'fs';

let src = fs.readFileSync('qml/MangaVolumes.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src +
    '\nmodule.pageGroups=pageGroups;' +
    'module.nextBatch=nextBatch;')(mod);

function fail(m) { console.log('FAIL ' + m); process.exit(1); }
function eq(a, b, m) { if (a !== b) fail(m + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }
function deepEq(a, b, m) {
    if (JSON.stringify(a) !== JSON.stringify(b))
        fail(m + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')');
}

// Volume rows exactly as the C++ hands them over: number is a STRING.
function vols(n) { var o = []; for (var i = 1; i <= n; i++) o.push({ id: 'v' + i, number: String(i) }); return o; }
// The numeric shape, so a future caller passing real numbers is covered too.
function numVols(n) { var o = []; for (var i = 1; i <= n; i++) o.push({ id: 'v' + i, number: i }); return o; }
function ownedMap(list) { var o = {}; for (var i = 0; i < list.length; i++) o[list[i]] = true; return o; }
function range(a, b) { var o = []; for (var i = a; i <= b; i++) o.push(i); return o; }

// ── pageGroups ────────────────────────────────────────────────────────────
var g = mod.pageGroups(vols(115), 10);
eq(g.length, 12, 'pageGroups: 115 volumes make 12 pages');
eq(g[0].label, 'Volumes 1–10', 'pageGroups: first page label');
eq(g[3].label, 'Volumes 31–40', 'pageGroups: fourth page label');
eq(g[11].label, 'Volumes 111–115', 'pageGroups: short final page label');
eq(g[11].volumes.length, 5, 'pageGroups: short final page holds 5');
deepEq(mod.pageGroups([], 10), [], 'pageGroups: empty in, empty out');
deepEq(mod.pageGroups(null, 10), [], 'pageGroups: null in, empty out');

var g8 = mod.pageGroups(vols(8), 10);
eq(g8.length, 1, 'pageGroups: 8 volumes make one page');
eq(g8[0].label, 'Volumes 1–8', 'pageGroups: single short page label');

// A page must carry the ROWS, not just the numbers — the shelf renders them.
eq(g[3].volumes[0].id, 'v31', 'pageGroups: page carries the original rows');
eq(g[3].first, 31, 'pageGroups: first');
eq(g[3].last, 40, 'pageGroups: last');

// Numeric rows behave identically to string rows.
var gn = mod.pageGroups(numVols(115), 10);
eq(gn[3].label, 'Volumes 31–40', 'pageGroups: numeric rows label the same as string rows');

// A named volume must never render as "NaN". Real data allows "Extra"
// (MangaTankobanTypes.h:18); a qualified shelf shouldn't contain one, but the
// label is user-visible and must degrade to the raw token rather than lie.
var gx = mod.pageGroups([{ id: 'a', number: '1' }, { id: 'b', number: 'Extra' }], 10);
eq(gx.length, 1, 'pageGroups: mixed page still groups');
eq(gx[0].label.indexOf('NaN'), -1, 'pageGroups: a named volume never renders as NaN');

// ── nextBatch ─────────────────────────────────────────────────────────────
// Nothing owned, never opened: starts at volume 1. (Acceptance 1)
var b1 = mod.nextBatch(vols(115), {}, 0, 10);
deepEq(b1.numbers, range(1, 10), 'nextBatch: cold start takes 1-10');
eq(b1.kind, 'next', 'nextBatch: cold start is a "next" batch');
eq(b1.label, 'Download next 10', 'nextBatch: cold start label');

// Owns 1-34, reading 34 -> 35-44. (Acceptance 2)
var b2 = mod.nextBatch(vols(115), ownedMap(range(1, 34)), 34, 10);
deepEq(b2.numbers, range(35, 44), 'nextBatch: owns 1-34 reading 34 takes 35-44');
eq(b2.first, 35, 'nextBatch: first is 35');
eq(b2.last, 44, 'nextBatch: last is 44');

// Owns 1-10 AND 30-40, reading 34 -> 41-50, NOT 11-20. (Acceptance 3)
var b3 = mod.nextBatch(vols(115), ownedMap(range(1, 10).concat(range(30, 40))), 34, 10);
deepEq(b3.numbers, range(41, 50), 'nextBatch: forward-continue ignores the hole behind');

// The reader is on an un-owned volume: it belongs IN the batch.
var b4 = mod.nextBatch(vols(115), ownedMap(range(1, 33)), 34, 10);
deepEq(b4.numbers, range(34, 43), 'nextBatch: the un-owned volume being read is included');

// Fewer than a full batch left. (Acceptance 7)
var b5 = mod.nextBatch(vols(115), ownedMap(range(1, 110)), 110, 10);
deepEq(b5.numbers, range(111, 115), 'nextBatch: tail takes what remains');
eq(b5.kind, 'remaining', 'nextBatch: tail is a "remaining" batch');
eq(b5.label, 'Download remaining 5', 'nextBatch: tail label');

// Short series, nothing owned. (Acceptance 8)
var b6 = mod.nextBatch(vols(8), {}, 0, 10);
deepEq(b6.numbers, range(1, 8), 'nextBatch: short series takes all 8');
eq(b6.kind, 'all', 'nextBatch: short series is an "all" batch');
eq(b6.label, 'Download all 8', 'nextBatch: short series label');

// Everything owned. (Acceptance 12)
var b7 = mod.nextBatch(vols(8), ownedMap(range(1, 8)), 8, 10);
deepEq(b7.numbers, [], 'nextBatch: fully owned yields nothing');
eq(b7.kind, 'complete', 'nextBatch: fully owned is "complete"');
eq(b7.label, 'All volumes on this device', 'nextBatch: fully owned label');

// No shelf at all.
var b8 = mod.nextBatch([], {}, 0, 10);
eq(b8.kind, 'complete', 'nextBatch: no volumes is "complete"');
deepEq(b8.numbers, [], 'nextBatch: no volumes yields nothing');
var b8b = mod.nextBatch(null, {}, 0, 10);
eq(b8b.kind, 'complete', 'nextBatch: null rows is "complete"');

// Numeric rows behave identically.
var b9 = mod.nextBatch(numVols(115), ownedMap(range(1, 34)), 34, 10);
deepEq(b9.numbers, range(35, 44), 'nextBatch: numeric rows select the same batch');

// A named volume is never selected — it has no number to acquire by.
var b10 = mod.nextBatch([{ id: 'a', number: '1' }, { id: 'b', number: 'Extra' },
                         { id: 'c', number: '2' }], {}, 0, 10);
deepEq(b10.numbers, [1, 2], 'nextBatch: a named volume is skipped, not NaN-selected');

console.log('PASS manga_volume_batch_test');
process.exit(0);
