const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { documents, envelope, exportTo } = require('../scripts/export_universe_content.cjs');
const source = documents();
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'colosseum-universe-export-'));
try {
  const keys = exportTo(directory);
  assert.deepEqual(keys, Object.keys(source));
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
  const audioDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'colosseum-universe-audio-'));
  try {
    const fixture = path.join(audioDirectory, 'dune.json');
    const audiobook = { id: 'dune-edition', title: 'Dune', workId: 'exact-work', recordingId: 'exact-recording' };
    const input = { schemaVersion: 1, universeSlug: 'dune',
      audiobooks: [{ ...audiobook, sources: [{ url: 'https://example.com/research' }] }],
      soundtrackReleases: [], themes: [], coverage: [] };
    fs.writeFileSync(fixture, JSON.stringify(input));
    assert.deepEqual(documents(audioDirectory)['audio-dune'], {
      audiobooks: [audiobook], soundtrackReleases: [], themes: []
    });
    input.audiobooks.push(audiobook);
    fs.writeFileSync(fixture, JSON.stringify(input));
    assert.throws(() => documents(audioDirectory), /Invalid Universe audio catalogue entries/);
  } finally {
    fs.rmSync(audioDirectory, { recursive: true });
  }
  console.log('PASS: Universe document round-trip, stable revisions, curated identities and optional audio export');
} finally {
  for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
  fs.rmdirSync(directory);
}
