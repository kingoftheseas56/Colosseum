import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const load = (file, scope = {}) => {
    if (fs.existsSync(file)) {
        vm.runInNewContext(fs.readFileSync(file, 'utf8').replace(/^\.(pragma|import)\b.*$/gm, ''), scope);
    }
    return scope;
};
const Biblio = load('qml/BiblioApi.js');
const FlowData = load('qml/AudiobookFlowData.js', { Biblio });
const bridge = load('qml/AudiobookBridgeData.js', { Biblio, FlowData });
const ebook = { title: 'The Way of Kings', author: 'Brandon Sanderson' };
const catalogueWork = { id: 'the-way-of-kings', title: ebook.title, authors: [{ name: ebook.author }],
    recordings: [{ id: 'kramer', narrators: [{ name: 'Michael Kramer' }] }] };
const pairKey = Biblio.pairKey(ebook.title, ebook.author);
const record = { recordingKey: pairKey + '#as:kramer', pairKey, workId: catalogueWork.id };
const catalog = {
    search: () => [{ id: 'different', title: ebook.title }, { id: catalogueWork.id, title: ebook.title }],
    work: id => id === catalogueWork.id ? catalogueWork : { ...catalogueWork, id, authors: ['Other Author'] }
};
const downloader = (rows, key = '') => ({
    recordingsForWork: pair => pair === pairKey ? rows : [],
    resolveRecordingKey: () => key,
    isDownloaded: value => rows.some(row => row.recordingKey === value && !row.missing)
});

test('catalogue lookup matches the author, and surfaces the chosen catalogue credits', () => {
    const result = bridge.describe(catalog, downloader([]), ebook);
    assert.equal(result.workId, catalogueWork.id);
    assert.equal(result.narrators, 'Michael Kramer');
    assert.equal(result.recordingKey, '');
});

test('ambiguous title and authors never choose an unrelated audiobook', () => {
    const ambiguous = { ...catalog, work: id => ({ ...catalogueWork, id }) };
    assert.equal(bridge.describe(ambiguous, downloader([]), ebook).workId, '');
    assert.equal(bridge.describe(catalog, downloader([]), { title: ebook.title }).workId, '');
});

test('local legacy playback remains available, ambiguous downloads wait for a choice', () => {
    const legacy = { recordingKey: pairKey, pairKey };
    assert.equal(bridge.describe(null, downloader([legacy], pairKey), ebook).recordingKey, pairKey);
    const other = { ...record, recordingKey: pairKey + '#as:other' };
    const result = bridge.describe(catalog, downloader([record, other]), ebook);
    assert.equal(result.recordings.length, 2);
    assert.equal(result.recordingKey, '');
});

test('the choice list surfaces explicit part numbers from saved source metadata only', () => {
    const part = { ...record, recordingKey: pairKey + '#src:part', sourceRef: 'way-of-kings-part-2' };
    const unlabeled = { ...record, sourceRef: 'way-of-kings-17' };
    const result = bridge.describe(catalog, downloader([unlabeled, part]), ebook);
    assert.equal(result.recordings[0].partNumber, '');
    assert.equal(result.recordings[1].partNumber, '2');
});

test('work or edition identity never admits a bundle, missing file, or conflicting work', () => {
    const key = record.recordingKey;
    for (const row of [{ ...record, bundle: true }, { ...record, missing: true },
                       { ...record, workId: 'another-work' }]) {
        const result = bridge.describe(catalog, downloader([row], key), ebook);
        assert.equal(result.recordingKey, '');
        assert.equal(result.recordings.length, 0);
    }
});

test('read-along requires an owned matching ebook and keeps its original path', () => {
    const local = { ...ebook, id: 'download', path: 'books/Way of Kings.epub' };
    const audio = { ...ebook, pairKey, workId: catalogueWork.id };
    assert.equal(bridge.ownedEbook(audio, [{ ...local, author: 'Other' }, { ...local, missing: true }]), null);
    assert.equal(bridge.ownedEbook(audio, [local]).path, local.path);
    assert.equal(bridge.ownedEbook({ ...audio, bundle: true }, [local]), null);
});

test('embedded chapter rows keep the positions exposed by the native player', () => {
    const source = fs.readFileSync('qml/AudiobookSession.qml', 'utf8');
    const [method] = source.match(/function rebuildChapters\(\) \{[\s\S]+?\n    \}/);
    const session = { files: ['recording.m4b'] };
    const mpv = { chapters: [{ title: 'Prologue', startSec: 0 },
        { title: 'Stormblessed', startSec: 40 }, { title: 'Honor', startSec: 80 }] };
    vm.runInNewContext(method + '\nrebuildChapters()', { session, mpv });
    assert.deepEqual(Array.from(session.chapterModel, row => row.time), [0, 40, 80]);
});
