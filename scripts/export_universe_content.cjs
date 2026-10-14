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
  return JSON.parse(JSON.stringify(data));
}
function documents() {
  const roster = readModule('Universes.js');
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
  return JSON.parse(JSON.stringify(out));
}
function envelope(data) {
  const revision = crypto.createHash('sha256').update(JSON.stringify(data)).digest('hex');
  return { schemaVersion: 1, revision, data };
}
function exportTo(destination) {
  fs.mkdirSync(destination, { recursive: true });
  const all = documents();
  for (const [key, data] of Object.entries(all)) {
    fs.writeFileSync(path.join(destination, key + '.json'), JSON.stringify(envelope(data), null, 2) + '\n');
  }
  return Object.keys(all);
}
if (require.main === module) {
  const destination = process.argv[2];
  if (!destination) throw new Error('Usage: node scripts/export_universe_content.cjs OUTPUT_DIRECTORY');
  console.log(JSON.stringify({ documents: exportTo(path.resolve(destination)) }));
}
module.exports = { readModule, documents, envelope, exportTo };
