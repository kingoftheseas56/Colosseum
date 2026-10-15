import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const main = readFileSync(new URL("../qml/Main.qml", import.meta.url), "utf8");
function mainFunction(name, nextName) {
    const start = main.indexOf("    function " + name + "(");
    const end = main.indexOf(nextName, start);
    assert(start >= 0 && end > start, "locate the live " + name + " function");
    return main.slice(start, end);
}
const routes = [];
const context = {
    VideoPresentation: { forEntry: () => ({}) },
    Sessions: { openOrSwitch: request => routes.push(JSON.parse(JSON.stringify(request))) },
    win: { openComicSession: (...args) => routes.push({ series: args }) }
};
runInNewContext(mainFunction("resumeContinue", "    //  detail"), context);
runInNewContext(mainFunction("openVaultComic", "    function showLocalRejection("), context);
context.win.openVaultComic = context.openVaultComic;
runInNewContext(readFileSync(new URL("../qml/comicreader/ComicReaderState.js", import.meta.url), "utf8")
    .replace(/^\.pragma library\s*/, ""), context);

// A cold launch has only persisted Progress, with no live reader or series session.
const archivePath = "C:/Comics/Loose comic.cbz";
const saved = JSON.parse(JSON.stringify(context.progressPayload({
    seriesId: "vault:comic-content-id", kind: "comic", seriesTitle: "Loose comic",
    label: "Loose comic", cover: "", chapterId: archivePath,
    page: 7, max: 20, maxSeen: 7, style: "comic"
})));
const before = JSON.stringify(saved);
context.resumeContinue(saved);
assert.deepEqual(routes.pop(), {
    appType: "tankoban", contentKind: "comic", title: "Loose comic",
    target: { vaultPath: archivePath, id: saved.id, title: "Loose comic" }
}, "Continue must recreate the standalone Vault reader from persisted CBZ progress");
assert.equal(JSON.stringify(saved), before, "routing preserves the saved page for the Vault reader");

// The standalone door also supports files whose Vault identity is unavailable.
context.openVaultComic(archivePath, "", "Loose comic");
const fallbackSession = routes.pop();
assert.equal(fallbackSession.target.id, "vault-file:" + archivePath);
const fallbackSaved = JSON.parse(JSON.stringify(context.progressPayload({
    seriesId: fallbackSession.target.id, kind: "comic", seriesTitle: fallbackSession.title,
    label: "Loose comic", cover: "", chapterId: fallbackSession.target.vaultPath,
    page: 7, max: 20, maxSeen: 7, style: "comic"
})));
context.resumeContinue(fallbackSaved);
assert.deepEqual(routes.pop(), fallbackSession,
    "Continue must retain the standalone door's fallback identity after persistence");

context.resumeContinue({ ...saved, kind: "manga", resume: { path: archivePath, page: 7 } });
assert.equal(routes.pop().target.vaultPath, archivePath,
    "Vault records with a path or the older manga namespace still reopen locally");

for (const [kind, id] of [["comic", "gc:run"], ["manga", "manga-series"], ["tankoban", "manga-series"]]) {
    context.resumeContinue({ kind, id, title: "Series", resume: { chapterId: "chapter-2" } });
    assert.deepEqual(routes.pop(), {
        series: kind === "tankoban" ? ["Series", id, "chapter-2", "tankoban"]
            : ["Series", id, "chapter-2"]
    }, "catalogue " + kind + " continues through its existing series route");
}
assert.equal(routes.length, 0);

// Setting archivePath synchronously opens the reader and consumes its resume lookup.
// Execute the actual loader/activation bodies against that property seam.
function readerItem(id = "", title = "Comic") {
    const opens = [];
    const item = { vaultId: id, title };
    Object.defineProperty(item, "archivePath", {
        set(path) { opens.push({ path, id: item.vaultId, title: item.title }); }
    });
    for (const signal of ["minimizeRequested", "closeRequested", "backRequested", "fullscreenRequested"])
        item[signal] = { connect: () => {} };
    return { item, opens };
}
const coldReader = readerItem();
const loader = { archivePath, vaultId: saved.id, title: saved.title };
const loaderStart = main.indexOf("        id: vaultComicLayer");
const handlerStart = main.indexOf("        onLoaded: {", loaderStart);
const handlerEnd = main.indexOf("\n        }", handlerStart);
assert(loaderStart >= 0 && handlerStart > loaderStart && handlerEnd > handlerStart);
const handler = main.slice(handlerStart + "        onLoaded: {".length, handlerEnd);
runInNewContext(handler, { item: coldReader.item, vaultComicLayer: loader, win: {} });
assert.deepEqual(coldReader.opens, [{ path: archivePath, id: saved.id, title: saved.title }],
    "cold reader must know its progress identity and title before opening the archive");

const liveReader = readerItem("vault:previous-comic", "Previous comic");
const activation = {
    win: {}, vaultComicLayer: { active: true, item: liveReader.item },
    wallpaperWorldForSession: () => "Tankoban", refreshWallpaper: () => {}
};
runInNewContext(mainFunction("activateSession", "    function captureSession("), activation);
activation.activateSession({ id: "comic-session", contentKind: "comic", title: saved.title,
    target: { vaultPath: archivePath, id: saved.id, title: saved.title } });
assert.deepEqual(liveReader.opens, coldReader.opens,
    "a reused reader must use the incoming progress identity before opening its archive");
console.log("PASS Continue comic routing and reader identity initialization");
