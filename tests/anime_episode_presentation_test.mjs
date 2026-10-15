// AnimeEpisodePresentation.js — pure presentation/queue behavior.
//
// Loads the .pragma library the abb_parse_test.mjs way (strip the pragma,
// evaluate with new Function) and proves mode selection, season ordering,
// Absolute/Seasons filtering, special exclusion, and cross-season playback
// targets that preserve the original provider stream id / season / episode.
import fs from 'fs';

let src = fs.readFileSync('qml/AnimeEpisodePresentation.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src +
    '\nmodule.effectiveMode=effectiveMode;' +
    'module.seasonNumbers=seasonNumbers;' +
    'module.visibleEpisodes=visibleEpisodes;' +
    'module.playbackEpisodes=playbackEpisodes;' +
    'module.playbackTargets=playbackTargets;')(mod);

function fail(m) { console.log('FAIL ' + m); process.exit(1); }
function eq(a, b, m) { if (a !== b) fail(m + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }
function deepEq(a, b, m) {
    if (JSON.stringify(a) !== JSON.stringify(b))
        fail(m + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')');
}

// A complete One Piece-style model: a contiguous window crossing the S1→S2
// provider boundary (abs 7,8,9) plus one special.
const complete = {
    status: 'mapped',
    absoluteComplete: true,
    defaultOrder: 'absolute',
    seasons: [
        { number: 1, label: 'Season 1', count: 2 },
        { number: 2, label: 'Season 2', count: 1 },
        { number: 0, label: 'Specials', count: 1 }
    ],
    episodes: [
        { streamId: 'tt:1:7', sourceSeason: 1, sourceEpisode: 7, absoluteNumber: 7, kind: 'episode', mapped: true },
        { streamId: 'tt:1:8', sourceSeason: 1, sourceEpisode: 8, absoluteNumber: 8, kind: 'episode', mapped: true },
        { streamId: 'tt:2:1', sourceSeason: 2, sourceEpisode: 1, absoluteNumber: 9, kind: 'episode', mapped: true },
        { streamId: 'tt:0:3', sourceSeason: 0, sourceEpisode: 3, absoluteNumber: null, kind: 'special', mapped: false }
    ]
};

// 1. Incomplete/unavailable models force seasons even if absolute is requested.
eq(mod.effectiveMode(null, 'absolute'), 'seasons', 'null model forces seasons');
eq(mod.effectiveMode({ absoluteComplete: false, defaultOrder: 'absolute' }, 'absolute'), 'seasons',
   'incomplete model forces seasons even when absolute requested');

// 2. A complete model defaults to its native defaultOrder; explicit wins.
eq(mod.effectiveMode(complete, ''), 'absolute', 'complete model defaults to native absolute');
eq(mod.effectiveMode({ ...complete, defaultOrder: 'seasons' }, ''), 'seasons',
   'complete model with seasons default stays on seasons');
eq(mod.effectiveMode(complete, 'seasons'), 'seasons', 'explicit seasons request is honored');

// 3. Season numbers sort ascending with season 0 last.
deepEq(mod.seasonNumbers(complete), [1, 2, 0], 'seasons ascending with specials last');

// 4. Absolute view is mapped regular rows only, sorted by absolute number.
const abs = mod.visibleEpisodes(complete, 'absolute', 0);
eq(abs.length, 3, 'absolute view excludes the special');
deepEq(abs.map(e => e.streamId), ['tt:1:7', 'tt:1:8', 'tt:2:1'], 'absolute view sorted by absolute number');

// 5. Seasons view filters by source season and keeps source episode order.
deepEq(mod.visibleEpisodes(complete, 'seasons', 1).map(e => e.streamId), ['tt:1:7', 'tt:1:8'],
       'seasons view returns season 1 rows in episode order');
deepEq(mod.visibleEpisodes(complete, 'seasons', 0).map(e => e.streamId), ['tt:0:3'],
       'season 0 shows the specials');

// Incomplete model ignores canonical ordering entirely.
const incomplete = { absoluteComplete: false, defaultOrder: 'seasons', episodes: complete.episodes };
deepEq(mod.visibleEpisodes(incomplete, 'absolute', 1).map(e => e.streamId), ['tt:1:7', 'tt:1:8'],
       'incomplete model falls back to seasons for a requested absolute view');

// Raw provider rows (non-anime shape) still filter by season via fallback.
const raw = {
    absoluteComplete: false, defaultOrder: 'seasons',
    episodes: [{ id: 'r:1:1', season: 1, episode: 1 }, { id: 'r:1:2', season: 1, episode: 2 }]
};
deepEq(mod.visibleEpisodes(raw, 'seasons', 1).map(e => e.id), ['r:1:1', 'r:1:2'],
       'raw provider rows filter by season fallback');

// 6-8. Playback targets: cross-season queue, preserved identities, no specials.
const targets = mod.playbackTargets(complete, 'absolute', 0, 'One Piece', 'art.jpg');
eq(targets.length, 3, 'three absolute targets (special excluded)');
eq(targets[2].id, 'tt:2:1', 'target id is the original stream id');
eq(targets[2].season, 2, 'target season is the source season');
eq(targets[2].episode, 1, 'target episode is the source episode');
eq(targets[2].title, 'One Piece - Episode 9', 'absolute title uses the absolute number');
eq(targets[2].metaLine, 'Episode 9', 'metaLine reads Episode N');
eq(targets[0].backdrop, 'art.jpg', 'backdrop is carried onto every target');
eq(targets.some(t => t.id === 'tt:0:3'), false, 'specials never appear in the absolute queue');

const ids = targets.map(t => t.id);
eq(ids[ids.indexOf('tt:1:8') + 1], 'tt:2:1', 'absolute queue crosses S1E8 into S2E1');

// 9. Locating the now-playing target yields the correct queue index.
eq(targets.findIndex(t => t.id === 'tt:2:1'), 2, 'now-playing target index located in the queue');

// 10. Rows with no (or empty) stream id fall back to seriesId:season:episode,
//     matching EpisodeBrowser.episodesFor — otherwise non-anime queues lose ids.
const idless = {
    absoluteComplete: true, defaultOrder: 'absolute',
    episodes: [
        { sourceSeason: 1, sourceEpisode: 4, absoluteNumber: 4, kind: 'episode', mapped: true },
        { streamId: '', sourceSeason: 1, sourceEpisode: 5, absoluteNumber: 5, kind: 'episode', mapped: true }
    ]
};
const idlessTargets = mod.playbackTargets(idless, 'absolute', 0, 'Show', 'art', 'kitsu:99');
eq(idlessTargets[0].id, 'kitsu:99:1:4', 'a missing stream id is built from seriesId:season:episode');
eq(idlessTargets[1].id, 'kitsu:99:1:5', 'an empty stream id is also rebuilt from seriesId');
// A present stream id still wins over the constructed fallback.
eq(mod.playbackTargets(complete, 'absolute', 0, 'One Piece', 'art', 'seriesX')[2].id, 'tt:2:1',
   'a present stream id is never overwritten by the fallback');

console.log('PASS anime episode presentation and playback queues');
process.exit(0);
