import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
import test from "node:test";

// Execute production QML bodies, not copies of the engine algorithms. The fake
// adapter deliberately unloads timing on load, like mpv, and delivers fileLoaded
// separately so pending seeks and terminal-state reloads are observable.
const qml = fs.readFileSync(new URL("../../qml/AudiobookSession.qml", import.meta.url), "utf8");
function qmlFunction(name, indent = 4) {
    const pad = " ".repeat(indent);
    const match = qml.match(new RegExp("^" + pad + "function " + name + "\\(([^)]*)\\) \\{\\r?\\n([\\s\\S]*?)^" + pad + "\\}", "m"));
    assert.ok(match, "missing QML function: " + name);
    const args = match[1].replace(/:\s*\w+/g, ""); // QML's typed parameters
    return "(function(" + args + ") {\n" + match[2] + "})";
}
function qmlProperty(name) {
    const match = qml.match(new RegExp("^    (?:readonly )?property \\w+ " + name + ": (.*)$", "m"));
    assert.ok(match, "missing QML property: " + name);
    if (match[1].trim() === "{") {
        const block = qml.slice(match.index).match(/^[\s\S]*?\r?\n    \}/);
        assert.ok(block, name + " binding block");
        return "(function() {" + block[0].slice(block[0].indexOf("{") + 1, -1) + "})()";
    }
    return "(" + match[1].trim() + ")";
}
function harness() {
    const seeks = [], loads = [], records = [];
    const mpv = {
        position: 0, duration: 600, pause: false, speed: 1,
        seekExact(t) { seeks.push(t); this.position = t; },
        loadSource(file) { loads.push(file); this.position = 0; this.duration = 0; }
    };
    const session = {
        ready: true, completed: false, activePairKey: "recording-a", book: {},
        files: ["a.mp3", "b.mp3", "c.mp3"], currentIndex: 0, chapterModel: [],
        pendingResumeSec: -1, pendingResumeFromEndSec: -1, jumpHistory: [],
        sleepMinutes: 30, sleepEndsAtMs: 0, sleepNowMs: 0,
        activityBeginIfNeeded() {}, activityDiscontinuity() {}, playbackRequested() {},
        rebuildChapters() {}, recordProgress() { records.push(mpv.position); }
    };
    Object.defineProperty(session, "multiFile", { get: () => session.files.length > 1 });
    for (const alias of ["position", "duration", "paused", "speed"])
        Object.defineProperty(session, alias, { get: () => mpv[alias === "paused" ? "pause" : alias] });
    const context = {
        session, mpv, Math, Date,
        Audiobooks: { resolveRecordingKey: k => k, localFiles: () => ["new.mp3"] }
    };
    const evaluate = code => vm.runInNewContext(code, context);
    for (const name of ["chapterIndex", "chapterStart", "chapterEnd", "chapterPosition", "chapterDuration", "bookFraction", "canGoBack", "sleepMinutesLeft", "speedLadder"])
        Object.defineProperty(session, name, { get: () => evaluate(qmlProperty(name)) });
    for (const name of ["seekInChapter", "skipBook", "markJumpOrigin", "backToPrevious", "openFor", "cycleSpeed", "seekTo", "playIndexKeepState", "playIndex", "overallProgress", "requestAudioPlayback", "stop"])
        session[name] = (...args) => evaluate(qmlFunction(name))(...args);
    return {
        session, mpv, seeks, loads, records, evaluate,
        loaded(duration = 600) { mpv.duration = duration; evaluate(qmlFunction("onFileLoaded", 8))(); }
    };
}

test("skipBook crosses into next file", () => {
    const h = harness();
    h.mpv.position = 580;
    h.session.skipBook(60);
    assert.equal(h.session.currentIndex, 1);
    assert.deepEqual(h.loads, ["b.mp3"]);
    assert.equal(h.session.pendingResumeSec, 40);
    h.loaded();
    assert.deepEqual(h.seeks, [40]);
    assert.equal(h.session.pendingResumeSec, -1);
});

test("chapter bounds from m4b chapters", () => {
    const { session: s, mpv } = harness();
    s.files = ["book.m4b"];
    s.chapterModel = [0, 600, 1500].map(time => ({ time }));
    mpv.duration = 3000; mpv.position = 700;
    assert.equal(s.chapterIndex, 1);
    assert.equal(s.chapterStart, 600);
    assert.equal(s.chapterEnd, 1500);
    assert.equal(s.chapterPosition, 100);
    assert.equal(s.chapterDuration, 900);
    mpv.position = 1500;
    assert.equal(s.chapterIndex, 2);
    assert.equal(s.chapterEnd, 3000);
});

test("chapter bounds fall back to whole file when no chapters", () => {
    const { session: s, mpv, seeks } = harness();
    s.files = ["book.m4b"]; s.chapterModel = []; mpv.duration = 3000;
    assert.equal(s.chapterIndex, 0);
    assert.equal(s.chapterStart, 0);
    assert.equal(s.chapterEnd, 3000);
    assert.equal(s.chapterDuration, 3000);
    s.seekInChapter(0.5);
    assert.deepEqual(seeks, [1500]);
    s.seekInChapter(-1); s.seekInChapter(2);
    assert.deepEqual(seeks, [1500, 0, 3000]);
    mpv.duration = 0;
    assert.equal(s.chapterDuration, 0);
    assert.equal(s.bookFraction, 0);
});

test("multi-file chapter timing is file-relative and bookFraction is book-wide", () => {
    const { session: s, mpv } = harness();
    s.currentIndex = 1; mpv.position = 300;
    assert.equal(s.chapterIndex, 1);
    assert.equal(s.chapterStart, 0);
    assert.equal(s.chapterEnd, 600);
    assert.equal(s.chapterPosition, 300);
    assert.equal(s.chapterDuration, 600);
    assert.equal(s.bookFraction, 0.5);
    s.files = ["book.m4b"];
    assert.equal(s.bookFraction, 0.5);
});

test("skipBook crosses into previous file using from-end pending seek", () => {
    const h = harness();
    h.session.currentIndex = 1; h.mpv.position = 20; h.mpv.pause = true;
    h.session.skipBook(-60);
    assert.equal(h.session.currentIndex, 0);
    assert.deepEqual(h.loads, ["a.mp3"]);
    assert.equal(h.session.pendingResumeFromEndSec, 40);
    assert.equal(h.mpv.pause, true);
    h.loaded(600);
    assert.deepEqual(h.seeks, [560]);
    assert.equal(h.session.pendingResumeFromEndSec, -1);
    assert.equal(h.records.length, 0, "loading must not save the pre-seek zero");
});

test("skipBook clamps at recording start", () => {
    const { session: s, mpv, seeks, loads } = harness();
    mpv.position = 20;
    s.skipBook(-60);
    assert.deepEqual(seeks, [0]);
    assert.deepEqual(loads, []);
});

test("skipBook clamps at recording end and within a single file", () => {
    const { session: s, mpv, seeks, loads } = harness();
    s.currentIndex = 2; mpv.position = 580;
    s.skipBook(60);
    assert.deepEqual(seeks, [600]);
    s.files = ["book.m4b"]; s.currentIndex = 0; mpv.position = 20;
    s.skipBook(-60);
    assert.deepEqual(seeks, [600, 0]);
    assert.deepEqual(loads, []);
    assert.equal(s.jumpHistory.length, 0, "shared seek methods never mark origins");
});

test("skipBook ignores presses while a file is still loading", () => {
    const { session: s, mpv, seeks, loads } = harness();
    s.currentIndex = 1; mpv.position = 0; mpv.duration = 0;
    s.skipBook(10);
    s.skipBook(60);
    assert.deepEqual(seeks, []);
    assert.deepEqual(loads, [], "an unknown length must never read as past the end");
    mpv.duration = 600; s.pendingResumeSec = 30;
    s.skipBook(60);
    assert.deepEqual(loads, [], "a parked resume seek is still in flight");
});

test("markJumpOrigin keeps 10 and skips duplicates", () => {
    const { session: s, mpv } = harness();
    assert.equal(s.canGoBack, false);
    for (let i = 0; i < 12; ++i) { mpv.position = i * 10; s.markJumpOrigin(); }
    assert.equal(s.jumpHistory.length, 10);
    assert.equal(s.jumpHistory[0].position, 20);
    assert.equal(s.jumpHistory.at(-1).fileIndex, 0);
    assert.equal(s.jumpHistory.at(-1).position, 110);
    s.markJumpOrigin();
    assert.equal(s.jumpHistory.length, 10);
    assert.equal(s.canGoBack, true);
});

test("backToPrevious same file", () => {
    const { session: s, mpv, seeks, loads } = harness();
    s.jumpHistory = [{ fileIndex: 0, position: 120 }]; mpv.pause = true;
    s.backToPrevious();
    assert.deepEqual(seeks, [120]);
    assert.deepEqual(loads, []);
    assert.equal(s.jumpHistory.length, 0);
    assert.equal(s.canGoBack, false);
    assert.equal(mpv.pause, true);
    s.backToPrevious();
    assert.deepEqual(seeks, [120], "empty history is a no-op");
});

test("backToPrevious after completion reloads the file and parks the position", () => {
    const h = harness();
    h.session.currentIndex = 2; h.session.completed = true; h.mpv.pause = true;
    h.mpv.position = 0; h.mpv.duration = 0;
    h.session.jumpHistory = [{ fileIndex: 2, position: 300 }];
    h.session.backToPrevious();
    assert.deepEqual(h.loads, ["c.mp3"]);
    assert.equal(h.session.pendingResumeSec, 300);
    assert.equal(h.session.completed, false);
    h.loaded();
    assert.deepEqual(h.seeks, [300]);
    assert.equal(h.mpv.pause, true);
});

test("backToPrevious while a resume is pending replaces its target", () => {
    const h = harness();
    h.session.pendingResumeSec = 45;
    h.session.jumpHistory = [{ fileIndex: 0, position: 120 }];
    h.session.backToPrevious();
    assert.equal(h.session.pendingResumeSec, 120);
    assert.deepEqual(h.seeks, []);
    h.loaded();
    assert.deepEqual(h.seeks, [120]);
});

test("backToPrevious other file keeps playback state and parks even zero", () => {
    const h = harness();
    h.session.currentIndex = 2;
    h.session.jumpHistory = [{ fileIndex: 0, position: 0 }];
    h.session.backToPrevious();
    assert.deepEqual(h.loads, ["a.mp3"]);
    assert.equal(h.session.pendingResumeSec, 0);
    assert.equal(h.mpv.pause, false);
    h.loaded();
    assert.deepEqual(h.seeks, [0]);
});

test("openFor different key clears history", () => {
    const { session: s } = harness();
    s.jumpHistory = [{ fileIndex: 0, position: 12 }];
    s.openFor("recording-a", {}, true);
    assert.equal(s.jumpHistory.length, 1, "same recording keeps history");
    s.openFor("recording-b", {}, true);
    assert.equal(s.jumpHistory.length, 0);
});

test("cycleSpeed walks the ladder and wraps", () => {
    const { session: s, mpv } = harness();
    const reader = fs.readFileSync(new URL("../../qml/reader2/ReaderShell.qml", import.meta.url), "utf8");
    const expected = JSON.parse(reader.match(/audioSpeeds: (\[[^\]]+\])/)[1]);
    assert.deepEqual(Array.from(s.speedLadder), expected, "preserve ReaderShell's exact list and order");
    mpv.speed = expected[0];
    for (let i = 1; i <= expected.length; ++i) {
        s.cycleSpeed();
        assert.equal(mpv.speed, expected[i % expected.length]);
    }
    mpv.speed = 1.1; s.cycleSpeed();
    assert.equal(mpv.speed, expected[0]);
});

test("sleep expiry pauses and records once", () => {
    const h = harness();
    h.mpv.position = 328;
    const section = qml.slice(qml.indexOf("// ── sleep timer"));
    const handler = section.match(/onTriggered: \{([^}]+)\}/);
    assert.ok(handler, "engine-owned sleep expiry handler");
    h.evaluate("(function() {" + handler[1] + "})")();
    assert.equal(h.mpv.pause, true);
    assert.deepEqual(h.records, [328]);
    assert.equal(h.session.sleepMinutes, 0);
});
