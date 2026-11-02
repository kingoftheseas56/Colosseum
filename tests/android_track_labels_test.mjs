// PF-07: readable Media3 subtitle titles and track/MIME codec labels from the production
// PlayerPage/SubtitleMenu helpers; ids, urls and selection fields stay untouched.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';

const read = n => readFileSync(new URL(`../qml/${n}`, import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const player = read('PlayerPage.qml');
const menu = read('SubtitleMenu.qml');
const subtitles = vm.createContext({});
vm.runInContext(read('Subtitles.js').replace(/^\.(pragma|import).*$/gm, ''), subtitles);
function fn(source, name) {
    const m = new RegExp(`^    function ${name}\\(([^)]*)\\)\\s*\\{([\\s\\S]*?)^    \\}`, 'm').exec(source);
    assert.ok(m, name);
    return m;
}
const root = { trackTech: () => 'tech' };
for (const name of ['subtitleDisplayTitle', 'subtitleRow', 'trackCodecLabel']) {
    const m = fn(player, name);
    root[name] = new Function('root', 'Subtitles', ...m[1].split(',').map(s => s.trim()).filter(Boolean), m[2])
        .bind(null, root, subtitles);
}
const menuObj = {};
for (const name of ['codecLabel', 'rowMeta']) {
    const m = fn(menu, name);
    menuObj[name] = new Function('menu', ...m[1].split(',').map(s => s.trim()).filter(Boolean), m[2]).bind(null, menuObj);
}

test('generic Media3/OpenSubtitles titles take the language name', () => {
    assert.equal(root.subtitleDisplayTitle('Subtitle', 'fr'), subtitles.langName('fr'));
    assert.equal(root.subtitleDisplayTitle('OpenSubtitles v3 #1234', 'es'), subtitles.langName('es'));
    assert.equal(root.subtitleDisplayTitle('Track 2', 'de'), subtitles.langName('de'));
    assert.equal(root.subtitleDisplayTitle('Commentary', 'en'), 'Commentary');
    assert.equal(root.subtitleDisplayTitle('Subtitle', ''), 'Subtitle');
    assert.equal(root.subtitleDisplayTitle('', ''), 'Subtitle');
    assert.notEqual(subtitles.langName('fr').toLowerCase(), 'fr');
});

test('subtitle rows keep identity fields', () => {
    const row = root.subtitleRow({ id: 7, title: 'Subtitle', lang: 'ja', url: 'file:///x.srt', selected: true, codec: 'application/x-subrip' });
    assert.equal(row.id, '7');
    assert.equal(row.url, 'file:///x.srt');
    assert.equal(row.selected, true);
    assert.equal(row.codec, 'application/x-subrip', 'raw codec stays in the row');
    assert.equal(row.label, subtitles.langName('ja'));
});

test('MIME codecs become readable; media3-cues hidden; mpv names unchanged', () => {
    const c = codec => root.trackCodecLabel({ codec });
    assert.equal(c('application/x-subrip'), 'SRT');
    assert.equal(c('audio/mp4a-latm'), 'AAC');
    assert.equal(c('audio/eac3-joc'), 'EAC3');
    assert.equal(c('audio/vnd.dts'), 'DTS');
    assert.equal(c('application/x-media3-cues'), '');
    assert.equal(c('text/vtt'), 'VTT');
    assert.equal(c('audio/opus'), 'OPUS');
    assert.equal(c('subrip'), 'SUBRIP', 'desktop mpv codec names keep their previous label');
    assert.equal(c(''), '');
    assert.equal(menuObj.codecLabel('application/x-media3-cues'), '');
    assert.equal(menuObj.codecLabel('application/x-subrip'), 'SRT');
    assert.equal(menuObj.rowMeta({ lang: 'en', codec: 'text/vtt', external: true }), 'EN · EXTERNAL · VTT');
    assert.equal(menuObj.rowMeta({ lang: 'en', codec: 'application/x-media3-cues' }), 'EN · EMBEDDED');
});
