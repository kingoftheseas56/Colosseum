import { loadQmlJs, assertCoverage, assertNoPublishedUnverified, assertNoForbidden } from "./guide_content_contract.mjs";

// House content contract — the Vault/local-media cohort (VLT-01..VLT-09, Batch 5), the Downloads
// cohort (DLD-01..DLD-15, Batch 6), and the Extensions & sources cohort (EXT-01..EXT-10, Batch 6).
// Every record is Draft (EXT-02/EXT-09 are Uncertain): repository evidence earns Draft/Uncertain
// only; only target-build verification earns Published. This gate proves coverage, the publication
// boundary, both batches' "Do not claim" lists, and per-record validity — before any of it can go
// live.
const mod = loadQmlJs("qml/guide/GuideContentHouse.js", ["lessons"]);
const logic = loadQmlJs("qml/guide/GuideLogic.js", ["validateLesson", "visibleLessons"]);
const lessons = mod.lessons();

let failures = 0;
function check(cond, label) { if (!cond) { console.log("FAIL: " + label); failures++; } }

// 1) Coverage: every VLT-01..09, DLD-01..15, and EXT-01..10 source id appears exactly once across
//    the records (merges carry several ids on one lesson).
try {
    assertCoverage(lessons, [
        "VLT-01", "VLT-02", "VLT-03", "VLT-04", "VLT-05", "VLT-06", "VLT-07", "VLT-08", "VLT-09",
        "DLD-01", "DLD-02", "DLD-03", "DLD-04", "DLD-05", "DLD-06", "DLD-07", "DLD-08", "DLD-09",
        "DLD-10", "DLD-11", "DLD-12", "DLD-13", "DLD-14", "DLD-15",
        "EXT-01", "EXT-02", "EXT-03", "EXT-04", "EXT-05", "EXT-06", "EXT-07", "EXT-08", "EXT-09", "EXT-10"
    ]);
} catch (e) { check(false, "coverage — " + e.message); }

// 2) No published record lacks its verification stamp (nothing draft is masquerading as published).
try { assertNoPublishedUnverified(lessons); } catch (e) { check(false, e.message); }

// 3) The Batch-5, Batch-6 (Downloads), and Batch-6 (Extensions) forbidden claims never appear.
//    Each pattern is claim-targeted: it matches the actionable overreach ("use the Downloaded
//    filter"), not the mere mention or a negation ("there is no Downloaded filter").
const FORBIDDEN = [
    // Batch 5 — Vault/local-media
    /CBR is (fully )?supported/i,               // CBR accepted by extension only, never "supported"
    /codec pack/i,                              // no codec-pack requirement or fix
    /rename.*extension/i,                       // extension renaming is never a conversion or fix
    /app repair/i,                              // no generic repair function exists
    /delete.*app data/i,                        // deleting app data is never a fix
    /drag.*folder.*(vault|add folder)/i,        // folder drag/drop never equals Add Folder / Vault
    /clear.*(delete|erase).*(file|progress|continue)/i,  // Clear never deletes files or progress
    // Batch 6 — Downloads
    /free.?space (meter|indicator|monitor)/i,   // no free-space meter/indicator/monitor exists
    /Colosseum.*shows.*free space/i,            // Colosseum never "shows free space"
    /reorder.*(download|queue)|queue.*(priority|reorder)|(speed|bandwidth|concurren\w*).*limit/i, // no queue reorder / speed-bandwidth-concurrency limit controls
    /(download|storage).*(folder|location).*(open|choose|change|set)/i, // no user-facing download/storage location control
    /(colosseum )?(seeds|seeding).*(control|ratio|stop|toggle)/i, // no seeding claim or seeding control
    /Play.*(means|is).*(offline|complete|finished|ready)/i, // Play while arriving is never offline-ready/completed
    /Cancel.*(keeps|preserves|retains).*partial/i, // Cancel deletes partial data; it never keeps it
    // Batch 6 — Extensions & sources
    /behav\w* identically/i,                    // no world behaves identically to another
    /every extension (works|behaves).{0,40}(all three|every world)/i, // no every-extension-works-everywhere claim
    /every installed extension (is|provides|acts as).{0,30}(acquisition|source|download)/i, // installed ≠ acquisition source
    /(reinstalling|reinstall).{0,30}(fix|fixes|repair)/i, // reinstall is never a universal/first-line fix
    /pasted (url|link).{0,30}(installed|installs)/i, // a pasted URL is never installed before preview
    /(reveal|show|unlock).{0,25}adult|adult.{0,25}(reveal|show|unlock)/i, // no adult-reveal setting exists
    /opens? (a |the )?settings (sheet|page)/i,  // no Settings label opens a settings sheet
    /settings (sheet|page) (opens|works|is available)/i, // no functional settings sheet exists
    /(disabling|disable).{0,25}(deletes|removes|erases|wipes)/i, // disabling is never destructive
    /removing.{0,40}(deletes|removes|erases) (downloaded )?(media|files?)/i, // removing never deletes media
    /top(-| )ranked.{0,30}(always|best|guarantee)/i // top rank never guarantees the best result
];
try { assertNoForbidden(lessons, FORBIDDEN); } catch (e) { check(false, "real cohort tripped the gate: " + e.message); }

// bad fixtures: the gate MUST reject each prohibited concept, else the guard is theatre.
const BAD_FIXTURES = [
    ["every-extension-identical",  "Every extension works in all three worlds."],
    ["behaves-identically",        "Extensions behave identically across worlds."],
    ["every-installed-is-source",  "Every installed extension is an acquisition source."],
    ["reinstall-as-fix",           "Reinstalling the extension fixes it."],
    ["paste-installs",             "A pasted URL installs immediately."],
    ["adult-setting",              "A setting can reveal adult extensions."],
    ["settings-sheet-opens",       "The Settings label opens a settings sheet."],
    ["disable-deletes",            "Disabling deletes the extension's configuration."],
    ["remove-deletes-media",       "Removing an extension deletes downloaded media."],
    ["top-ranked-guarantee",       "The top-ranked source always succeeds."]
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

// 5) the publication boundary: NO House lesson is production-visible yet (none is Published/
//    verified), and no planned/uncertain lesson leaks into the visible set.
const visible = logic.visibleLessons(lessons);
const houseVisible = visible.filter(l => (l.sourceIds || []).some(id => /^(VLT|DLD|EXT)-/.test(id)));
check(houseVisible.length === 0, "no VLT/DLD/EXT lesson is Published/visible yet (Draft/Uncertain until target-build verification)");
check(!visible.some(l => l.status === "planned" || l.status === "uncertain"), "no planned/uncertain lesson is visible");

// 6) EXT-02 stays Uncertain and neutral: the first-run conflict must be stated as unresolved,
//    never asserting either "fresh installs have sources enabled" or "no sources enabled".
const ext02 = lessons.find(l => (l.sourceIds || []).includes("EXT-02"));
const ext02Text = JSON.stringify(ext02 || {});
check(ext02 && ext02.status === "uncertain", "EXT-02 remains Uncertain (first-run conflict unresolved)");
check(/not yet resolved/i.test(ext02Text), "EXT-02 states the first-run conflict is unresolved");
check(!/fresh installs (start|begin|come|have)/i.test(ext02Text),
    "EXT-02 asserts neither first-run default (neutral while the conflict is open)");

// 7) EXT-09 stays Uncertain: the built-in Settings label must not be presented as a working
//    surface, and the exact notice wording is carried.
const ext09 = lessons.find(l => (l.sourceIds || []).includes("EXT-09"));
const ext09Text = JSON.stringify(ext09 || {});
check(ext09 && ext09.status === "uncertain", "EXT-09 remains Uncertain (built-in settings not landed)");
check(/does not open/i.test(ext09Text), "EXT-09 does not claim the Settings label opens a surface");
check(/settings arrive with the indexer sheet/i.test(ext09Text), "EXT-09 carries the exact notice wording");

console.log(failures === 0 ? "guide_content_house_test: ALL PASS" : ("guide_content_house_test: " + failures + " FAIL"));
process.exit(failures === 0 ? 0 : 1);
