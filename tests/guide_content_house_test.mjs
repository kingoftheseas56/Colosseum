import { loadQmlJs, assertCoverage, assertNoPublishedUnverified, assertNoForbidden } from "./guide_content_contract.mjs";

// House content contract — the Vault/local-media cohort (VLT-01..VLT-09) distilled from Batch 5.
// Every record is Draft: repository evidence earns Draft only; only target-build verification earns
// Published. This gate proves coverage, the publication boundary, and the Batch-5 "Do not claim"
// list — before any of it can go live.
const lessons = loadQmlJs("qml/guide/GuideContentHouse.js", ["lessons"]).lessons();

let failures = 0;
function check(cond, label) { if (!cond) { console.log("FAIL: " + label); failures++; } }

// 1) Coverage: every VLT-01..09 source id appears exactly once across the records (merges carry
//    several ids on one lesson).
try {
    assertCoverage(lessons, ["VLT-01", "VLT-02", "VLT-03", "VLT-04", "VLT-05", "VLT-06", "VLT-07", "VLT-08", "VLT-09"]);
} catch (e) { check(false, "VLT coverage — " + e.message); }

// 2) No published record lacks its verification stamp (nothing draft is masquerading as published).
try { assertNoPublishedUnverified(lessons); } catch (e) { check(false, e.message); }

// 3) The Batch-5 forbidden claims never appear anywhere in the records.
try {
    assertNoForbidden(lessons, [
        /CBR is (fully )?supported/i,               // CBR accepted by extension only, never "supported"
        /codec pack/i,                              // no codec-pack requirement or fix
        /rename.*extension/i,                       // extension renaming is never a conversion or fix
        /app repair/i,                              // no generic repair function exists
        /delete.*app data/i,                        // deleting app data is never a fix
        /drag.*folder.*(vault|add folder)/i,        // folder drag/drop never equals Add Folder / Vault
        /clear.*(delete|erase).*(file|progress|continue)/i  // Clear never deletes files or progress
    ]);
} catch (e) { check(false, "real cohort tripped the gate: " + e.message); }

console.log(failures === 0 ? "guide_content_house_test: ALL PASS" : ("guide_content_house_test: " + failures + " FAIL"));
process.exit(failures === 0 ? 0 : 1);
