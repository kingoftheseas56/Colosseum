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

// 3) forbidden claims never appear anywhere in the cohort
try {
    assertNoForbidden(lessons, [/Vinyl/i, /Escape.*always.*Back/i, /clear all recent searches/i]);
} catch (e) { check(false, e.message); }

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
