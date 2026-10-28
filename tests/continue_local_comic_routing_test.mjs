import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Execute the shipped routing functions, rather than a reimplementation.
// Optional source argument supports an isolated negative control without editing production.
const source = readFileSync(process.argv[2] || new URL('../qml/Main.qml', import.meta.url), 'utf8');
const names = ['resumeContinue', 'openComicSession', 'openVaultComic'];
const functions = names.map(name => {
    const match = source.match(new RegExp('    function ' + name + '\\([^]*?\\n    }'));
    assert.ok(match, `Production function ${name} exists`);
    return match[0];
}).join('\n');
const calls = [];
const context = {
    Sessions: { openOrSwitch: value => calls.push(JSON.parse(JSON.stringify(value))) },
    VideoPresentation: { forEntry: () => ({}) }
};
context.win = context;
runInNewContext(functions, context);
function resume(entry) {
    calls.length = 0;
    const before = JSON.stringify(entry);
    context.resumeContinue(entry);
    assert.equal(JSON.stringify(entry), before, 'Continue must not mutate persisted progress');
    return calls;
}
let count = 0;
for (const kind of ['comic', 'manga']) {
    for (const id of ['vault:8697cea015efd85af0a150836a7d1e4096eca12f', 'vault-file:/tmp/A comic.cbz']) {
        const path = '/tmp/A comic.cbz';
        const result = resume({ kind, id, title: 'Local comic', resume: { chapterId: path, page: 3, maxSeen: 4 } });
        assert.equal(result.length, 1);
        assert.deepEqual(result[0], {
            appType: 'tankoban', contentKind: 'comic', title: 'Local comic',
            target: { vaultPath: path, id, title: 'Local comic' }
        }, 'A local Continue must restore the canonical Vault reader target, never a provider series');
        count++;
    }
}
for (const kind of ['comic', 'manga', 'tankoban']) {
    const result = resume({ kind, id: 'provider:series-1', title: 'Series', resume: { chapterId: '/not-proof-of-local-ownership' } });
    assert.deepEqual(result[0].target, { title: 'Series', seriesId: 'provider:series-1', chapterId: '/not-proof-of-local-ownership', entryKind: kind === 'tankoban' ? 'tankoban' : '' });
    count++;
}
assert.equal(resume({ kind: 'comic', id: 'vault:missing-path', resume: {} }).length, 0,
    'A local record without its path must not open a provider reader');
count++;
assert.equal(resume(null).length, 0);
count++;
// QML property notifications are synchronous: changing archivePath opens the
// reader immediately. Execute the shipped host callbacks and the shipped resume
// function to verify identity exists before that first load consumes resume.
const shellSource = readFileSync(new URL('../qml/comicreader/ComicReaderShell.qml', import.meta.url), 'utf8');
const applyResume = shellSource.match(/    function _applyResume\([^]*?\n    }/)[0];
const loaderSource = source.slice(source.indexOf('id: vaultComicLayer'));
const onLoaded = loaderSource.match(/        onLoaded: \{([^]*?)\n        }/)[1];
const activateSession = source.match(/    function activateSession\([^]*?\n    }/)[0];
for (const initialLoad of [true, false]) {
    const path = '/tmp/A comic.cbz';
    const id = 'vault:8697cea015efd85af0a150836a7d1e4096eca12f';
    const item = { vaultId: '', title: '', minimizeRequested: { connect() {} },
        closeRequested: { connect() {} }, backRequested: { connect() {} },
        fullscreenRequested: { connect() {} } };
    const loaded = { _resumeArmed: true, curChapterId: '', pageCount: 4,
        currentPage: 1, maxSeen: 0, mode: 'single', progressKind: 'comic',
        progress: { get: (kind, identity) => kind === 'comic' && identity === id
            ? { resume: { chapterId: path, page: 3, maxSeen: 3 } } : null },
        item, vaultComicLayer: { archivePath: path, vaultId: id, title: 'Local', active: true, item },
        wallpaperWorldForSession: () => 'Vault', refreshWallpaper() {} };
    Object.defineProperty(loaded, 'seriesId', { get: () => item.vaultId });
    loaded.win = loaded;
    runInNewContext(applyResume, loaded);
    Object.defineProperty(item, 'archivePath', { set(value) {
        loaded.curChapterId = value;
        loaded._applyResume();
    } });
    if (initialLoad) runInNewContext(onLoaded, loaded);
    else {
        runInNewContext(activateSession, loaded);
        loaded.activateSession({ id: 'session-local', contentKind: 'comic',
            target: { vaultPath: path, id, title: 'Local' } });
    }
    assert.equal(loaded.currentPage, 3,
        `${initialLoad ? 'Initial load' : 'Active replacement'} must set Vault identity before archivePath triggers resume`);
    count++;
}
console.log(`PASS ${count} production Continue routing and initialization cases`);
