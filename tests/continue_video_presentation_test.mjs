import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const source = readFileSync(new URL("../qml/ContinueVideoPresentation.js", import.meta.url), "utf8")
    .replace(/^\.(?:pragma|import).*$/gm, "");
const catalogDeliverySource = readFileSync(new URL("../qml/CatalogDelivery.js", import.meta.url), "utf8")
    .replace(/^\.pragma library\s*/m, "");
const CatalogDelivery = {};
runInNewContext(catalogDeliverySource, CatalogDelivery);
let lookups = 0;
const catalog = {
    ready: () => true,
    titlePresentation: (id) => {
        lookups++;
        return id === "tt0388629" ? { title: "One Piece", type: "series" } : {};
    }
};
const context = { Qt: { hsla: (...args) => args }, catalog, CatalogDelivery };
runInNewContext(source, context);
const record = { kind: "video", id: "tt0388629:23:13", progress: 0.42 };
const first = context.forEntry(record, catalog);
const second = context.forEntry(record, catalog);
assert.equal(first.title, "One Piece");
assert.equal(first.type, "series");
assert.equal(first.cover, "https://live.metahub.space/poster/small/tt0388629/img");
assert.equal(lookups, 1, "episode cards share the same offline title lookup");
assert.equal(second.title, first.title);
assert.equal(record.title, undefined, "progress record remains untouched");
assert.equal(context.forEntry({ kind: "book", id: "tt0388629" }, catalog).cover, undefined);
assert.equal(context.forEntry({ kind: "video", id: "other:12" }, catalog).cover, undefined);
const tmdbMovie = { kind: "video", id: "tmdb:550", libraryId: "tmdb:550", type: "movie" };
const tmdbEpisode = { kind: "video", id: "tmdb:1396:1:1", libraryId: "tmdb:1396", type: "series" };
const imdbEpisode = { kind: "video", id: "tt1234567:0:1", libraryId: "tt1234567", type: "series" };
assert.equal(context.forEntry(tmdbMovie, catalog).detailId, "tmdb:550");
assert.equal(context.forEntry(tmdbMovie, catalog).type, "movie");
assert.equal(context.forEntry(tmdbEpisode, catalog).detailId, "tmdb:1396");
assert.equal(context.forEntry(tmdbEpisode, catalog).type, "series");
assert.equal(context.forEntry(tmdbEpisode, catalog).cover, "");
assert.equal(lookups, 1, "native TMDB entries do not use the IMDb catalogue");
assert.equal(context.forEntry(imdbEpisode, catalog).detailId, "tt1234567");
assert.equal(context.forEntry(imdbEpisode, catalog).type, "series");
assert.equal(first.detailId, "", "legacy IMDb presentation keeps its fallback route");
for (const invalid of [
    { ...tmdbEpisode, libraryId: "tmdb:550" },
    { ...tmdbEpisode, type: "movie" },
    { ...tmdbEpisode, id: "tmdb:1396:01:1" },
    { ...tmdbEpisode, id: "tmdb:1396:1:0" },
    { ...tmdbEpisode, id: "tmdb:1396:1:1\n" },
    { ...tmdbMovie, id: "tmdb:0550", libraryId: "tmdb:0550" },
    { ...imdbEpisode, libraryId: "tt1234568" },
    { ...imdbEpisode, libraryId: undefined },
]) {
    assert.ok(!context.forEntry(invalid, catalog).detailId, JSON.stringify(invalid));
}
const main = readFileSync(new URL("../qml/Main.qml", import.meta.url), "utf8");
for (const name of ["resumeContinue", "detailContinue"]) {
    const start = main.indexOf(`    function ${name}(entry) {`);
    const end = main.indexOf("\n    }", start);
    assert.ok(start >= 0 && end > start);
    runInNewContext(main.slice(start, end + "\n    }".length), context);
}
const calls = [];
context.VideoPresentation = { forEntry: context.forEntry };
context.win = {
    resumeContinue: context.resumeContinue,
    openTheatreSeries: (item) => calls.push(["detail", item.id, item.type]),
    openLocalVideoSession: (item) => calls.push(["local", item.path]),
    openMovieSession: (infoHash) => calls.push(["torrent", infoHash]),
};
context.resumeContinue(tmdbMovie);
context.detailContinue(tmdbEpisode);
context.resumeContinue(imdbEpisode);
context.detailContinue(imdbEpisode);
context.resumeContinue({ ...tmdbEpisode, resume: { localPath: "C:/fixture.mkv" } });
context.resumeContinue({ ...tmdbEpisode, resume: { infoHash: "fixture-hash" } });
context.detailContinue({ kind: "video", id: "mal:21:1:2" });
context.detailContinue({ kind: "video", id: "raw-torrent", resume: { infoHash: "raw-hash" } });
context.detailContinue({ ...tmdbEpisode, libraryId: "tmdb:550" });
assert.deepEqual(calls, [
    ["detail", "tmdb:550", "movie"],
    ["detail", "tmdb:1396", "series"],
    ["detail", "tt1234567", "series"],
    ["detail", "tt1234567", "series"],
    ["local", "C:/fixture.mkv"],
    ["torrent", "fixture-hash"],
    ["detail", "mal:21", "series"],
    ["torrent", "raw-hash"],
]);
console.log("PASS Continue video presentation and routes");
