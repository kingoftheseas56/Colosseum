import fs from 'fs';

const root = process.argv[2] || '.';
const modelPath = root + '/qml/HistoryStatsModel.js';
let src = fs.readFileSync(modelPath, 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.monthKeys=monthKeys;module.aggregateStats=aggregateStats;module.historyRows=historyRows;module.highlightCards=highlightCards;')(mod);

function fail(message) {
    console.error('FAIL ' + message);
    process.exit(1);
}
function eq(actual, expected, message) {
    const left = JSON.stringify(actual);
    const right = JSON.stringify(expected);
    if (left !== right)
        fail(message + ' (got ' + left + ', want ' + right + ')');
}

eq(mod.monthKeys('2026-07', '2026-09'), ['2026-07', '2026-08', '2026-09'],
   'month range is inclusive and chronological');
eq(mod.monthKeys('', '2026-09'), ['2026-09'], 'missing history floor falls back to current month');

const july = {
    month: '2026-07', watchSeconds: 3600, listenSeconds: 600, pagesRead: 20,
    completedCount: 1, activeDays: 2,
    recentActivity: [
        { localDate: '2026-07-08', lastAtMs: 80, sessionId: 's1', world: 'theatre',
          kind: 'movie', titleKey: 'movie:dune', itemKey: 'movie:dune', title: 'Dune',
          cover: 'dune.jpg', watchSeconds: 3600, listenSeconds: 0, pagesRead: 0,
          progressMicros: 0, completed: true },
        { localDate: '2026-07-02', lastAtMs: 20, sessionId: 's2', world: 'tankoban',
          kind: 'manga_chapter', titleKey: 'manga:frieren', itemKey: 'ch:1', title: 'Frieren',
          cover: 'frieren.jpg', watchSeconds: 0, listenSeconds: 0, pagesRead: 20,
          progressMicros: 0, completed: false }
    ]
};
const august = {
    month: '2026-08', watchSeconds: 1800, listenSeconds: 1200, pagesRead: 30,
    completedCount: 2, activeDays: 3,
    recentActivity: [
        { localDate: '2026-08-19', lastAtMs: 190, sessionId: 's3', world: 'biblio',
          kind: 'audiobook', titleKey: 'book:hail-mary', itemKey: 'audio:hail-mary', title: 'Project Hail Mary',
          cover: 'hail.jpg', watchSeconds: 0, listenSeconds: 1200, pagesRead: 0,
          progressMicros: 0, completed: false },
        { localDate: '2026-08-12', lastAtMs: 120, sessionId: 's4', world: 'theatre',
          kind: 'movie', titleKey: 'movie:dune', itemKey: 'movie:dune', title: 'Dune',
          cover: 'dune.jpg', watchSeconds: 1800, listenSeconds: 0, pagesRead: 0,
          progressMicros: 0, completed: false },
        { localDate: '2026-08-03', lastAtMs: 30, sessionId: 's5', world: 'tankoban',
          kind: 'manga_chapter', titleKey: 'manga:frieren', itemKey: 'ch:2', title: 'Frieren',
          cover: 'frieren.jpg', watchSeconds: 0, listenSeconds: 0, pagesRead: 30,
          progressMicros: 0, completed: true }
    ]
};

const all = mod.aggregateStats([july, august], 'all', 'overview');
eq(all.summary.map(x => x.value), ['1h 30m', '50', '30m', '3', '5'],
   'all-world summary uses exact projector totals');
eq(all.rows[0].title, 'Dune', 'overview ranks most recently active title first');

const theatre = mod.aggregateStats([july, august], 'theatre', 'watch');
eq(theatre.summary.map(x => x.value), ['1h 30m', '0', '0m', '1', '2'],
   'world filter derives honest metrics from activity moments');
eq(theatre.rows.length, 1, 'world filter removes other worlds');
eq(theatre.rows[0].value, '1h 30m', 'watch ranking combines title sessions');

const episodeSeries = mod.aggregateStats([{ recentActivity: [
    { localDate: '2026-08-01', lastAtMs: 10, sessionId: 'ep1', world: 'theatre',
      kind: 'episode', titleKey: 'theatre:tt14688458', itemKey: 'tt14688458:3:6',
      title: 'Silo', watchSeconds: 1800 },
    { localDate: '2026-08-02', lastAtMs: 20, sessionId: 'ep2', world: 'theatre',
      kind: 'episode', titleKey: 'theatre:tt14688458', itemKey: 'tt14688458:3:7',
      title: 'Silo - S3E7', watchSeconds: 1800 }
] }], 'all', 'overview');
eq(episodeSeries.rows.length, 1, 'episodes aggregate into one show row');
eq(episodeSeries.rows[0].title, 'Silo',
   'an aggregated show row uses the show title instead of the latest episode label');
eq(episodeSeries.rows[0].value, '1h 0m', 'show row combines watch time across episodes');

const deliveries = [
    { title: 'Dune', providerKey: 'simkl', providerName: 'SIMKL', state: 'confirmed' },
    { title: 'Project Hail Mary', providerKey: 'trakt', providerName: 'Trakt', state: 'needs_attention' }
];
const history = mod.historyRows([july, august], 'all', deliveries);
eq(history.map(x => x.title), ['Project Hail Mary', 'Dune', 'Dune', 'Frieren', 'Frieren'],
   'history is newest first across months');
eq(history.map(x => x.syncLabel),
   ['Needs attention', 'On 1 tracker', 'On 1 tracker', 'Local only', 'Local only'],
   'history decorates exact title matches with safe delivery receipts');
eq(history[1].trackerReceipts,
   [{ providerKey: 'simkl', providerName: 'SIMKL', state: 'confirmed' }],
   'history exposes only presentation-safe receipt fields');
const collision = mod.historyRows([{ recentActivity: [
    { title: 'Dune', titleKey: 'theatre:dune-1984', world: 'theatre' },
    { title: 'Dune', titleKey: 'theatre:dune-2021', world: 'theatre' }
] }], 'all', deliveries);
eq(collision.map(x => x.syncLabel), ['Local only', 'Local only'],
   'ambiguous display titles never inherit a guessed tracker receipt');
eq(mod.historyRows([july, august], 'biblio').length, 1,
   'history world filter is applied');

const cards = mod.highlightCards({ highlights: [
    { role: 'theatre', title: 'Dune', cover: 'dune.jpg', watchSeconds: 5400, world: 'theatre' },
    { role: 'tankoban', title: 'Frieren', cover: 'frieren.jpg', pagesRead: 50, world: 'tankoban' }
] });
eq(cards.map(x => x.label), ['Most watched', 'Most read'], 'highlight roles retain product copy');
eq(cards.map(x => x.value), ['1h 30m', '50 pages'], 'highlight values use their native metric');

console.log('PASS history/highlights/stats model');
process.exit(0);
