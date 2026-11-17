import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";

const baseline = process.argv.includes("--baseline");
const qml = baseline
    ? execFileSync("git",["show","HEAD:qml/AudiobookSession.qml"],{encoding:"utf8"})
    : fs.readFileSync(new URL("../../qml/AudiobookSession.qml",import.meta.url),"utf8");
function qmlFunction(name) {
    const rx = new RegExp("^    function "+name+"\\(([^)]*)\\) \\{\\r?\\n([\\s\\S]*?)^    \\}", "m");
    const match = qml.match(rx);
    assert.ok(match, name);
    return "(function("+match[1]+") {\n"+match[2]+"    })";
}
const work = "book|author";
const a = work+"#src:"+"a".repeat(40);
const b = work+"#src:"+"b".repeat(40);
let selected = a;
const saved = new Map([
    [a,{resume:{fileIndex:0,position:40}}],
    [b,{resume:{fileIndex:0,position:15}}]
]);
const reads = [];
const mpv = {
    position:0,duration:100,pause:true,loadSource(path) { this.loaded=path; }
};
const Audiobooks = {
    resolveRecordingKey(key) { return key===work ? selected : key; },
    localFiles(key) { return key===a?["a.mp3"]:key===b?["b.mp3"]:[]; },
    markRecordingPlayed(key) { this.played=key; return true; }
};
const Progress = {
    get(kind,key) { assert.equal(kind,"audiobook");reads.push(key);return saved.get(key); },
    record(entry) { saved.set(entry.id,entry); }
};
const session = {
    ready:false,completed:false,activePairKey:"",book:{},files:[],currentIndex:0,
    pendingResumeSec:-1,audioHandoffPaused:false,
    rebuildChapters() {}, activityBeginIfNeeded() {}, requestAudioPlayback() {},
    overallProgress() { return this.currentIndex+(mpv.position/100); },
    recordProgress() {}
};
const context={session,Audiobooks,Progress,mpv,Math};
session.recordProgress=vm.runInNewContext(qmlFunction("recordProgress"),context);
session.openFor=vm.runInNewContext(qmlFunction("openFor"),context);

session.openFor(work,{title:"Book",author:"Author",pairKey:work},true);
assert.equal(session.activePairKey,a,"bare work key resolves to recording A");
assert.equal(session.pendingResumeSec,40);
assert.equal(mpv.loaded,"a.mp3");
assert.equal(Audiobooks.played,a);
session.pendingResumeSec=-1;
mpv.position=58;
selected=b;
session.openFor(work,{title:"Book",author:"Author",pairKey:work},true);
assert.equal(saved.get(a).resume.position,58,"leaving A persists A");
assert.equal(saved.get(a).resume.recordingKey,a);
assert.equal(saved.get(a).resume.pairKey,work);
assert.equal(session.activePairKey,b);
assert.equal(session.pendingResumeSec,15,"B resumes from its own saved position");
session.pendingResumeSec=-1;
mpv.position=31;
session.recordProgress();
assert.equal(saved.get(a).resume.position,58,"B never overwrites A");
assert.equal(saved.get(b).resume.position,31);
selected=a;
session.openFor(work,{title:"Book",author:"Author",pairKey:work},true);
assert.equal(session.pendingResumeSec,58,"A resumes after B");
assert.deepEqual(reads,[a,b,a]);
selected="";
session.openFor(work,{title:"Book",pairKey:work},true);
assert.equal(session.activePairKey,a,"ambiguous work leaves the active recording untouched");
console.log("PASS: 13 recording-key playback, resume, progress and ambiguity assertions"+(baseline?" (baseline)":""));
