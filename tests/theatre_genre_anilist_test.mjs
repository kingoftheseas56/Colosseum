// TheatreGenreApi.js — AniList fallback for the anime genre pages.
//
// Jikan's /anime?genres= filter endpoint 504s under load (the /genres/anime list
// and /top/anime stay up), so an anime genre page falls to AniList's GraphQL,
// mapped into the same MAL-template card shape. This proves the pure mapping and
// the sort selection; the async fetch orchestration is verified in-app.
import fs from 'fs';

let src = fs.readFileSync('qml/TheatreGenreApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.anilistToCard=anilistToCard;module.anilistSort=anilistSort;')(mod);

function fail(m) { console.log('FAIL ' + m); process.exit(1); }
function eq(a, b, m) { if (a !== b) fail(m + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }

// sort mode -> AniList MediaSort enum
eq(mod.anilistSort('score'), 'SCORE_DESC', 'score sorts by SCORE_DESC');
eq(mod.anilistSort('readers'), 'POPULARITY_DESC', 'watchers sort defaults to POPULARITY_DESC');
eq(mod.anilistSort(''), 'POPULARITY_DESC', 'empty sort defaults to popularity');

// a full AniList media -> the MAL-template card shape the page already renders
const media = {
    idMal: 16498, id: 16498,
    title: { english: 'Attack on Titan', romaji: 'Shingeki no Kyojin' },
    coverImage: { large: 'https://img/aot.jpg' },
    format: 'TV', seasonYear: 2013, startDate: { year: 2013 },
    episodes: 25, status: 'FINISHED', averageScore: 85, popularity: 700000,
    studios: { nodes: [{ name: 'Wit Studio' }] },
    genres: ['Action', 'Drama', 'Fantasy', 'Mystery'],
    description: 'Several hundred years ago, humans were <br>nearly exterminated by Titans.'
};
const c = mod.anilistToCard(media, 0);
eq(c.title, 'Attack on Titan', 'prefers the english title');
eq(c.cover, 'https://img/aot.jpg', 'cover comes from coverImage.large');
eq(c.type, 'TV', 'format maps to type');
eq(c.year, 2013, 'seasonYear maps to year');
eq(c.metaCounts, '25 ep', 'episode count renders as N ep');
eq(c.score, 8.5, 'averageScore is scaled from 0-100 to the 0-10 MAL scale');
eq(c.authors, 'Wit Studio', 'the main studio becomes the author line');
eq(c.item.id, 'mal:16498', 'item id keeps the mal: scheme via idMal so clicks resolve like Jikan cards');
eq(c.item.type, 'series', 'item type is series');
if (c.synopsis.indexOf('<') >= 0) fail('synopsis must strip AniList HTML tags');
if (c.genres.length !== 4) fail('genres are carried through');

// romaji fallback + no-idMal fallback + null score
const c2 = mod.anilistToCard({ id: 999, title: { romaji: 'Only Romaji' }, coverImage: {}, genres: [], averageScore: null }, 1);
eq(c2.title, 'Only Romaji', 'falls back to the romaji title');
eq(c2.item.id, 'anilist:999', 'a media with no MAL id uses the anilist: scheme');
eq(c2.score, null, 'a null averageScore stays null (no fake rating)');
eq(c2.metaCounts, '—', 'no episode count renders as a dash');

console.log('PASS theatre genre AniList fallback mapping');
process.exit(0);
