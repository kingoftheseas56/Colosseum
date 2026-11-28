const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { documents } = require('../scripts/export_universe_content.cjs');
function load(file, imports = {}) {
  const context = { console, ...imports };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../qml', file), 'utf8')
    .replace(/^\.(?:pragma|import).*$/gm, ''), context, { filename: file });
  return context;
}
const bridge = load('UniverseContentApi.js');
let content = documents();
bridge.setAssetResolver(value => value.replace('../assets/', 'https://content.example/assets/'));
bridge.setReader(key => content[key] || {});
const roster = load('Universes.js', { ContentApi: bridge });
assert.deepEqual(JSON.parse(JSON.stringify(roster.rosterFacets(roster.worldRoster([])))).map(f => f.label), ['Books','Manga','Anime','Films','Series']);
const canonicalCounts = { books: 7, manga: 5, anime: 5, films: 9, series: 9 };
for (const [key, count] of Object.entries(canonicalCounts)) assert.equal(roster.filterRoster(roster.worldRoster([]), key).length, count);
for (const [key, label] of Object.entries({ books: 'Novels', manga: 'Manga', anime: 'Anime', films: 'Movies', series: 'Shows', comics: 'Graphic novels' }))
  assert.equal(roster.holds({ chips: [{ t: label }] }, key), true, 'serialized RegExp fallback for ' + key);
for (const facet of content['universes-roster'].FACETS) assert.equal(typeof facet.match.source, 'string', 'RegExp exports as a pattern');
for (const facet of content['universes-roster'].FACETS) facet.match = {};
bridge.reload();
for (const [key, label] of Object.entries({ books: 'Novels', manga: 'Manga', anime: 'Anime', films: 'Movies', series: 'Shows', comics: 'Graphic novels' }))
  assert.equal(roster.holds({ chips: [{ t: label }] }, key), true, 'legacy cached RegExp fallback for ' + key);
assert.equal(roster.configFor('One Piece').anime.id, 'tt0388629');
const east = load('OnePieceEastBlueData.js', { ContentApi: bridge });
const atlas = load('OnePieceEastBlueAtlasData.js', { ContentApi: bridge });
const paradise = load('OnePieceParadiseAdapter.js', { ContentApi: bridge });
assert.ok(atlas.badgeSource('romance').startsWith('https://content.example/'));
assert.ok(paradise.artForArc({ id: 'amazon-lily' }).badgeSource.endsWith('/impel-down.png'));
content['tribute-one-piece-east-blue'].story.arcs[0].summary = 'A refreshed API story';
content['tribute-one-piece-east-blue'].artByArc.romance.posterSource = 'https://content.example/revision-2.png';
content['universes-roster'].universes.find(u => u.name === 'One Piece').blurb = 'Refreshed roster';
bridge.reload();
assert.equal(east.arc('romance').summary, 'A refreshed API story');
assert.equal(atlas.bannerSource('romance'), 'https://content.example/revision-2.png');
assert.equal(roster.configFor('One Piece').blurb, 'Refreshed roster');
assert.equal(roster.configFor('One Piece').anime.id, 'tt0388629');
content = {};
bridge.reload();
assert.equal(east.arc('romance').summary, 'A refreshed API story', 'missing remote document preserves last known data');
const extension = load('UniverseExtApi.js', { ContentApi: bridge });
extension.setReader(() => JSON.stringify({ universe: { title: 'Starter', sections: [{ title: 'Films', kind: 'video', entries: [{ title: 'Film', id: 'tt0814243', type: 'movie' }] }] } }));
let payload;
extension.load('com.colosseum.universe.onepiece', value => payload = value);
assert.equal(payload.title, 'Starter');
content['universe-one-piece'] = { universe: { title: 'API revision', sections: [{ title: 'Films', kind: 'video', entries: [{ title: 'Film', id: 'tt0814243', type: 'movie' }] }] } };
bridge.reload();
extension.load('com.colosseum.universe.onepiece', value => payload = value);
assert.equal(payload.title, 'API revision', 'process lifetime extension cache must invalidate');

const audio = load('UniverseAudio.js', { ContentApi: bridge });
assert.equal(audio.shelvesFor('Dune').length, 0);
const source = (provider, identity) => ({ provider, identity, sourceId: identity, checkedOn: '2026-11-28' });
content['audio-dune'] = {
  audiobooks: [
    { id: 'first', englishTitle: 'Dune', language: 'en', workId: 'audio-work', recordingId: 'edition-a',
      narrators: ['Narrator A'], availability: source('audiobookbay', 'edition-a') },
    { id: 'second', englishTitle: 'Dune', language: 'English', workId: 'audio-work', recordingId: 'edition-b',
      narrators: ['Narrator B'], availability: { ...source('audiobookbay', 'edition-b'),
        sourceId: 'https://audiobookbay.lu/abss/dune-edition-b/' } },
    { id: 'wrong-edition', englishTitle: 'Dune', language: 'en', workId: 'audio-work', recordingId: 'edition-c',
      availability: source('audiobookbay', 'edition-a') },
    { id: 'japanese', englishTitle: 'Dune', language: 'ja', workId: 'audio-work', recordingId: 'edition-ja',
      availability: source('audiobookbay', 'edition-ja') },
    { id: 'unverified-domain', englishTitle: 'Dune', language: 'en', workId: 'audio-work', recordingId: 'edition-c',
      availability: { ...source('audiobookbay', 'edition-c'), sourceId: 'https://audiobookbay.lu.example/abss/dune/' } },
    { id: 'unmatched', title: 'Dune', listenUrl: 'https://example.com/catalogue' }
  ],
  soundtrackReleases: [{
    id: 'score', englishTitle: 'Original Soundtrack', screenWorkEnglishTitle: 'Dune', screenWorkId: 'tt1160419',
    youtubeMusicBrowseId: 'MPREb_x', availability: source('youtube-music', 'MPREb_x'),
    tracks: [{ englishTitle: 'Score track', youtubeVideoId: '123456789ab', availability: source('youtube-music', '123456789ab') }]
  }, {
    id: 'playlist-only', englishTitle: 'Unmatched score', screenWorkId: 'tt1160419',
    youtubePlaylistId: 'PL123456789a', listenUrl: 'https://example.com/official'
  }, {
    id: 'restricted', englishTitle: 'Restricted soundtrack', screenWorkId: 'tt1160419',
    youtubeMusicBrowseId: 'MPREb_restricted', availability: {
      ...source('youtube-music', 'MPREb_restricted'), status: 'restricted-catalogue-metadata', region: 'IN'
    },
    tracks: [{ title: 'Official track credit', availableIn: false }]
  }],
  themes: [{
    id: 'theme', englishTitle: 'Theme song', role: 'opening', screenWorkId: 'tt1160419',
    versions: [{ edit: 'Full', youtubeVideoId: 'abcdefghijk', availability: source('youtube-music', 'abcdefghijk') },
      { edit: 'TV size', youtubeVideoId: '123456789ab', availability: source('youtube-music', '123456789ab') },
      { edit: 'Draft', youtubeVideoId: '11111111111' }]
  }]
};
const [books, music] = audio.shelvesFor('Dune');
assert.equal(books.title, 'Audiobooks');
assert.equal(books.runs[0].works[0].recordingId, 'edition-a');
assert.equal(books.runs[0].works[1].recordingId, 'edition-b');
assert.equal(books.runs[0].works.length, 2);
assert.equal(books.runs[0].works[0].listenUrl, undefined);
assert.equal(music.title, 'Music');
assert.equal(audio.shelvesFor('Dune', false).length, 1);
assert.equal(music.runs.length, 2);
const album = music.runs[0].works[0];
const [full, tv] = music.runs[1].works;
assert.equal(music.runs[0].works.length, 1);
assert.equal(music.runs[1].works.length, 2);
assert.equal(album.tracks[0].listenUrl, 'https://www.youtube.com/watch?v=123456789ab');
assert.equal(audio.vinylItem(album).destination.providerId, 'MPREb_x');
assert.equal(audio.vinylItem(full).record.tracks[0].videoId, 'abcdefghijk');
const webMusic = audio.audioShelves(content['audio-dune'], true)[1];
const restricted = webMusic.runs.find(run => run.label === 'Restricted soundtracks').works[0];
assert.equal(restricted.status, 'Playback unavailable in India');
assert.equal(restricted.listenUrl, '');
assert.equal(restricted.tracks[0].title, 'Official track credit');
assert.equal(audio.vinylItem(restricted), null);
assert.equal(tv.title, 'Theme song (TV size)');
assert.notEqual(full.key, tv.key);
assert.equal(audio.slugFor('Weekly Shōnen Jump'), 'weekly-shonen-jump');

const main = fs.readFileSync(path.join(__dirname, '../qml/Main.qml'), 'utf8');
const queuedCalls = [];
const activated = [];
const shell = {
  UniverseAudio: audio,
  Qt: { callLater: fn => queuedCalls.push(fn) },
  vinylEnabled: true,
  vinylProfileActive: true,
  vinylShellGeneration: 4,
  pendingUniverseVinyl: null,
  pendingVinylResume: null,
  vinylWorld: null,
  worldStack: { current: 'Universes' },
  guardNamedCollectionsNavigation: () => false,
  closeUniverse() {},
  openAudiobookBridge() {}
};
shell.win = shell;
vm.createContext(shell);
for (const name of ['openUniverseAudio', 'hasPendingUniverseVinyl', 'applyUniverseVinyl']) {
  const declaration = main.match(new RegExp('^    function ' + name + '\\([^\\n]*\\n[\\s\\S]*?^    }', 'm'));
  assert.ok(declaration, 'missing Main route ' + name);
  vm.runInContext(declaration[0], shell);
}
shell.openWorld = medium => {
  assert.equal(shell.hasPendingUniverseVinyl(), true, 'cold loader sees the destination before Home initialization');
  shell.worldStack.current = medium;
};
shell.openUniverseAudio(full);
queuedCalls.shift()();
assert.equal(activated.length, 0, 'pending route survives the asynchronous loader');
shell.vinylWorld = {
  hostReady: true, canEnterVinyl: false, resumeRequiresArtistSelection: true,
  enforceTasteGate() {},
  activateItem: item => activated.push(item)
};
shell.applyUniverseVinyl();
assert.equal(shell.hasPendingUniverseVinyl(), true, 'onboarding preserves the selected song');
shell.vinylWorld.canEnterVinyl = true;
shell.applyUniverseVinyl();
shell.applyUniverseVinyl();
assert.equal(activated.length, 1, 'readiness only opens the exact destination once');
assert.equal(activated[0].destination.providerId, 'abcdefghijk');
assert.equal(shell.hasPendingUniverseVinyl(), false);
console.log('PASS: in-process roster/story/art refresh, exact identity, offline retained values and extension cache invalidation');
