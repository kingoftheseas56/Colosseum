import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Execute the production QML's JavaScript functions. This is a policy boundary
// test; the separate QuickTest fixture qualifies real Qt context/save behavior.
const source = fs.readFileSync(new URL('../qml/feria/FeriaPageTools.qml', import.meta.url), 'utf8');
function functions(text) {
    const result = [];
    for (const match of text.matchAll(/^    function (\w+)\([^\n]*\) \{/gm)) {
        let depth = 1, quote = '', comment = '', start = text.indexOf('{', match.index), i = start + 1;
        for (; i < text.length && depth; ++i) {
            const ch = text[i], next = text[i + 1];
            if (comment === 'line') { if (ch === '\n') comment = ''; continue; }
            if (comment === 'block') { if (ch === '*' && next === '/') { comment = ''; ++i; } continue; }
            if (quote) { if (ch === '\\') ++i; else if (ch === quote) quote = ''; continue; }
            if (ch === '"' || ch === "'") quote = ch;
            else if (ch === '/' && next === '/') { comment = 'line'; ++i; }
            else if (ch === '/' && next === '*') { comment = 'block'; ++i; }
            else if (ch === '{') ++depth;
            else if (ch === '}') --depth;
        }
        result.push(text.slice(match.index, i));
    }
    return result.join('\n');
}
const WebEngineView = { SavePage: 1, DownloadLinkToDisk: 2, DownloadImageToDisk: 3, DownloadMediaToDisk: 4, OpenLinkInNewTab: 5, CopyLinkToClipboard: 6, CopyImageToClipboard: 7, CopyMediaUrlToClipboard: 8 };
const ContextMenuRequest = { MediaTypeNone: 0, MediaTypeImage: 1, MediaTypeVideo: 2, MediaTypeAudio: 3, MediaTypeCanvas: 4, MediaCanSave: 16 };
function fixture(variant = '') {
    const actions = [], offers = [], cancels = [];
    const browser = { visible: true, closingSession: false, rendererLost: false, replacementLoadPending: false,
        documentReady: true, saveContextControlsAvailable: true, navigationGeneration: 1, profile: {}, url: 'https://fixture.test/page',
        accountSessionId: 'space-a', accountVisitToken: 'visit-a', mainFrame: { children: [] },
        saveBinding: { accountEpoch: 1, profileId: 'profile-a', spaceId: 'space-a', pageGeneration: 1,
            documentGeneration: 1, hostId: 'embedded', hostGeneration: 1 },
        action: () => ({enabled: true}), triggerWebAction: action => actions.push(action),
        wakePage() {}, message() {} };
    const owner = { active: true, profile: browser.profile,
        beginNativeSave(view, snapshot) { const id = 'save-' + (offers.length + 1); offers.push({id,view,snapshot}); return id; },
        cancelNativeSave(id, reason) { cancels.push({id,reason}); },
        pendingPageSaveForView() { return false; } };
    browser.saveAdmissionOwner = owner;
    const state = { browser, active: true, contextSerial: 0, contextSnapshot: null, saveOperationSerial: 0,
        saveStatus: '', saveOutcome: {}, WebEngineView, ContextMenuRequest,
        WebEngineDownloadRequest: { SingleHtmlSaveFormat:0, CompleteHtmlSaveFormat:1, MimeHtmlSaveFormat:2 },
        Qt: { callLater: fn => fn() }, console };
    state.tools = state;
    let production = functions(source);
    if (variant === 'context') production = production.replace('contextId !== contextSnapshot.contextId', 'false');
    if (variant === 'document') production = production.replace('snapshot.navigationGeneration !== browser.navigationGeneration', 'false');
    if (variant === 'identity') production = production.replace('snapshot.binding[key] !== fresh[key]', 'false');
    if (variant === 'media') production = production.replace('(snapshot.mediaFlags & ContextMenuRequest.MediaCanSave) !== 0', 'true');
    if (variant === 'serialization') production = production.replace('&& owner.pendingPageSaveForView(browser)', '&& false');
    if (variant === 'frame') production = production.replace('!snapshot.mainFrameOnly || !mainFrameContextOnly()', 'false');
    if (variant === 'menu') production = production.replace('if (replaceMenu) request.accepted = true', 'if (false) request.accepted = true');
    if (variant === 'url') production = production.replace(/function admittedSaveUrl\(value\) \{[\s\S]*?\n    \}/, 'function admittedSaveUrl(value) { return true }');
    vm.createContext(state); vm.runInContext(production, state);
    return { state, browser, owner, actions, offers, cancels };
}
function link(url = 'https://fixture.test/file.bin') {
    return {linkUrl: url, mediaUrl: '', mediaType: 0, mediaFlags: 0, linkText: 'file'};
}
function snapshot(f, request = link()) { return f.state.captureSaveContext(request); }
function check(variant = '') {
    let f = fixture(variant);
    assert.equal(typeof f.state.captureSaveContext, 'function', 'production must capture a save context');
    const a = snapshot(f), b = snapshot(f, link('https://fixture.test/b.bin'));
    assert.notEqual(a.contextId, b.contextId);
    assert.equal(f.state.saveFromContext(a.contextId, 'link').status, 'invalidated', 'new context must retire prior native target');
    assert.equal(f.actions.length, 0);
    assert.equal(f.state.saveFromContext(b.contextId, 'link').status, 'dispatched');
    assert.equal(f.actions[0], WebEngineView.DownloadLinkToDisk);
    assert.equal(f.state.saveFromContext(b.contextId, 'link').status, 'invalidated', 'snapshot is single use');

    f = fixture(variant); const c = snapshot(f); f.browser.navigationGeneration++;
    assert.equal(f.state.saveFromContext(c.contextId, 'link').status, 'invalidated', 'same-URL new document must reject old context');
    assert.equal(f.actions.length, 0);
    for (const key of ['accountEpoch','profileId','spaceId','pageGeneration','documentGeneration','hostId','hostGeneration']) {
        f = fixture(variant); const old = snapshot(f); f.browser.saveBinding[key] += 'replacement';
        assert.equal(f.state.saveFromContext(old.contextId, 'link').status, 'invalidated', `${key} replacement rejects context`);
    }
    f = fixture(variant); const hidden = snapshot(f); f.state.active = false;
    assert.equal(f.state.saveFromContext(hidden.contextId, 'link').status, 'invalidated', 'selection cannot redirect a context');

    for (const url of ['javascript:alert(1)','data:video/mp4,a','blob:https://fixture.test/x','file:///tmp/a','https://u:p@fixture.test/file','https://fixture.test/\nfile']) {
        f = fixture(variant); const bad = snapshot(f, link(url));
        assert.equal(f.state.saveFromContext(bad.contextId, 'link').status, 'unsupported', url);
        assert.equal(f.actions.length, 0);
    }
    for (const type of [ContextMenuRequest.MediaTypeAudio,ContextMenuRequest.MediaTypeVideo]) {
        f = fixture(variant); const blocked = snapshot(f, {linkUrl:'', mediaUrl:'https://fixture.test/media',mediaType:type,mediaFlags:0});
        assert.equal(f.state.saveFromContext(blocked.contextId, 'media').status, 'unsupported', 'MediaCanSave is required even with enabled native action');
        f = fixture(variant); const supported = snapshot(f, {linkUrl:'',mediaUrl:'https://fixture.test/media',mediaType:type,mediaFlags:16});
        assert.equal(f.state.saveFromContext(supported.contextId, 'media').status, 'dispatched');
    }
    f = fixture(variant); const canvas = snapshot(f, {linkUrl:'',mediaUrl:'',mediaType:4,mediaFlags:16});
    assert.equal(f.state.saveFromContext(canvas.contextId, 'image').status, 'unsupported');
    assert.equal(f.state.contextNativeAction(canvas.contextId, 'copyImage').status, 'dispatched', 'canvas keeps supported native copy while Save is unavailable');
    f = fixture(variant); f.browser.mainFrame.children = [{}]; const frame = snapshot(f);
    assert.equal(frame.frameProvenance, 'unavailable');
    assert.equal(f.state.saveFromContext(frame.contextId, 'link').status, 'unsupported', 'unqualified frame fidelity must fail closed');

    f = fixture(variant); f.browser.action = () => null;
    assert.equal(f.state.savePage().status, 'unsupported', 'missing native action must fail closed');
    f = fixture(variant); f.browser.saveAdmissionOwner = null;
    assert.equal(f.state.savePage().status, 'unsupported', 'no listener cannot dispatch an explicit-path or native save');
    assert.equal(f.actions.length, 0);
    f = fixture(variant); f.owner.pendingPageSaveForView = () => true;
    assert.equal(f.state.savePage().status, 'blocked', 'initial page-save admission must serialize');
    f = fixture(variant); const native = snapshot(f);
    assert.equal(f.state.contextNativeAction(native.contextId, 'openLink').status, 'dispatched');
    assert.equal(f.actions[0], WebEngineView.OpenLinkInNewTab);
    assert.equal(f.state.contextNativeAction(native.contextId, 'copyLink').status, 'invalidated');
    f = fixture(variant); const copy = snapshot(f);
    assert.equal(f.state.contextNativeAction(copy.contextId, 'arbitraryAction').status, 'unsupported');
    const request = link(); snapshot(f, request); assert.equal(request.accepted, true, 'resource native menu is replaced only when chrome controls exist');
    f.browser.saveContextControlsAvailable = false; const noSurface = link(); snapshot(f,noSurface);
    assert.equal(noSurface.accepted, true, 'all shared-profile native save starts must be guarded even without embedded chrome');
    f.browser.saveContextControlsAvailable = true; const text = {linkUrl:'',mediaUrl:'',mediaType:0,mediaFlags:0,selectedText:'selected'}; snapshot(f,text);
    assert.equal(text.accepted, undefined, 'text/edit native menus remain usable');
    f = fixture(variant); f.browser.navigationGeneration = 7; f.browser.saveBinding.documentGeneration = 7;
    assert.equal(f.state.savePage().status, 'dispatched', 'fresh command reads current document');
    assert.equal(f.offers[0].snapshot.navigationGeneration, 7);
    assert.equal(f.actions[0], WebEngineView.SavePage);
    assert.equal(f.offers[0].snapshot.kind, 'page');
    f = fixture(variant); assert.equal(f.state.savePage(99).status, 'unsupported');
    assert.equal(f.actions.length, 0);
    f = fixture(variant); f.owner.beginNativeSave = () => '';
    assert.equal(f.state.savePage().status, 'blocked', 'failed owner admission must not execute native action');
    assert.equal(f.actions.length, 0);
}
check(process.env.FERIA_SAVE_NEGATIVE || '');
console.log('Feria Save context boundary checks passed (policy test, not native runtime qualification).');
