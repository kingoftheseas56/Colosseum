// Arc 74-C: run the actual Main.qml Continue router against isolated service doubles.
// No real Progress, Downloads, or user profile is used.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const main = readFileSync(new URL("../qml/Main.qml", import.meta.url), "utf8");
const start = main.indexOf("    function resumeContinue(entry) {");
const end = main.indexOf("    //  detail", start);
assert(start >= 0 && end > start, "current production Continue function found");
const source = main.slice(start, end);
const records = [];
const details = [];
const downloader = { states: Object.create(null),
    statusOf: id => downloader.states[String(id)] || {state: "none"} };
const volumes = {states: Object.create(null),
    statusOf: id => volumes.states[String(id)] || {state: "none"} };
const win = {
    openComicSession(...args) { records.push({kind:"reader", args}) },
    detailContinue(entry) { details.push(entry) },
    openVaultComic(...args) { records.push({kind:"vault", args}) },
    openBookSession(...args) { records.push({kind:"book", args}) },
    openBook(...args) { records.push({kind:"book-detail", args}) },
    openLocalVideoSession(...args) { records.push({kind:"video", args}) },
    openMovieSession(...args) { records.push({kind:"movie", args}) },
    openTheatreSeries(...args) { records.push({kind:"theatre-detail", args}) },
    openVinylResume(...args) { records.push({kind:"music", args}) }
};
const ctx = {win, Downloads: downloader, TankobanVolumes: volumes,
    VideoPresentation: {forEntry: () => ({title: "",cover:"",type:""})}};
runInNewContext(source,ctx);
let checked=0;
function visit(entry) {
    const before=JSON.stringify(entry);
    const previous=records.length, oldDetails=details.length;
    ctx.resumeContinue(entry);
    assert.equal(JSON.stringify(entry), before, "production routing must never mutate Progress payload");
    return {route: records.slice(previous), details: details.slice(oldDetails)};
}
function check(condition,message) {assert.ok(condition,message);checked++;}
const chapter={id:"mal:1",kind:"manga",title:"Monster",resume:{
    chapterId:"tankoyomi:pt:provider:chapter-47",page:12,pageFraction:.35}};
downloader.states[chapter.resume.chapterId]={state:"done"};
let result=visit(chapter);
check(result.route.length===1 && result.route[0].kind==="reader",
      "downloaded Portuguese manga routes directly to reader");
check(result.route[0].args.join("|")===[chapter.title,chapter.id,chapter.resume.chapterId].join("|"),
      "reader receives exact qualified chapter ID and canonical series ID");
check(result.details.length===0, "successful resume must not open chapter selection");
delete downloader.states[chapter.resume.chapterId];
result=visit(chapter);
check(result.route.length===0 && result.details.length===1,
      "missing saved chapter opens details rather than an empty reader");
result=visit({...chapter,resume:{}});
check(result.route.length===0 && result.details.length===1,
      "empty chapter target does not open an empty reader");
result=visit({...chapter,resume:{chapterId:chapter.resume.chapterId,page:12}});
check(result.route.length===0 && result.details.length===1,
      "failed/offline chapter cannot silently substitute another chapter");
const volume={id:"mal:13",kind:"tankoban",title:"One Piece",resume:{
    chapterId:"volume-mal13-6",page:9,pageFraction:.6}};
volumes.states[volume.resume.chapterId]={state:"ready"};
result=visit(volume);
check(result.route.length===1 && result.route[0].args[2]===volume.resume.chapterId
      && result.route[0].args[3]==="tankoban",
      "ready volume routes with exact saved volume ID and distinct namespace");
delete volumes.states[volume.resume.chapterId];
result=visit(volume);
check(result.route.length===0 && result.details.length===1,
      "missing volume routes to series page without erasing saved record");
result=visit({...volume,resume:{}});
check(result.route.length===0 && result.details.length===1,
      "empty volume target routes to recovery/detail");
result=visit({id:"gc:comic",kind:"comic",title:"Western issue",resume:{chapterId:"issue-2"}});
check(result.route.length===1 && result.route[0].args[2]==="issue-2",
      "western comic routing stays unchanged");
result=visit({id:"vault:comic:1",kind:"manga",title:"Loose archive",
              resume:{chapterId:"C:/Fixtures/Comic.cbz"}});
check(result.route.length===1 && result.route[0].kind==="vault",
      "Vault manga legacy identity stays on local archive door");
result=visit({id:"book:1",kind:"book",title:"Novel",resume:{path:"C:/Books/Novel.epub"}});
check(result.route.length===1 && result.route[0].kind==="book",
      "book resume unaffected");
result=visit({id:"tt1",kind:"video",title:"Film",resume:{localPath:"C:/Video/test.mp4",position:130}});
check(result.route.length===1 && result.route[0].kind==="video",
      "video resume unaffected");
console.log("PASS " + checked + " production Main.qml Continue routing checks");

const extensionsStart = main.indexOf("    function openExtensionsPage(world, configurationId) {");
const pendingStart = main.indexOf("    function openPendingExtensionConfiguration() {", extensionsStart);
const closeStart = main.indexOf("    function closeExtensionsPage() {", pendingStart);
const updateStart = main.indexOf("    // ---- Update page:", closeStart);
assert(extensionsStart >= 0 && pendingStart > extensionsStart && closeStart > pendingStart
       && updateStart > closeStart,
       "current production Extensions navigation functions found");
const extensionsSource = main.slice(extensionsStart, updateStart);

let extensionChecks = 0;
function checkExtension(condition, message) {
    assert.ok(condition, message);
    extensionChecks++;
}

function makeExtensionsRouter({loaded = true, guard = () => false} = {}) {
    const opened = [];
    const item = loaded ? {
        world: "",
        openConfiguration(id) { opened.push(id); },
        takeKeyboardFocus() {}
    } : null;
    const ctx = {
        pendingExtensionConfiguration: "",
        extensionsLayer: {active: false, item},
        downloadsLayer: {active: true},
        updateLayer: {active: true},
        historyStatsLayer: {active: true},
        syncCenterLayer: {active: true},
        vaultLayer: {active: true},
        taskbar: {open: true},
        Qt: {callLater(callback) { callback(); }}
    };
    const win = {
        bookRouteGeneration: 0,
        parkFeria() {},
        guardNamedCollectionsNavigation(continuation) { return guard(continuation); },
        openExtensionsPage(...args) { return ctx.openExtensionsPage(...args); },
        openPendingExtensionConfiguration() { return ctx.openPendingExtensionConfiguration(); }
    };
    ctx.win = win;
    runInNewContext(extensionsSource, ctx);
    return {ctx, item, opened};
}

const configurationId = "colosseum.well.tankoyomi";

// An already-created Loader item should receive the requested configuration once.
{
    const router = makeExtensionsRouter();
    router.ctx.openExtensionsPage("tankoban", configurationId);
    router.ctx.openPendingExtensionConfiguration();
    checkExtension(router.opened.length === 1 && router.opened[0] === configurationId,
                   "ready Extensions item opens the requested Tankoyomi configuration once");
    checkExtension(router.ctx.pendingExtensionConfiguration === "",
                   "ready configuration open clears the pending ID");
    checkExtension(router.item.world === "tankoban",
                   "ready Extensions item receives the requested world");
}

// If activation precedes Loader completion, retain the ID until the item arrives.
{
    const router = makeExtensionsRouter({loaded: false});
    router.ctx.openExtensionsPage("tankoban", configurationId);
    checkExtension(router.ctx.pendingExtensionConfiguration === configurationId
                   && router.opened.length === 0,
                   "unloaded Extensions item keeps the pending configuration ID");
    router.ctx.extensionsLayer.item = {
        world: "",
        openConfiguration(id) { router.opened.push(id); },
        takeKeyboardFocus() {}
    };
    router.ctx.openPendingExtensionConfiguration();
    checkExtension(router.opened.length === 1 && router.opened[0] === configurationId
                   && router.ctx.pendingExtensionConfiguration === "",
                   "pending configuration opens and clears when the Loader item arrives");
}

// The navigation guard must keep the original arguments in its deferred callback.
{
    let shouldDefer = true;
    let deferred = null;
    const router = makeExtensionsRouter({guard(continuation) {
        if (!shouldDefer) return false;
        deferred = continuation;
        return true;
    }});
    router.ctx.openExtensionsPage("tankoban", configurationId);
    checkExtension(typeof deferred === "function" && !router.ctx.extensionsLayer.active
                   && router.opened.length === 0,
                   "named-collection guard defers the Extensions route");
    shouldDefer = false;
    deferred();
    checkExtension(router.opened.length === 1 && router.opened[0] === configurationId
                   && router.ctx.pendingExtensionConfiguration === "",
                   "deferred callback preserves and opens the requested configuration ID");
}

// Closing the Extensions page must discard any configuration still waiting on the Loader.
{
    const router = makeExtensionsRouter({loaded: false});
    router.ctx.openExtensionsPage("tankoban", configurationId);
    router.ctx.closeExtensionsPage();
    checkExtension(router.ctx.pendingExtensionConfiguration === ""
                   && !router.ctx.extensionsLayer.active,
                   "closing Extensions clears a pending configuration and deactivates the layer");
}

console.log("PASS " + extensionChecks + " production Main.qml Extensions navigation checks");
