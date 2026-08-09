import { loadQmlJs, assertCoverage, assertNoPublishedUnverified, assertNoForbidden } from "./guide_content_contract.mjs";

// Task 6 content contract — the Start/Foundation cohort (FND-01..FND-19) distilled from Batch 1, with
// Batch 7/8 updater wording as the newer authority. Everything is Draft/Uncertain: repository evidence
// earns Draft only; only target-build verification earns Published. This gate proves coverage, the
// publication boundary, the forbidden claims, and per-record validity — before any of it can go live.
const mod = loadQmlJs("qml/guide/GuideContentStart.js", ["lessons"]);
const logic = loadQmlJs("qml/guide/GuideLogic.js", ["validateLesson", "visibleLessons"]);
const lessons = mod.lessons();

let failures = 0;
function check(cond, label) { if (!cond) { console.log("FAIL: " + label); failures++; } }

// 1) every FND source id appears exactly once (merges carry several ids on one lesson)
try {
    assertCoverage(lessons, Array.from({ length: 19 }, (_, i) => `FND-${String(i + 1).padStart(2, "0")}`));
} catch (e) { check(false, "FND coverage — " + e.message); }

// 2) no published record lacks its verification stamp
try { assertNoPublishedUnverified(lessons); } catch (e) { check(false, e.message); }

// 3) forbidden CONCEPTS never appear in the cohort (Preflight T6-F1: enforce the full contract, not just
//    literal tokens). Broad-but-false-positive-safe regexes for the cohort, bad fixtures proving each
//    fires, and ID-specific semantic guards for concepts too subtle for a safe regex (RUNNING=installed).
const FORBIDDEN = [
    /Vinyl/i,                               // no named fourth world
    /\bfourth world\b/i,                    // no fourth world under ANY name
    /Escape.*always.*Back/i,                // original literal
    /Esc(ape)?\s+is\s+(the\s+)?back\b/i,     // no unconditional Escape=Back, even without "always"
    /clear all recent searches/i,           // no unsupported global recent-search clear
    /install\s+it\s+manually/i              // no unsupported manual-install instruction
];
try { assertNoForbidden(lessons, FORBIDDEN); } catch (e) { check(false, "real cohort tripped the gate: " + e.message); }

// bad fixtures: the gate MUST reject each prohibited concept, else the guard is theatre.
const BAD_FIXTURES = [
    ["fourth world (named)",       "Vinyl is the fourth world."],
    ["fourth world (unnamed)",     "There is a fourth world for records."],
    ["Escape = Back",              "Escape is the Back key on every screen."],
    ["global recent clear",        "Use clear all recent searches to wipe them."],
    ["manual-install instruction", "Download the release from GitHub and install it manually."]
];
BAD_FIXTURES.forEach(([concept, text]) => {
    let caught = false;
    try { assertNoForbidden([{ id: "bad", blocks: [{ kind: "paragraph", text }] }], FORBIDDEN); } catch (e) { caught = true; }
    check(caught, "gate must REJECT prohibited concept: " + concept);
});

// ID-specific semantic guards — subtle concepts a safe regex cannot cover without false positives.
function lessonFor(id) { return lessons.find(l => (l.sourceIds || []).includes(id)); }
const fnd13 = lessonFor("FND-13");
check(fnd13 && /contextual/i.test(JSON.stringify(fnd13)),
    "FND-13 must teach Escape as CONTEXTUAL, not a universal Back");
const fnd18 = lessonFor("FND-18");
const fnd18Text = JSON.stringify(fnd18 || {});
check(fnd18 && /latest release/i.test(fnd18Text) && /not the build/i.test(fnd18Text),
    "FND-18 must preserve the installed-vs-latest distinction (RUNNING/release is not the installed build)");
const fnd16 = lessonFor("FND-16");
check(fnd16 && /does not[\s\S]{0,24}install/i.test(JSON.stringify(fnd16)),
    "FND-16/17 must NOT promise a manual-install action (Manual update required installs nothing)");
const worlds = lessonFor("FND-01");
const worldsText = JSON.stringify(worlds || {});
check(/tankoban/i.test(worldsText) && /biblio/i.test(worldsText) && /theatre/i.test(worldsText),
    "FND-01/02 must name exactly the three worlds (Tankoban, Biblio, Theatre)");

// 4) every record is well-formed under the shared lesson validator
lessons.forEach(l => {
    const errs = logic.validateLesson(l);
    check(errs.length === 0, "valid lesson " + (l && l.id) + ": " + errs.join(","));
});

// 5) the publication boundary: NO FND lesson is production-visible (none is Published/verified yet),
//    and no planned/uncertain lesson leaks into the visible set.
const visible = logic.visibleLessons(lessons);
const fndVisible = visible.filter(l => (l.sourceIds || []).some(id => /^FND-/.test(id)));
check(fndVisible.length === 0, "no FND lesson is Published/visible yet (Draft/Uncertain until target-build verification)");
check(!visible.some(l => l.status === "planned" || l.status === "uncertain"), "no planned/uncertain lesson is visible");

// 6) the two genuinely-uncertain foundation records stay Uncertain (installed version, exhaustive history)
["FND-18", "FND-19"].forEach(id => {
    const l = lessons.find(x => (x.sourceIds || []).includes(id));
    check(!!l, id + " record present");
    check(l && l.status === "uncertain", id + " remains Uncertain until product/runtime resolves it");
});

console.log(failures === 0 ? "guide_content_start_test: ALL PASS" : ("guide_content_start_test: " + failures + " FAIL"));
process.exit(failures === 0 ? 0 : 1);
