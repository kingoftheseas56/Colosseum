import { loadQmlJs, assertCoverage, assertNoPublishedUnverified, assertNoForbidden } from "./guide_content_contract.mjs";

// Biblio content contract — the acquisition + Library cohort (BIB-01..07 + BIB-23) distilled from
// Batch 3. Every record is Draft (BIB-06 is Uncertain): repository evidence earns Draft/Uncertain
// only; only target-build verification earns Published. This gate proves coverage, the publication
// boundary, the forbidden claims, and per-record validity — before any of it can go live.
const mod = loadQmlJs("qml/guide/GuideContentBiblio.js", ["lessons"]);
const logic = loadQmlJs("qml/guide/GuideLogic.js", ["validateLesson", "visibleLessons"]);
const lessons = mod.lessons();

let failures = 0;
function check(cond, label) { if (!cond) { console.log("FAIL: " + label); failures++; } }

// 1) every BIB source id appears exactly once (merges carry several ids on one lesson; BIB-08..22
//    are reader/audiobook lessons deliberately out of scope for this handoff).
try {
    assertCoverage(lessons, ["BIB-01", "BIB-02", "BIB-03", "BIB-04", "BIB-05", "BIB-06", "BIB-07", "BIB-23"]);
} catch (e) { check(false, "BIB coverage — " + e.message); }

// 2) no published record lacks its verification stamp (nothing draft is masquerading as published).
try { assertNoPublishedUnverified(lessons); } catch (e) { check(false, e.message); }

// 3) the Batch-3 forbidden claims never appear anywhere in the records. Each pattern encodes a
//    specific overreach the packet's "Do not claim" list forbids.
const FORBIDDEN = [
    /(PDF|MOBI|AZW3?|DJVU|FB2).{0,20}(supported|readable|can be read)/i, // badge parses names — not a support matrix
    /all.{0,20}acquisition.{0,20}(inside|within) Colosseum|acquisition.{0,20}stays? (inside|in-app)/i, // URL-only rows route externally
    /(use|switch to|open|show|filter by|enable) (the )?Downloaded (filter|tab)/i, // Downloaded filter is deliberately absent — catch the false instruction, not the negation
    /(more |higher )?seeders? guarantee/i,                                 // seeder count never guarantees success
    /canonical edition|best edition/i,                                     // no canonical/best edition exists
    /\bListen button\b/i,                                                  // standalone Listen retired — audio is the reader's Audio surface
    /\brename.{0,20}extension/i                                            // renaming an extension is never a format conversion/fix
];
try { assertNoForbidden(lessons, FORBIDDEN); } catch (e) { check(false, "real cohort tripped the gate: " + e.message); }

// bad fixtures: the gate MUST reject each prohibited concept, else the guard is theatre.
const BAD_FIXTURES = [
    ["format-support overreach",     "PDF is supported by the reader."],
    ["format-readable overreach",    "MOBI files are readable here."],
    ["all-acquisition-inside claim", "All acquisition stays inside Colosseum."],
    ["in-app-acquisition claim",     "Acquisition stays in-app and never leaves."],
    ["Downloaded filter",            "Use the Downloaded filter to see local books."],
    ["Downloaded tab",               "Open the Downloaded tab."],
    ["seeders guarantee",            "More seeders guarantee a faster download."],
    ["canonical edition",            "Pick the canonical edition for best results."],
    ["best edition",                 "The best edition is highlighted."],
    ["Listen button",                "Tap the Listen button on the book page."],
    ["rename extension",             "Rename the extension to epub to fix it."]
];
BAD_FIXTURES.forEach(([concept, text]) => {
    let caught = false;
    try { assertNoForbidden([{ id: "bad", blocks: [{ kind: "paragraph", text }] }], FORBIDDEN); } catch (e) { caught = true; }
    check(caught, "gate must REJECT prohibited concept: " + concept);
});

// 4) every record is well-formed under the shared lesson validator.
lessons.forEach(l => {
    const errs = logic.validateLesson(l);
    check(errs.length === 0, "valid lesson " + (l && l.id) + ": " + errs.join(","));
});

// 5) the publication boundary: NO BIB lesson is production-visible yet (none is Published/verified),
//    and no planned/uncertain lesson leaks into the visible set.
const visible = logic.visibleLessons(lessons);
const bibVisible = visible.filter(l => (l.sourceIds || []).some(id => /^BIB-/.test(id)));
check(bibVisible.length === 0, "no BIB lesson is Published/visible yet (Draft/Uncertain until target-build verification)");
check(!visible.some(l => l.status === "planned" || l.status === "uncertain"), "no planned/uncertain lesson is visible");

// 6) BIB-06 stays Uncertain: the format lesson must advertise the labels WITHOUT publishing a
//    support matrix, and must carry the three-way listed-vs-downloadable-vs-openable distinction.
const bib06 = lessons.find(l => (l.sourceIds || []).includes("BIB-06"));
const bib06Text = JSON.stringify(bib06 || {});
check(bib06 && bib06.status === "uncertain", "BIB-06 remains Uncertain until each format is runtime-exercised");
check(/EPUB/.test(bib06Text) && /PDF/.test(bib06Text), "BIB-06 advertises the format labels the picker shows");
check(/not the same as opening successfully|not proof the reader can open/i.test(bib06Text),
    "BIB-06 must separate listed/downloadable/openable (no confirmed support)");
check(/not as a tested guarantee/i.test(bib06Text),
    "BIB-06 must frame the labels as what the source named, NOT a tested guarantee of what opens");

// 7) BIB-23 is an absence: the Library lesson must state plainly that no Downloaded filter exists,
//    and the cohort must carry the three distinct concepts (Library / Continue / local copy).
const bib07 = lessons.find(l => (l.sourceIds || []).includes("BIB-07") && (l.sourceIds || []).includes("BIB-23"));
const bib07Text = JSON.stringify(bib07 || {});
check(bib07, "BIB-07 and BIB-23 merge into one Library record");
check(/no Downloaded filter/i.test(bib07Text), "BIB-23 absence: the Library lesson states there is no Downloaded filter");
check(/Continue/i.test(bib07Text) && /local copy/i.test(bib07Text),
    "BIB-07 distinguishes Library membership, Continue, and a downloaded local copy");

console.log(failures === 0 ? "guide_content_biblio_test: ALL PASS" : ("guide_content_biblio_test: " + failures + " FAIL"));
process.exit(failures === 0 ? 0 : 1);
