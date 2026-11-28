// Export the shipped curated identities as versioned API documents. No provider searches.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
function evaluateModule(file) {
  const source = fs.readFileSync(path.join(root, 'qml', file), 'utf8');
  const context = { ContentApi: { register() {}, document() { return {}; }, asset(value) { return value; } } };
  vm.createContext(context);
  vm.runInContext(source.replace(/^\.(?:pragma|import).*$/gm, ''), context, { filename: file });
  return context;
}
function readModule(file) {
  const context = evaluateModule(file);
  const data = {};
  for (const key of Object.keys(context)) {
    if (key !== 'ContentApi' && !key.startsWith('_') && typeof context[key] !== 'function') data[key] = context[key];
  }
  return JSON.parse(JSON.stringify(data, (_key, value) =>
    Object.prototype.toString.call(value) === '[object RegExp]'
      ? { source: value.source, flags: value.flags } : value));
}
function screenPins(node, pins = new Map()) {
  if (!node || typeof node !== 'object') return pins;
  const title = node.t || node.title;
  if (typeof title === 'string' && /^tt\d{7,}$/.test(node.id)) pins.set(node.id, title);
  for (const child of Object.values(node)) screenPins(child, pins);
  return pins;
}
function projectAudio(data, rules, pins) {
  function fields(row, keys) {
    return Object.fromEntries(keys.split(' ').filter(key => row[key] != null).map(key => [
      key, key === 'availability' ? fields(row[key], 'provider identity sourceId checkedOn status region matchScope') : row[key]
    ]));
  }
  function playableSongs(rows, keys, needsOwnTitle) {
    return Array.isArray(rows) ? rows
      .filter(row => row && (!needsOwnTitle || rules.englishTitle(row)) && rules.hasVideo(row))
      .map(row => fields(row, keys)) : [];
  }
  function pinnedScreenWork(row) {
    return rules.isScreenRecording(row) && pins.has(row.screenWorkId);
  }
  function soundtrackTracks(row) {
    const keys = 'title englishTitle artists disc trackNumber durationSeconds coverUrl youtubeVideoId availability availabilityStatus availableIn';
    if (!rules.hasAlbumIdentity(row)) return playableSongs(row.tracks, keys, true);
    return Array.isArray(row.tracks) ? row.tracks
      .filter(track => track && typeof track.title === 'string' && track.title.trim())
      .map(track => fields(track, keys)) : [];
  }
  return {
    audiobooks: data.audiobooks.filter(rules.isEnglishRecording).map(row => fields(row,
      'id title englishTitle bookTitle authors narrators workId recordingId audioSiloUrl language region publisher releaseDate durationMinutes abridgement dramatized coverUrl availability')),
    soundtrackReleases: data.soundtrackReleases.filter(pinnedScreenWork).map(row => ({
      ...fields(row, 'id title englishTitle screenWorkId season artists composers releaseDate label format edition coverUrl youtubeMusicBrowseId youtubeVideoId availability'),
      screenWorkEnglishTitle: pins.get(row.screenWorkId),
      tracks: soundtrackTracks(row)
    })).filter(row => rules.hasAlbumIdentity(row) || rules.hasVideo(row) || row.tracks.length),
    themes: data.themes.filter(row => pinnedScreenWork(row) && (row.role === 'opening' || row.role === 'ending')).map(row => ({
      ...fields(row, 'id title englishTitle screenWorkId season role number usedInEpisodes uses artists coverUrl youtubeVideoId availability'),
      screenWorkEnglishTitle: pins.get(row.screenWorkId),
      versions: playableSongs(row.versions, 'edit artists coverUrl youtubeVideoId availability', false)
    })).filter(row => rules.hasVideo(row) || row.versions.length)
  };
}
function documents(audioDirectory = path.join(root, 'assets/universe-audio'), roster = readModule('Universes.js'), screenCatalogue = {}) {
  const out = { 'universes-roster': roster };
  for (const sea of ['EastBlue', 'Paradise', 'NewWorld', 'FinalSea']) {
    const slug = sea.replace(/([a-z])([A-Z])/g, '$1-$2').toLowerCase();
    out['tribute-one-piece-' + slug] = {
      story: readModule('OnePiece' + sea + 'Data.js'),
      atlas: readModule('OnePiece' + sea + 'AtlasData.js')
    };
    const document = out['tribute-one-piece-' + slug];
    document.artByArc = {};
    if (sea === 'EastBlue') {
      const atlas = evaluateModule('OnePieceEastBlueAtlasData.js');
      for (const arc of document.story.arcs) {
        const marker = atlas.canonMarker(arc.id);
        document.artByArc[arc.id] = { markerId: arc.id, badgeSource: atlas.badgeSource(arc.id),
          posterSource: atlas.bannerSource(arc.id), fallbackSource: marker ? marker.poster : '' };
      }
    } else {
      const adapter = evaluateModule('OnePiece' + sea + 'Adapter.js');
      for (const arc of document.story.arcs) document.artByArc[arc.id] = adapter.artForArc(arc);
    }
  }
  out['tribute-dcau'] = readModule('DCAUUniverseData.js');
  const galaxySource = fs.readFileSync(path.join(root, 'qml/GalaxyUniversePage.qml'), 'utf8');
  const destinations = galaxySource.match(/readonly property var (?:starterDestinations|destinations): (\[[\s\S]*?\n    \])/);
  const skywalker = galaxySource.match(/readonly property var (?:starterSkywalkerDestination|skywalkerDestination): (\(\{[\s\S]*?\n    \}\))/);
  if (!destinations || !skywalker) throw new Error('Galaxy layout declarations changed; update exporter explicitly');
  out['tribute-star-wars'] = {
    destinations: vm.runInNewContext(destinations[1]),
    skywalkerDestination: vm.runInNewContext(skywalker[1])
  };
  for (const stem of ['one-piece', 'dcau', 'star-wars']) {
    out['universe-' + stem] = JSON.parse(fs.readFileSync(path.join(root, 'assets/universes', stem + '.json'), 'utf8'));
  }
  if (fs.existsSync(audioDirectory)) {
    const audioRules = evaluateModule('UniverseAudio.js');
    const entries = new Map(roster.universes.concat(roster.archive).map(entry => [audioRules.slugFor(entry.name), entry]));
    for (const file of fs.readdirSync(audioDirectory).filter(name => name.endsWith('.json')).sort()) {
      const slug = path.basename(file, '.json');
      const data = JSON.parse(fs.readFileSync(path.join(audioDirectory, file), 'utf8'));
      const entry = entries.get(slug);
      if (!entry || data?.schemaVersion !== 1 || data.universeSlug !== slug)
        throw new Error('Invalid Universe audio document: ' + file);
      if (Array.isArray(data.soundtrackReleases)) {
        data.soundtrackReleases = data.soundtrackReleases.map(row =>
          row && !row.id && typeof row.releaseId === 'string' ? { ...row, id: row.releaseId } : row);
      }
      for (const key of ['audiobooks', 'soundtrackReleases', 'themes']) {
        const rows = data[key];
        if (!Array.isArray(rows) || rows.some(row => typeof row?.id !== 'string' || !row.id.trim()
            || typeof row.title !== 'string' || !row.title.trim())
            || new Set(rows.map(row => row.id)).size !== rows.length)
          throw new Error('Invalid Universe audio catalogue entries: ' + file + ' ' + key);
      }
      const pins = screenPins([screenCatalogue[slug], entry, roster.shelfOverlays?.[entry.name], out['universe-' + slug]]);
      const projected = projectAudio(data, audioRules, pins);
      if (Object.values(projected).some(rows => rows.length)) out['audio-' + slug] = projected;
    }
  }
  return JSON.parse(JSON.stringify(out));
}
function envelope(data) {
  const revision = crypto.createHash('sha256').update(JSON.stringify(data)).digest('hex');
  return { schemaVersion: 1, revision, data };
}
function exportTo(destination, audioDirectory, roster, screenCatalogue) {
  fs.mkdirSync(destination, { recursive: true });
  const all = documents(audioDirectory, roster, screenCatalogue);
  for (const [key, data] of Object.entries(all)) {
    const audio = key.startsWith('audio-');
    const text = JSON.stringify(envelope(data), null, audio ? undefined : 2) + '\n';
    if (audio && Buffer.byteLength(text) > 512 * 1024)
      throw new Error('Universe audio document exceeds the Worker size limit: ' + key);
    fs.writeFileSync(path.join(destination, key + '.json'), text);
  }
  return Object.keys(all);
}
if (require.main === module) {
  const destination = process.argv[2];
  if (!destination) throw new Error('Usage: node scripts/export_universe_content.cjs OUTPUT_DIRECTORY');
  console.log(JSON.stringify({ documents: exportTo(path.resolve(destination)) }));
}
module.exports = { readModule, documents, envelope, exportTo };
