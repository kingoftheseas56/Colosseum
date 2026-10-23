import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
const playerPage = readFileSync(join(repo, 'qml', 'PlayerPage.qml'), 'utf8');
const qml = playerPage.replace(/\r\n/g, '\n');

function matchingBrace(source, openAt, label) {
    let depth = 0;
    let mode = 'code';
    for (let i = openAt; i < source.length; i++) {
        const ch = source[i];
        const next = source[i + 1];
        if (mode === 'line-comment') {
            if (ch === '\n') mode = 'code';
            continue;
        }
        if (mode === 'block-comment') {
            if (ch === '*' && next === '/') {
                mode = 'code';
                i++;
            }
            continue;
        }
        if (mode === 'single' || mode === 'double' || mode === 'template') {
            if (ch === '\\') {
                i++;
                continue;
            }
            if ((mode === 'single' && ch === "'")
                    || (mode === 'double' && ch === '"')
                    || (mode === 'template' && ch.charCodeAt(0) === 96))
                mode = 'code';
            continue;
        }
        if (ch === '/' && next === '/') {
            mode = 'line-comment';
            i++;
            continue;
        }
        if (ch === '/' && next === '*') {
            mode = 'block-comment';
            i++;
            continue;
        }
        if (ch === "'") { mode = 'single'; continue; }
        if (ch === '"') { mode = 'double'; continue; }
        if (ch.charCodeAt(0) === 96) { mode = 'template'; continue; }
        if (ch === '{') depth++;
        else if (ch === '}' && --depth === 0) return i;
    }
    throw new Error('unbalanced braces while extracting ' + label);
}

function extractFunction(source, name) {
    const match = new RegExp('^[ \t]*function\\s+' + name + '\\s*\\([^)]*\\)\\s*\\{', 'm').exec(source);
    assert.ok(match, 'PlayerPage.qml must define ' + name);
    const openAt = source.indexOf('{', match.index);
    return source.slice(match.index, matchingBrace(source, openAt, name) + 1);
}

function extractBlock(source, pattern, label) {
    const match = pattern.exec(source);
    assert.ok(match, 'PlayerPage.qml must contain ' + label);
    const openAt = source.indexOf('{', match.index);
    const closeAt = matchingBrace(source, openAt, label);
    return source.slice(openAt + 1, closeAt);
}

function assertBefore(source, first, second, label) {
    const a = source.indexOf(first);
    const b = source.indexOf(second);
    assert.ok(a >= 0 && b > a, label + ' must call ' + first + ' before ' + second);
}

const fetchBody = extractFunction(qml, 'fetchSubtitles');
assertBefore(fetchBody, 'root.maybeAutoSelectTracks("online-subs")', 'root.maybeAutoSub()', 'subtitle fetch callback');
const fileLoadedBody = extractBlock(qml, /\bonFileLoaded\s*:\s*\{/, 'onFileLoaded handler');
assertBefore(fileLoadedBody, 'root.maybeAutoSelectTracks("file-loaded")', 'root.maybeAutoSub()', 'file-loaded handler');

function stripQmlDirectives(source) {
    return source.replace(/^\s*\.pragma library\s*$/gm, '').replace(/^\s*\.import\s+.*$/gm, '');
}

const qmlFunctionNames = [
    'fetchSubtitles', 'pickSubtitle', 'onlineSubtitleRow', 'subtitleRow',
    'maybeAutoSub', 'currentShowKey', 'currentTrackPreference', 'saveTrackPreference',
    'trackAutomationExcluded', 'trackPolicyKey', 'effectiveAudioLanguages',
    'effectiveSubtitleLanguages', 'selectedAudioRow', 'applySavedTrackDelays',
    'resetTrackAutomation', 'maybeAutoSelectTracks', 'pickSubtitleTrack',
    'turnSubtitlesOff'
];
const qmlFunctions = qmlFunctionNames.map(name => extractFunction(qml, name));
const subRowsBody = extractBlock(qml, /\breadonly\s+property\s+var\s+subRows\s*:\s*\{/, 'subRows property');

function makeHarness() {
    const context = vm.createContext({});
    const pending = [];
    const added = [];
    const root = {
        onlineSubs: [],
        addedOnlineUrls: {},
        autoSubDone: false,
        userTouchedSubs: false,
        userTouchedAudio: false,
        fileReady: false,
        subsLoading: false,
        subModelRev: 0,
        subStreamType: 'series',
        subStreamId: 'tt1234567:1:2',
        mediaId: 'tt1234567:1:2',
        currentPlaybackUrl: '',
        trackAutomationRev: 0,
        trackAutoDoneKey: '',
        autoAudioTrackId: '',
        autoSubtitleTrackId: '',
        autoTrackSignature: '',
        audioRows: [],
        trackTech: function() { return ''; },
        supportsPlayerCapability: function() { return false; }
    };
    const settings = {
        trackPrefsJson: '{}',
        preferredAudioLanguages: 'eng,jpn',
        preferredSubtitleLanguages: 'spa,eng',
        blockedTrackWords: '',
        preferEmbeddedSubtitles: false,
        subtitleAutoUpgrade: false,
        forcedSubsWhenNativeAudio: false,
        subtitlesOffByDefault: false
    };
    const player = {
        audioTracks: [],
        subtitleTracks: [],
        audioTrack: '',
        subtitleTrack: '',
        audioDelay: 0,
        subDelay: 0,
        addSubtitle: function(url, title, lang, select) {
            added.push({ url, title, lang, select });
        }
    };

    vm.runInContext(stripQmlDirectives(readFileSync(join(repo, 'qml', 'TrackLanguage.js'), 'utf8')), context,
        { filename: 'TrackLanguage.js' });
    vm.runInContext(stripQmlDirectives(readFileSync(join(repo, 'qml', 'PlayerTrackPrefs.js'), 'utf8')), context,
        { filename: 'PlayerTrackPrefs.js' });
    vm.runInContext(stripQmlDirectives(readFileSync(join(repo, 'qml', 'Subtitles.js'), 'utf8')), context,
        { filename: 'Subtitles.js' });
    context.root = root;
    context.mpv = player;
    context.playerSettings = settings;
    context.Subtitles = {
        fetch: function(type, id, done) { pending.push({ type, id, done }); },
        pickDefault: context.pickDefault
    };
    context.TrackLanguage = {
        normalizeLang: context.normalizeLang,
        parseLanguageList: context.parseLanguageList,
        showKey: context.showKey,
        trackSignature: context.trackSignature,
        pickBestAudioTrack: context.pickBestAudioTrack,
        pickBestSubtitleTrack: context.pickBestSubtitleTrack
    };
    context.PlayerTrackPrefs = {
        getPref: context.getPref,
        upsertPref: context.upsertPref
    };
    assert.equal(typeof context.normalizeLang, 'function', 'real TrackLanguage.js must load');
    assert.equal(typeof context.getPref, 'function', 'real PlayerTrackPrefs.js must load');
    assert.equal(typeof context.Subtitles.pickDefault, 'function', 'real Subtitles.js fallback must load');

    vm.runInContext(qmlFunctions.join('\n'), context, { filename: 'PlayerPage.qml functions' });
    for (const name of qmlFunctionNames) root[name] = context[name];
    const getter = vm.runInContext('(function() {' + subRowsBody + '\n})', context,
        { filename: 'PlayerPage.qml subRows' });
    Object.defineProperty(root, 'subRows', { configurable: true, get: getter });

    return {
        root,
        settings,
        player,
        added,
        pending,
        deliver: function(list) {
            assert.equal(pending.length, 1, 'one subtitle request should be pending');
            const request = pending.shift();
            assert.equal(request.type, 'series');
            assert.equal(request.id, 'tt1234567:1:2');
            request.done(list);
        },
        fileLoaded: function() {
            root.fileReady = true;
            root.maybeAutoSelectTracks('file-loaded');
            root.maybeAutoSub();
        },
        freshPlay: function() {
            root.resetTrackAutomation();
            root.fileReady = false;
            player.subtitleTrack = '';
            player.subtitleTracks = [];
            root.fetchSubtitles();
            assert.equal(root.userTouchedSubs, false, 'fresh play resets the per-play manual-touch latch');
        },
        teachEnglishPreference: function() {
            root.onlineSubs = [spanishSubtitle(), englishSubtitle()];
            root.pickSubtitleTrack('ext:english');
            const pref = context.getPref(settings.trackPrefsJson, root.currentShowKey());
            assert.equal(pref.subtitleLang, 'eng', 'manual English choice is stored as normalized English');
            assert.equal(pref.subtitlesOff, false);
            root.userTouchedSubs = false;
            root.addedOnlineUrls = {};
            root.onlineSubs = [];
            player.subtitleTrack = '';
            player.subtitleTracks = [];
            added.length = 0;
        }
    };
}

function englishSubtitle() {
    return {
        id: 'ext:english',
        lang: 'en',
        langName: 'English',
        title: 'English',
        url: 'https://subs.example/episode.en.srt',
        preferred: true
    };
}

function spanishSubtitle() {
    return {
        id: 'ext:spanish',
        lang: 'spa',
        langName: 'Spanish',
        title: 'Spanish',
        url: 'https://subs.example/episode.es.srt',
        preferred: false
    };
}

// A prior explicit English choice is stored by the real per-show preference module. On the
// fresh playback, the addon response deliberately places Spanish first. The current QML and
// TrackLanguage policy must still add English once, and the fallback must not add it a second time.
{
    const h = makeHarness();
    h.teachEnglishPreference();
    h.freshPlay();
    h.fileLoaded();
    assert.equal(h.added.length, 0, 'file-loaded with no addon rows must wait');
    h.deliver([spanishSubtitle(), englishSubtitle()]);
    assert.equal(h.added.length, 1, 'a late addon response must cause exactly one online subtitle add');
    assert.equal(h.added[0].url, englishSubtitle().url, 'remembered English wins over Spanish addon order');
    assert.equal(h.added[0].select, true, 'the selected online subtitle is passed to mpv for activation');
    h.player.subtitleTracks = [{
        id: 'media3:subtitle:external:1',
        lang: 'en',
        title: 'English',
        external: true,
        selected: false
    }];
    h.root.maybeAutoSelectTracks('track-list');
    h.root.maybeAutoSub();
    assert.equal(h.added.length, 1, 'track-list and pickDefault fallback notifications must not add a duplicate');
    console.log('PASS persisted English preference is restored once after a fresh play with late addon');
}

// An addon can win the race against file-loaded. The callback leaves it pending while the file
// is closed; the later real automation function sees the new rows and selects English once.
{
    const h = makeHarness();
    h.teachEnglishPreference();
    h.freshPlay();
    h.deliver([spanishSubtitle(), englishSubtitle()]);
    assert.equal(h.added.length, 0, 'online-subs callback before file-loaded must not add to a closed player');
    h.fileLoaded();
    assert.equal(h.added.length, 1, 'file-loaded must retry automation for an addon that arrived early');
    assert.equal(h.added[0].url, englishSubtitle().url, 'early addon response must select the remembered English track');
    h.root.maybeAutoSelectTracks('track-list');
    h.root.maybeAutoSub();
    assert.equal(h.added.length, 1, 'early-arrival path must also add only once');
    console.log('PASS early addon arrival is deferred until file-loaded and selected once');
}

// A manual Off choice must suppress a late addon response now and remain persisted on the next
// play after fetchSubtitles resets its transient touch latch.
{
    const h = makeHarness();
    h.freshPlay();
    h.fileLoaded();
    h.root.turnSubtitlesOff();
    assert.equal(h.root.userTouchedSubs, true);
    assert.equal(h.player.subtitleTrack, '');
    h.deliver([spanishSubtitle(), englishSubtitle()]);
    assert.equal(h.added.length, 0, 'late addon response must not override manual Off');
    assert.equal(h.player.subtitleTrack, '', 'manual Off remains active after the addon response');

    h.added.length = 0;
    h.freshPlay();
    h.fileLoaded();
    h.deliver([spanishSubtitle(), englishSubtitle()]);
    assert.equal(h.added.length, 0, 'saved Off preference must suppress automatic selection on a fresh play');
    assert.equal(h.player.subtitleTrack, '', 'saved Off preference remains active on a fresh play');
    console.log('PASS manual Off blocks late addon selection and survives a fresh play');
}

console.log('android_subtitle_autopick_test: PASS');