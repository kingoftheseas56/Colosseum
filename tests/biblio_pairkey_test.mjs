import fs from 'fs';
// BiblioApi.js is a .pragma library of pure JS; pairKey/fullAudiobook never call XMLHttpRequest.
let src = fs.readFileSync('qml/BiblioApi.js','utf8').replace(/^\.pragma library\s*$/m,'');
const mod = {};
new Function('module', src + '\nmodule.pairKey=pairKey;module.fullAudiobook=fullAudiobook;')(mod);
function fail(m){ console.log("FAIL "+m); process.exit(1); }
const pk = mod.pairKey;
// THE review-flagged seam: audiobook entity "(Unabridged)" must pair with the bare ebook title.
if (pk("Dune (Unabridged)","Frank Herbert") !== pk("Dune","Frank Herbert")) fail("unabridged suffix breaks pairing: "+pk("Dune (Unabridged)","Frank Herbert")+" vs "+pk("Dune","Frank Herbert"));
if (pk("Project Hail Mary (Unabridged)","Andy Weir") !== pk("Project Hail Mary","Andy Weir")) fail("PHM unabridged");
if (pk("Gone Girl: A Novel","Gillian Flynn") !== pk("Gone Girl","Gillian Flynn")) fail(": A Novel tail");
// case/whitespace/punct still fold
if (pk("  DUNE ","frank  herbert!") !== pk("Dune","Frank Herbert")) fail("case/space fold");
if (pk("Crime & Punishment","X") !== pk("crime and punishment","X")) fail("ampersand fold");
// DISTINCT titles must NOT collapse (guard against over-stripping)
if (pk("Dune","Frank Herbert") === pk("Dune Messiah","Frank Herbert")) fail("over-collapse: Dune == Dune Messiah");
// fullAudiobook sets a pairKey equal to pairKey(collectionName, artistName), and it matches the bare ebook
const ab = mod.fullAudiobook({ collectionName:"Dune (Unabridged)", artistName:"Frank Herbert", description:"x", artworkUrl100:"https://x/100x100bb.jpg" });
if (ab.pairKey !== pk("Dune","Frank Herbert")) fail("fullAudiobook.pairKey != ebook key: "+ab.pairKey);
console.log("PASS pairKey collapses (unabridged/: A Novel/case/&), keeps distinct titles apart");
console.log("  Dune(Unabridged) -> '"+pk("Dune (Unabridged)","Frank Herbert")+"'  Dune Messiah -> '"+pk("Dune Messiah","Frank Herbert")+"'");
process.exit(0);
