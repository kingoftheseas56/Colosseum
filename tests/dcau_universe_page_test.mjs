import fs from 'fs';

let failed = 0;
const ok = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failed++; };
const eq = (a,b,m) => JSON.stringify(a) === JSON.stringify(b) ? ok(m) : bad(`${m}: ${JSON.stringify(a)} != ${JSON.stringify(b)}`);

const payload = JSON.parse(fs.readFileSync('assets/universes/dcau.json','utf8')).universe;
const sections = Object.fromEntries(payload.sections.map(s => [s.id,s]));
const videos = [...sections.tv.entries, ...sections.shorts.entries, ...sections.movies.entries];
const expectedVideoIds = [
  'tt0103359','tt0115378','tt0118266','tt0147746','tt0247729','tt0260662','tt0275137','tt6025022',
  'tt6075386','tt0337763','tt0106364','tt0143127','tt0231237','tt0233298','tt0346578','tt6556890','tt8752474'
];
eq(videos.map(v=>v.id).sort(), expectedVideoIds.slice().sort(), 'all 17 Cinemeta/IMDb IDs remain canonical');

const comicMap = {
  'The Batman Adventures':[4374,11366],
  'The Batman Adventures: Mad Love':[4943,153724],
  'Mad Love Deluxe Edition':[4943,80956],
  'Batman & Robin Adventures':[5222,50187],
  'Superman Adventures':[5531,14615],
  'Adventures in the DC Universe':[5719,48881],
  'Batman: Gotham Adventures':[5954,15941],
  'Batman Beyond':[6259,190572],
  'Batman Beyond (TV tie-ins)':[6261,163954],
  'Batman Beyond: Return of the Joker':[18999,282726],
  'Justice League Adventures':[10924,10563],
  'Gotham Girls':[13050,10470],
  'Batman Adventures Vol. 2':[15821,183948],
  'Justice League Unlimited':[11988,8823]
};
for (const c of sections.comics.entries) {
  const expected = comicMap[c.title];
  if (!expected) { bad(`unexpected comic ${c.title}`); continue; }
  eq([c.gcdId, c.posts[0]], expected, `${c.title} canonical GCD/GetComics mapping`);
  eq(c.id, `gcd:${c.gcdId}`, `${c.title} uses canonical progress id`);
}
eq(sections.comics.entries.length, 14, '14 DCAU comic entries');

const dataPath='qml/DCAUUniverseData.js';
eq(fs.existsSync(dataPath), true, 'DCAUUniverseData.js exists');
if (fs.existsSync(dataPath)) {
  const src=fs.readFileSync(dataPath,'utf8').replace(/^\.pragma library\s*$/m,'');
  const mod={};
  new Function('module', src+'\nmodule.hubs=hubs;module.hub=hub;module.videosForHub=videosForHub;module.comicsForHub=comicsForHub;module.progressBelongsToJustice=progressBelongsToJustice;')(mod);
  eq(mod.hubs.map(h=>h.id), ['gotham','metropolis','justice','future'], 'four hub order');
  eq(mod.hubs.flatMap(h=>h.videoIds).sort(), expectedVideoIds.slice().sort(), 'video partition covers all works exactly once');
  eq(mod.hubs.flatMap(h=>h.comicPosts).sort((a,b)=>a-b), Object.values(comicMap).map(x=>x[1]).sort((a,b)=>a-b), 'comic partition covers all works exactly once');
  eq(mod.videosForHub(payload,'justice').map(x=>x.id), ['tt0247729','tt0275137','tt6025022','tt8752474'], 'Justice Theatre order');
  eq(mod.comicsForHub(payload,'justice').map(x=>x.gcdId), [5719,10924,11988], 'Justice Tankoban order');
  eq(mod.progressBelongsToJustice({kind:'video',id:'tt0275137:1:9'}), true, 'episode progress groups by IMDb root');
  eq(mod.progressBelongsToJustice({kind:'comic',id:'gcd:10924'}), true, 'comic progress groups by GCD id');
  eq(mod.progressBelongsToJustice({kind:'video',id:'tt0103359:1:1'}), false, 'Gotham progress excluded from Justice Continue');
}

const portrait=fs.readFileSync('qml/PortraitTile.qml','utf8');
eq(portrait.includes('property real posterWidth'), true, 'PortraitTile exposes default-preserving posterWidth override');
eq(portrait.includes('width: tile.posterWidth'), true, 'PortraitTile width follows posterWidth');

for (const f of [
 'qml/DCAUWorldPortal.qml','qml/DCAUEnvironmentGotham.qml','qml/DCAUEnvironmentMetropolis.qml',
 'qml/DCAUEnvironmentSpace.qml','qml/DCAUEnvironmentFutureGotham.qml',
 'assets/universes/dcau/portals/gotham.jpg','assets/universes/dcau/portals/metropolis.jpg',
 'assets/universes/dcau/portals/watchtower.jpg','assets/universes/dcau/portals/future-gotham.jpg'])
 eq(fs.existsSync(f),true,`${f} exists`);

const pagePath='qml/DCAUUniversePage.qml';
eq(fs.existsSync(pagePath),true,'DCAUUniversePage.qml exists');
if (fs.existsSync(pagePath)) {
 const q=fs.readFileSync(pagePath,'utf8');
 for (const n of ['ContinueTile','DCAUWorldPortal','DCAUWorldPage','Progress.recent("", 100)','continueResumeRequested','continueDetailRequested'])
   eq(q.includes(n),true,`DCAU page contains ${n}`);
 eq(q.includes('live.metahub.space'),false,'DCAU portal page never substitutes Cinemeta posters for location art');
 eq(q.includes('assets/universes/dcau/portals/'),true,'DCAU portal page uses bundled location artwork');
}
const portalSrc=fs.readFileSync('qml/DCAUWorldPortal.qml','utf8');
eq(portalSrc.includes('objectName: \"dcauPortal_\"'),true,'portal component exposes stable Lanista name');
const worldPath='qml/DCAUWorldPage.qml';
eq(fs.existsSync(worldPath),true,'DCAUWorldPage.qml exists');
if (fs.existsSync(worldPath)) {
 const q=fs.readFileSync(worldPath,'utf8');
 for (const n of ['PortraitTile','CataloguePosterCard','visualProfile: "gallery"','posterWidth: 180','width: 200'])
   eq(q.includes(n),true,`DCAU world page contains ${n}`);
}

const main=fs.readFileSync('qml/Main.qml','utf8');
eq(main.includes('com.colosseum.universe.dcau'),true,'Main recognizes DCAU extension id');
eq(main.includes('DCAUUniversePage.qml'),true,'Main routes DCAU to bespoke page');
eq(main.includes('UniverseExtensionPage.qml'),true,'Main preserves generic fallback');
eq(main.includes('item.comicRequested.connect(win.openGcdSeries)'),true,'DCAU comics route to GCD series page');
eq(main.includes('function _openPinnedGcdSeries(d)'),true,'GCD route supports pinned-post fallback without fuzzy identity');
eq(main.includes('if (_openPinnedGcdSeries(d)) return'),true,'missing local GCD row uses pinned canonical fallback');
eq(main.includes('item.continueResumeRequested.connect(win.resumeContinue)'),true,'DCAU Continue resumes through existing route');
eq(main.includes('item.continueDetailRequested.connect(win.detailContinue)'),true,'DCAU Continue details through existing route');

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
