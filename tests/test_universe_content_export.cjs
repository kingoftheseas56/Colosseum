const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { documents, envelope, exportTo } = require('../scripts/export_universe_content.cjs');
const source = documents();
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'colosseum-universe-export-'));
try {
  const keys = exportTo(directory);
  assert.equal(keys.length, 10);
  for (const key of keys) {
    const read = JSON.parse(fs.readFileSync(path.join(directory, key + '.json'), 'utf8'));
    assert.equal(read.schemaVersion, 1);
    assert.deepEqual(read.data, source[key], key + ' must preserve every curated field');
    assert.equal(read.revision, envelope(read.data).revision);
    assert.notEqual(envelope({ ...read.data, changed: true }).revision, read.revision);
  }
  const roster = source['universes-roster'];
  assert.ok(roster.universes.length + roster.archive.length >= 20);
  const onePiece = roster.universes.concat(roster.archive).find(u => u.name === 'One Piece');
  assert.equal(onePiece.anime.id, 'tt0388629');
  for (const sea of ['east-blue', 'paradise', 'new-world', 'final-sea']) {
    const value = source['tribute-one-piece-' + sea];
    assert.ok(value.story.arcs.length, sea + ' has story records');
    assert.ok(value.atlas.canonMarkers.length, sea + ' has atlas records');
    assert.equal(new Set(value.story.arcs.map(a => a.id)).size, value.story.arcs.length);
  }
  assert.ok(source['tribute-star-wars'].destinations.length);
  assert.equal(source['tribute-star-wars'].skywalkerDestination.id, 'skywalker');
  console.log('PASS: 10 content documents round-trip every field, stable revisions, identity and arc completeness');
} finally {
  for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
  fs.rmdirSync(directory);
}
