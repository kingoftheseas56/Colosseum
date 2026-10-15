import fs from 'fs';

let src = fs.readFileSync('qml/OnePieceCatalogApi.js', 'utf8')
    .replace(/^\.pragma library\s*$/m, '');
const api = {};
new Function('module', src + `
module.setRequestAdapter=setRequestAdapter;
module.resetCache=resetCache;
module.numbersFromSpec=numbersFromSpec;
module.loadAnimeEpisodes=loadAnimeEpisodes;
module.loadLiveActionEpisodes=loadLiveActionEpisodes;
module.loadOnePaceEpisodes=loadOnePaceEpisodes;
module.selectOnePaceEpisodes=selectOnePaceEpisodes;
module.selectVolumes=selectVolumes;`)(api);

let failed = 0;
function eq(actual, expected, label) {
    if (JSON.stringify(actual) === JSON.stringify(expected)) console.log('ok  ' + label);
    else { console.log('FAIL ' + label, actual, 'expected', expected); failed++; }
}

eq(api.numbersFromSpec('45, 48-53'), [45,48,49,50,51,52,53], 'sparse wiki range parsing');
function kitsuPage(offset) {
    const data = [];
    for (let n = offset + 1; n <= offset + 20; n++)
        data.push({ attributes: { number: n, canonicalTitle: `Episode ${n}`, thumbnail: {} } });
    return { data };
}

let requestedOffsets = [];
api.resetCache();
api.setRequestAdapter((url, done) => {
    const m = /offset%5D=(\d+)/.exec(url);
    const offset = m ? Number(m[1]) : 0;
    requestedOffsets.push(offset);
    done(kitsuPage(offset));
});
let futureRows = null;
api.loadAnimeEpisodes({ anime: '61-63' }, rows => { futureRows = rows; });
eq(futureRows.map(r => r.episode), [61,62,63], 'anime fetch grows past East Blue');
eq(requestedOffsets, [0,20,40,60], 'anime fetch stops once requested arc is covered');


const paceMeta = { poster: 'poster', background: 'background', videos: [
    {season:3,episode:1,id:'SY_1',title:'Captain Usopp'},
    {season:3,episode:7,id:'SY_7',title:'To the Sea'},
    {season:4,episode:1,id:'GA_1',title:"You're the Rare Breed"},
    {season:5,episode:1,id:'BA_1',title:'Enter: Sanji'},
    {season:6,episode:11,id:'BUGGYS_CREW_1',title:'Cover Story'}
] };
eq(api.selectOnePaceEpisodes(paceMeta, {onePacePrefixes:['SY_','GA_']}).map(v => v.id),
   ['SY_1','SY_7','GA_1'], 'One Pace follows Story Arcs prefixes across addon seasons');
eq(api.selectOnePaceEpisodes(paceMeta, {onePacePrefixes:['AR_']}).map(v => v.id),
   [], 'One Pace excludes unrelated cover-story ids');
let paceRows = null, paceInstalled = null, paceUrl = '';
api.setRequestAdapter((url, done) => { paceUrl = url; done({meta: paceMeta}); });
api.loadOnePaceEpisodes([{id:'com.onepace.fedew',enabled:true,transportUrl:'https://pace.example/manifest.json'}],
    {onePacePrefixes:['SY_','GA_']}, (rows, installed) => { paceRows=rows; paceInstalled=installed; });
eq(paceUrl, 'https://pace.example/meta/series/pp_onepace.json', 'One Pace uses installed extension transport');
eq(paceInstalled, true, 'One Pace reports installed extension');
eq(paceRows.map(v => v.id), ['SY_1','SY_7','GA_1'], 'One Pace catalogue rows come from installed addon meta');
let missingInstalled = true;
api.loadOnePaceEpisodes([], {onePacePrefixes:['RO_']}, (rows, installed) => { missingInstalled=installed; });
eq(missingInstalled, false, 'One Pace remains extension-gated when not installed');

const volumes = [{number:'8',cover:'a'},{number:'9',cover:'b'},{number:'12',cover:'c'}];
eq(api.selectVolumes(volumes, {volumes:'8-9'}).map(v => v.number), ['8','9'], 'arc volume membership');

process.exit(failed ? 1 : 0);
