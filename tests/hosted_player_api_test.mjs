// hosted_player_api_test.mjs — the trusted hosted-player provider contract.
//
// VidKing is a keyless hosted web player, not a torrent/direct/download source.
// This pins the three functions the Sources sheet and the player surface rely on:
//   rowsFor(hostedExtensions, media) — build trusted provider rows, and ONLY for a
//     valid positive-integer TMDB id (and, for series, positive season+episode).
//   embedUrl(providerId, media, resumeSeconds) — construct VidKing's DOCUMENTED
//     movie/tv embed URL from app-owned constants; "" for any unregistered provider.
//   normalizeEvent(raw) — defensive PLAYER_EVENT normalization; null for anything
//     malformed, so a hostile postMessage can never masquerade as a player event.
//
// Loads the real qml/HostedPlayerApi.js (a `.pragma library`) the same way the
// Torrentio-honesty harness loads AddonClient.js — no mocks, real source.
import fs from 'fs';

let src = fs.readFileSync('qml/HostedPlayerApi.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src +
  '\nmodule.rowsFor=rowsFor;' +
  '\nmodule.embedUrl=embedUrl;' +
  '\nmodule.normalizeEvent=normalizeEvent;')(mod);

let failures = 0;
function eq(actual, expected, msg) {
  const label = msg || (JSON.stringify(actual) + ' === ' + JSON.stringify(expected));
  if (actual === expected) { console.log('  ok   ' + label); }
  else { console.log('  FAIL ' + label + ' (got ' + JSON.stringify(actual) + ')'); failures++; }
}

const installed = [{
  id: 'net.vidking.player', enabled: true,
  manifest: { id: 'net.vidking.player', resources: ['hosted-player'], types: ['movie', 'series'], idPrefixes: ['tt'] }
}];

console.log('rowsFor — a hosted row only for an enabled extension and a valid TMDB id');

eq(mod.rowsFor(installed, { type: 'movie', imdbId: 'tt1375666', tmdbId: 27205 }).length, 1,
   'enabled VidKing + positive tmdbId -> one hosted row');
eq(mod.rowsFor([{ ...installed[0], enabled: false }], { type: 'movie', imdbId: 'tt1375666', tmdbId: 27205 }).length, 0,
   'disabled extension -> no hosted row');
eq(mod.rowsFor(installed, { type: 'movie', imdbId: 'tt1375666', tmdbId: 0 }).length, 0,
   'tmdbId 0 -> no hosted row (optimism still needs an id)');
eq(mod.rowsFor(installed, { type: 'series', imdbId: 'tt0903747', tmdbId: 1396, season: 0, episode: 3 }).length, 0,
   'series without a positive season -> no hosted row');
eq(mod.rowsFor(installed, { type: 'series', imdbId: 'tt0903747', tmdbId: 1396, season: 2, episode: 3 }).length, 1,
   'series with positive season+episode -> one hosted row');
eq(mod.rowsFor(null, { type: 'movie', tmdbId: 27205 }).length, 0, 'null list -> [], never throws');
eq(mod.rowsFor(installed, null).length, 0, 'null media -> [], never throws');

const movieRow = mod.rowsFor(installed, { type: 'movie', imdbId: 'tt1375666', tmdbId: 27205 })[0];
eq(movieRow.kind, 'hostedPlayer', 'row.kind is hostedPlayer');
eq(movieRow.providerId, 'vidking', 'row.providerId is vidking');
eq(movieRow.extensionId, 'net.vidking.player', 'row.extensionId is the VidKing extension id');
eq(movieRow.streamKind, 'Hosted', 'row.streamKind is Hosted');
eq(movieRow.streamLabel, 'Web player', 'row.streamLabel is Web player');
eq(movieRow.media.tmdbId, 27205, 'row.media.tmdbId is the integer id');

console.log('embedUrl — VidKing documented routes, whole-second progress, gold color');

eq(mod.embedUrl('vidking', { type: 'movie', tmdbId: 27205 }, 83),
   'https://www.vidking.net/embed/movie/27205?color=e8b923&autoPlay=true&progress=83',
   'movie embed URL');
eq(mod.embedUrl('vidking', { type: 'series', tmdbId: 1396, season: 2, episode: 3 }, 41),
   'https://www.vidking.net/embed/tv/1396/2/3?color=e8b923&autoPlay=true&nextEpisode=true&episodeSelector=true&progress=41',
   'series embed URL with nextEpisode + episodeSelector');
eq(mod.embedUrl('unknown-provider', { type: 'movie', tmdbId: 27205 }, 0), '',
   'unregistered provider -> "" (no arbitrary iframe URL)');
eq(mod.embedUrl('vidking', { type: 'movie', tmdbId: 0 }, 0), '',
   'movie without a valid tmdbId -> ""');
eq(mod.embedUrl('vidking', { type: 'series', tmdbId: 1396, season: 0, episode: 3 }, 0), '',
   'series without positive season -> ""');
eq(mod.embedUrl('vidking', { type: 'movie', tmdbId: 27205 }, 12.7),
   'https://www.vidking.net/embed/movie/27205?color=e8b923&autoPlay=true&progress=12',
   'progress floored to whole seconds');

console.log('normalizeEvent — only a well-formed VidKing PLAYER_EVENT survives');

eq(mod.normalizeEvent({ type: 'PLAYER_EVENT', data: { event: 'timeupdate', currentTime: 12, duration: 100 } }).event,
   'timeupdate', 'valid timeupdate normalizes');
eq(mod.normalizeEvent({ type: 'OTHER', data: {} }), null, 'non-PLAYER_EVENT -> null');
eq(mod.normalizeEvent({ type: 'PLAYER_EVENT', data: { event: 'timeupdate', currentTime: -1, duration: 100 } }), null,
   'negative currentTime -> null');
eq(mod.normalizeEvent({ type: 'PLAYER_EVENT', data: { event: 'hack', currentTime: 1, duration: 2 } }), null,
   'disallowed event name -> null');
eq(mod.normalizeEvent(null), null, 'null -> null');
eq(mod.normalizeEvent('not json'), null, 'unparseable string -> null');
eq(mod.normalizeEvent(JSON.stringify({ type: 'PLAYER_EVENT', data: { event: 'ended', currentTime: 100, duration: 100 } })).event,
   'ended', 'JSON string form is accepted');

if (failures) { console.log('\nFAIL — ' + failures + ' check(s) failed'); process.exit(1); }
console.log('\nPASS — hosted player provider contract holds');
