import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const source = readFileSync(new URL("../qml/feria/FeriaSessionDeck.qml", import.meta.url), "utf8");
const clone = value => JSON.parse(JSON.stringify(value));
function bodyAt(start) {
    const open = source.indexOf("{", start);
    let depth = 1, quote = "", comment = "";
    for (let i = open + 1; i < source.length; ++i) {
        const char = source[i], next = source[i + 1];
        if (comment === "line") { if (char === "\n") comment = ""; continue; }
        if (comment === "block") { if (char === "*" && next === "/") { comment = ""; i++; } continue; }
        if (quote) { if (char === "\\") i++; else if (char === quote) quote = ""; continue; }
        if (char === "'" || char === '"') quote = char;
        else if (char === "/" && next === "/") { comment = "line"; i++; }
        else if (char === "/" && next === "*") { comment = "block"; i++; }
        else if (char === "{") depth++;
        else if (char === "}" && --depth === 0) return { body: source.slice(open + 1, i), end: i + 1 };
    }
    throw new Error("Unterminated live QML body");
}
const initialTabs = [
    { providerId: "ytmusic", url: "https://music.youtube.com/watch?v=old", title: "Previous song" },
    { providerId: "mangaplus", url: "https://mangaplus.shueisha.co.jp/viewer/old", title: "Previous chapter" }
];
function fixture(saved = initialTabs, automatic = false) {
    const accountStore = {
        browserTabs: clone(saved), browserPreferences: { restoreTabs: automatic },
        browserRestoreOffers: [], captured: false,
        captureBrowserRestoreOffers() {
            if (this.captured) return true;
            this.browserRestoreOffers = clone(this.browserTabs).map((row, i) => ({ ...row, restoreId: 'offer-' + i }));
            this.captured = true;
            return true;
        },
        saveBrowserRestoreOffers(rows) { this.browserRestoreOffers = clone(rows); return true; },
        saveBrowserTabs(rows) { this.browserTabs = clone(rows); return true; }
    };
    const sessions = {
        rows: [], activeId: "", serial: 0, failedUrl: "",
        browsers() { return this.rows; },
        openBrowser(descriptor) {
            if (descriptor.url === this.failedUrl) return "";
            const id = "session-" + (++this.serial);
            this.rows.push({ id, title: descriptor.title, target: clone(descriptor) });
            context.entries[id] = { sessionId: id, descriptor: clone(descriptor), browser: {loading: false}, mainLoadSucceeded: false };
            return id;
        },
        switchTo(id) { this.activeId = id; }
    };
    const queued = [];
    const context = {
        accountStore, sessions, ready: true, quiescing: false, restoredProfile: false,
        entries: {}, activeEntry: null, closedTabs: [], policy: { storageRoot: "profile-a" },
        message() {},
        snapshotTimer: { stop() {}, restart() { context.persistTabs(); } },
        Qt: { callLater(callback) { queued.push(callback); } }
    };
    context.deck = context;
    Object.defineProperty(context, 'restoreSource', { get() { return accountStore.browserRestoreOffers; } });
    for (const match of source.matchAll(/^    function (\w+)\([^\n]*\) \{/gm))
        runInNewContext(source.slice(match.index, bodyAt(match.index).end), context);
    context.syncSessions = () => {};
    const flush = () => { while (queued.length) queued.shift()(); };
    runInNewContext(bodyAt(source.indexOf("    Component.onCompleted:")).body, context);
    flush();
    return { context, accountStore, sessions, flush };
}

const manual = fixture();
manual.sessions.openBrowser({ providerId: "mangaplus", url: "https://example.com/new", title: "New session" });
manual.context.scheduleSnapshot();
assert.equal(manual.accountStore.browserTabs.length, 1, "the live session still persists its current tabs");
manual.context.restoreTabs();
assert.deepEqual(manual.sessions.rows.map(row => row.target.url),
    ["https://example.com/new", ...initialTabs.map(row => row.url)],
    "manual restore must retain the previous session after the first current-session save");
assert.equal(manual.context.restoreSource.length, 2, "allocation must not consume offers before real load success");
for (const row of manual.sessions.rows.slice(1)) manual.context.completeRestore(manual.context.entries[row.id]);
assert.equal(manual.context.restoreSource.length, 0, "successful restorations consume only their own offers");
manual.context.restoreTabs();
assert.equal(manual.sessions.rows.length, 3, "repeated restore does not duplicate tabs");

const automatic = fixture(initialTabs, true);
assert.equal(automatic.sessions.rows.length, 0, "legacy automatic preference cannot open pages at startup");
assert.equal(automatic.context.restoreSource.length, 2);

const profile = fixture();
profile.accountStore.browserTabs = [{ providerId: "ytmusic", url: "https://example.com/profile-b", title: "Other profile" }];
profile.accountStore.captured = false;
const accountConnection = source.indexOf("        target: deck.accountStore");
runInNewContext(bodyAt(source.indexOf("        function onProfileChanged()", accountConnection)).body, profile.context);
profile.flush();
profile.context.restoreTabs();
assert.deepEqual(profile.sessions.rows.map(row => row.target.url), ["https://example.com/profile-b"],
    "a profile switch replaces the restore source instead of restoring another profile's tabs");

const partial = fixture();
partial.sessions.failedUrl = initialTabs[1].url;
partial.context.restoreTabs();
assert.equal(partial.context.restoreSource.length, 2, "allocation failure and uncompleted navigation both remain offered");
partial.context.completeRestore(partial.context.entries[partial.sessions.rows[0].id]);
assert.equal(partial.context.restoreSource.length, 1, "failed allocation remains available after another offer succeeds");
partial.context.dismissRestoreTabs();
assert.equal(partial.context.restoreSource.length, 0, "explicit dismissal consumes the source");

const duplicate = fixture([initialTabs[0], initialTabs[0]]);
duplicate.context.restoreTabs('ytmusic');
const [successful, failed] = duplicate.sessions.rows;
duplicate.context.entries[successful.id].mainLoadSucceeded = true;
duplicate.context.completeRestore(duplicate.context.entries[successful.id]);
duplicate.context.entries[failed.id].browser.loading = true;
duplicate.context.restoreTabs('ytmusic');
assert.equal(duplicate.context.restoreSource.length, 1,
    'A successful sibling with the same URL must not consume a different pending restore ID');
assert.equal(duplicate.sessions.activeId, failed.id, 'Retry must select the exact pending sibling');

const captureFailure = fixture();
captureFailure.accountStore.browserRestoreOffers = [];
captureFailure.accountStore.captureBrowserRestoreOffers = () => false;
let snapshotWritten = false;
captureFailure.accountStore.saveBrowserTabs = () => { snapshotWritten = true; return true; };
captureFailure.context.persistTabs();
assert.equal(snapshotWritten, false, 'A failed initial offer capture must fence overwriting the previous snapshot');
captureFailure.accountStore.captureBrowserRestoreOffers = () => true;
captureFailure.context.persistTabs();
assert.equal(snapshotWritten, true, 'A successful capture retry must release current snapshot saving');
console.log("PASS Feria manual-only previous-session restore, successful commit, profile isolation and retry/dismissal");
