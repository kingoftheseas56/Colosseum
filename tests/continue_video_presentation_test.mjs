import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const source = readFileSync(new URL("../qml/ContinueVideoPresentation.js", import.meta.url), "utf8")
    .replace(/^\.pragma library\s*/, "");
let lookups = 0;
const catalog = {
    ready: () => true,
    titlePresentation: (id) => {
        lookups++;
        return id === "tt0388629" ? { title: "One Piece", type: "series" } : {};
    }
};
const context = { Qt: { hsla: (...args) => args }, catalog };
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
console.log("PASS Continue video presentation");
