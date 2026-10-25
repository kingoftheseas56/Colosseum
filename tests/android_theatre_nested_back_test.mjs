// Executes the real nested Back handlers from SourcesSheet and both Theatre
// detail variants. Only focus/UI effects are stubbed; state ownership stays in
// the production function bodies.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const root = new URL('../', import.meta.url);
const sourcesText = readFileSync(new URL('qml/SourcesSheet.qml', root), 'utf8').replace(/\r\n/g, '\n');
const seriesFiles = ['TheatreSeries.qml', 'TheatreSeriesView.qml'];
const seriesSources = Object.fromEntries(seriesFiles.map(name => [
    name, readFileSync(new URL(`qml/${name}`, root), 'utf8').replace(/\r\n/g, '\n'),
]));

function extractFunction(source, name) {
    const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const match = new RegExp(`^    function ${escaped}\\s*\\([^\\n]*\\)\\s*\\{([\\s\\S]*?)^    \\}`, 'm').exec(source);
    return match?.[1] || null;
}

function sourceEscapeFixture(overrides = {}) {
    const events = [];
    const sheet = {
        open: false,
        sourceContextOpen: false,
        extMenuOpen: false,
        ...overrides,
        closeSourceContext(restore) {
            events.push(['closeSourceContext', restore]);
            this.sourceContextOpen = false;
        },
        closeExtMenu(restore) {
            events.push(['closeExtMenu', restore]);
            this.extMenuOpen = false;
        },
        hide() {
            events.push(['hideSheet']);
            this.open = false;
        },
    };
    const body = extractFunction(sourcesText, 'requestEscape');
    assert.ok(body, 'SourcesSheet.qml must expose requestEscape() for shell Back');
    const requestEscape = new Function('sheet', 'Qt', body);
    const invoke = () => requestEscape(sheet, { callLater: fn => fn(), PopupFocusReason: 0 });
    return { sheet, events, invoke };
}

function pageEscapeFixture(source, pageFlags = {}, sourceFlags = {}) {
    const events = [];
    const sheetFixture = sourceEscapeFixture(sourceFlags);
    const page = {
        episodeJumpOpen: false,
        episodeContextOpen: false,
        seasonMenuOpen: false,
        ...pageFlags,
        closeEpisodeJump(restore) {
            events.push(['closeEpisodeJump', restore]);
            this.episodeJumpOpen = false;
        },
        closeEpisodeContext(restore) {
            events.push(['closeEpisodeContext', restore]);
            this.episodeContextOpen = false;
        },
        closeSeasonMenu(restore) {
            events.push(['closeSeasonMenu', restore]);
            this.seasonMenuOpen = false;
        },
    };
    const sources = {
        get open() { return sheetFixture.sheet.open; },
        requestEscape() {
            events.push(['sourcesRequestEscape']);
            const consumed = sheetFixture.invoke();
            events.push(...sheetFixture.events.splice(0));
            return consumed;
        },
    };
    const body = extractFunction(source, 'requestEscape');
    assert.ok(body, 'Theatre detail must expose requestEscape() for shell Back');
    const requestEscape = new Function('page', 'sources', 'Qt', body);
    const invoke = () => requestEscape(page, sources, { callLater: fn => fn(), PopupFocusReason: 0 });
    return { page, sheet: sheetFixture.sheet, events, invoke };
}

test('SourcesSheet consumes context/menu Back before hiding the sheet', () => {
    for (const [field, effect] of [
        ['sourceContextOpen', 'closeSourceContext'],
        ['extMenuOpen', 'closeExtMenu'],
    ]) {
        const fixture = sourceEscapeFixture({ open: true, [field]: true });
        assert.equal(fixture.invoke(), true, `${field} should consume Back`);
        assert.deepEqual(fixture.events, [[effect, true]]);
        assert.equal(fixture.sheet.open, true, `${field} dismissal must leave the sheet open`);
    }

    const sheet = sourceEscapeFixture({ open: true });
    assert.equal(sheet.invoke(), true, 'open sheet should consume Back');
    assert.equal(sheet.sheet.open, false, 'sheet dismissal should hide it');
    assert.equal(sheet.events[0][0], 'hideSheet');
    assert.equal(sheet.invoke(), false, 'no visible sheet transient should return false');
});

for (const [name, source] of Object.entries(seriesSources)) {
    test(`${name} gives nested sources and episode transients Back before closing detail`, () => {
        const nested = pageEscapeFixture(source, {}, { open: true, sourceContextOpen: true });
        assert.equal(nested.invoke(), true, 'source context should consume first Back');
        assert.deepEqual(nested.events, [['sourcesRequestEscape'], ['closeSourceContext', true]]);
        assert.equal(nested.sheet.open, true, 'source context dismissal must retain the sheet');

        nested.events.length = 0;
        assert.equal(nested.invoke(), true, 'open source sheet should consume the next Back');
        assert.deepEqual(nested.events, [['sourcesRequestEscape'], ['hideSheet']]);
        assert.equal(nested.sheet.open, false);

        for (const [field, effect] of [
            ['episodeJumpOpen', 'closeEpisodeJump'],
            ['episodeContextOpen', 'closeEpisodeContext'],
            ['seasonMenuOpen', 'closeSeasonMenu'],
        ]) {
            nested.page[field] = true;
            nested.events.length = 0;
            assert.equal(nested.invoke(), true, `${field} should consume Back`);
            assert.deepEqual(nested.events.filter(event => event[0] !== 'sourcesRequestEscape'), [[effect, true]]);
            assert.equal(nested.page[field], false);
        }

        nested.events.length = 0;
        assert.equal(nested.invoke(), false, 'no sheet or transient should defer to shell detail close');
        assert.deepEqual(nested.events, [['sourcesRequestEscape']]);

        // The episode context has z=80; the source sheet has z=60. A stale
        // underlying open sheet must not outrank the visible context menu.
        const contextAboveSheet = pageEscapeFixture(source, { episodeContextOpen: true }, { open: true });
        assert.equal(contextAboveSheet.invoke(), true);
        assert.deepEqual(contextAboveSheet.events, [['closeEpisodeContext', true]]);
        assert.equal(contextAboveSheet.sheet.open, true);
    });
}
