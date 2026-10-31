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
