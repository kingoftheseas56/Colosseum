import fs from 'fs';
let src = fs.readFileSync('qml/AbbApi.js','utf8').replace(/^\.pragma library\s*$/m,'');
const mod = {};
const fn = new Function('module', src + '\nmodule.parseSearch=parseSearch;module.parseDetail=parseDetail;module.constructMagnet=constructMagnet;module.searchTerm=searchTerm;module.relevantRows=relevantRows;module.pickAudiobook=pickAudiobook;module.resolveAudiobook=resolveAudiobook;');
fn(mod);
const searchHtml = fs.readFileSync('tests/fixtures/abb/search.html','utf8');
const detailHtml = fs.readFileSync('tests/fixtures/abb/detail.html','utf8');
let rows = mod.parseSearch(searchHtml);
function fail(m){ console.log("FAIL "+m); process.exit(1); }
if (rows.length !== 7) fail("expected 7 rows, got "+rows.length+" (honeypot leak?)");
rows.forEach((r,i)=>{ if(!r.slug) fail("row "+i+" no slug"); if(!r.title) fail("row "+i+" no title"); });
if (!rows.some(r=>r.author.indexOf("Sanderson")>=0)) fail("author split");
let d = mod.parseDetail(detailHtml);
if (d.infoHash !== "1d32f8b449ebe0d63b9d08ba6e86525ff17baa3d") fail("infoHash: "+d.infoHash);
let mag = mod.constructMagnet(d.infoHash, "Rhythm of War");
if (mag.indexOf("magnet:?xt=urn:btih:"+d.infoHash)!==0) fail("magnet prefix");
if ((mag.match(/&tr=/g)||[]).length !== 7) fail("tracker count");
// searchTerm — MUST be lowercase: ABB's ?s= redirects capitalized queries to the
// HOMEPAGE (reproduced 3/3 on 2026-07-18), whose latest posts then parse as "results"
// — that's how "Joe Country" downloaded a romance novel. Lowercase never redirects.
if (mod.searchTerm("Joe Country", "Mick Herron") !== "joe country mick herron") fail("searchTerm not lowercased");
if (mod.searchTerm("Dune", "") !== "dune") fail("searchTerm author-less");
// relevantRows — the homepage-junk gate: rows sharing NO meaningful word with the
// requested book are dropped; a fully-unrelated set (the redirect failure mode)
// therefore collapses to [] and the UI says "no audiobook found" instead of
// offering strangers' uploads.
const junk = [
  { title: "Stuck-Up Suit (Series of Standalone Novels #3)", author: "Penelope Ward, Vi Keeland" },
  { title: "Endless Summer Nights (Windy Harbor #4)", author: "Willow Aster" },
  { title: "Al Clark: Thera, Book 3", author: "Jonathan G. Meyer" },
];
if (mod.relevantRows(junk, "Joe Country", "Mick Herron").length !== 0) fail("relevantRows kept homepage junk");
const mixed = junk.concat([{ title: "[Slough House 6] - Joe Country", author: "Mick Herron" }]);
const kept = mod.relevantRows(mixed, "Joe Country", "Mick Herron");
if (kept.length !== 1 || kept[0].title.indexOf("Joe Country") < 0) fail("relevantRows should keep only the real match, kept="+kept.length);
// Stopword-ONLY queries ("The Book") fall back to raw-word matching — some signal beats
// none: rows sharing a raw word survive, rows sharing nothing still gate out.
if (mod.relevantRows([{ title: "Al Clark: Thera, Book 3", author: "" }], "The Book", "").length !== 1) fail("stopword-only fallback should raw-match 'book'");
if (mod.relevantRows([{ title: "Endless Summer Nights", author: "Willow Aster" }], "The Book", "").length !== 0) fail("stopword-only fallback kept a zero-overlap row");
// The fixture's real Sanderson rows survive their own query.
if (mod.relevantRows(rows, "Rhythm of War", "Brandon Sanderson").length < 1) fail("relevantRows dropped real results");

console.log("PASS rows="+rows.length+" hash="+d.infoHash+" contents='"+d.contents+"'");
console.log("  sample: '"+rows[0].title+"' by '"+rows[0].author+"' ["+rows[0].size+", "+rows[0].format+"]");

// Arc64 L1: production pick rule plus the real XHR callback contract, offline only.
const pickBook = { title: "The Quiet River", author: "Ada North", language: "English" };
const pickRows = JSON.parse(fs.readFileSync('tests/fixtures/abb/pick-rows.json','utf8'));
function check(name, predicate) { if (!predicate) fail(name); console.log('PASS '+name); }
check('language filter', mod.pickAudiobook(pickRows, pickBook).candidates.every(r=>r.language!=='French'));
check('abridged dropped', !mod.pickAudiobook(pickRows, pickBook).candidates.some(r=>r.slug==='abridged'));
check('abridged kept when alone', mod.pickAudiobook([pickRows[2]],pickBook).pick.slug==='abridged');
check('strict title miss gives confident false', !mod.pickAudiobook([{title:'A Quiet Night',author:'Ada North'}],pickBook).confident);
check('author surname required', !mod.pickAudiobook([{title:'The Quiet River',author:'Ada West'}],pickBook).confident);
check('short title and surname', mod.pickAudiobook([{title:'It',author:'Amy Li',format:'MP3'}],{title:'It',author:'Amy Li'}).confident);
check('short surname must match', !mod.pickAudiobook([{title:'It',author:'Amy Wu'}],{title:'It',author:'Amy Li'}).confident);
check('M4B over MP3', mod.pickAudiobook([pickRows[0],pickRows[1]],pickBook).pick.slug==='m4b');
check('newest wins tie', mod.pickAudiobook([pickRows[1],{...pickRows[1],slug:'newer',posted:'2026-10-02'}],pickBook).pick.slug==='newer');
check('unknown language retained', mod.pickAudiobook([{...pickRows[1],language:''}],{...pickBook,language:''}).confident);
check('unparseable date stable', mod.pickAudiobook([{...pickRows[1],slug:'unknown',posted:'?'},pickRows[1]],pickBook).pick.slug==='unknown');
check('no input mutation', JSON.stringify(pickRows)===fs.readFileSync('tests/fixtures/abb/pick-rows.json','utf8').trim());
function lookupWith(status, body, throws=false) {
 let calls=0, result;
 globalThis.XMLHttpRequest=class { static DONE=4; open() {} setRequestHeader() {} send() {if(throws)throw new Error('offline');this.status=status;this.responseText=body;this.readyState=4;this.onreadystatechange();} };
 mod.resolveAudiobook('Quiet River','Ada North',value=>{calls++;result=value});
 delete globalThis.XMLHttpRequest;
 check('callback exactly once '+status, calls===1);
 return result;
}
check('network failure sets error true', lookupWith(0,'').error===true);
check('HTTP failure sets error true', lookupWith(503,'').error===true);
check('real empty sets error false', lookupWith(200,'<html>No results</html>').error===false);
check('empty response sets error false', lookupWith(200,'').error===false);
check('send exception sets error true', lookupWith(0,'',true).error===true);

process.exit(0);
