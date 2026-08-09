import { loadQmlJs } from "./guide_content_contract.mjs";

const mod = loadQmlJs("qml/guide/GuideLogic.js", [
    "validateLesson", "visibleLessons", "lessonById", "search", "lessonsForSection", "relatedLessons"
]);

let failures = 0;
function check(condition, label) {
    if (!condition) {
        console.log("FAIL: " + label);
        failures++;
    }
}
function ids(items) { return items.map(item => item.id).join(","); }

function lesson(overrides) {
    return Object.assign({
        id: "open-guide", sourceIds: ["FND-01"], section: "start", title: "Open the Guide",
        outcome: "Find help.", status: "published", verifiedCommit: "abc", verifiedDate: "2026-08-09",
        order: 10, worlds: [], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/fixture"], openQuestions: [], contexts: ["home"], searchTerms: ["help"],
        blocks: [], related: [], asset: ""
    }, overrides || {});
}

const published = lesson();
const draft = lesson({
    id: "first-run-sources", sourceIds: ["EXT-02"], section: "downloads", title: "First-run sources",
    outcome: "Understand source setup.", status: "draft", verifiedCommit: "", verifiedDate: "",
    order: 20, contexts: ["extensions"], searchTerms: ["sources"], evidence: []
});
const contextPeer = lesson({
    id: "help-elsewhere", sourceIds: ["FND-02"], section: "start", title: "Guidance elsewhere",
    outcome: "Find help.", order: 30, contexts: ["extensions"], searchTerms: ["help"]
});
const punctuationTitle = lesson({
    id: "safe-start", sourceIds: ["FND-03"], section: "start", title: "Safe Start: Downloads",
    outcome: "Choose a safe download route.", order: 40, contexts: ["home"], searchTerms: ["first run"]
});
const related = lesson({
    id: "next-steps", sourceIds: ["FND-04"], section: "start", title: "Next steps",
    outcome: "Continue after setup.", order: 50, related: ["open-guide", "first-run-sources", "missing", "open-guide"]
});
const tiesFirst = lesson({
    id: "tie-first", sourceIds: ["FND-05"], section: "start", title: "Archive basics",
    outcome: "Learn the archive.", order: 60, contexts: ["home"], searchTerms: []
});
const tiesSecond = lesson({
    id: "tie-second", sourceIds: ["FND-06"], section: "start", title: "Archive basics",
    outcome: "Learn the archive.", order: 70, contexts: ["home"], searchTerms: []
});
const titleWord = lesson({
    id: "title-word", sourceIds: ["FND-07"], section: "start", title: "Open guide safely",
    outcome: "Follow the route.", order: 80, contexts: ["home"], searchTerms: []
});
const titlePrefix = lesson({
    id: "title-prefix", sourceIds: ["FND-08"], section: "start", title: "Guidebook orientation",
    outcome: "Follow the route.", order: 90, contexts: ["home"], searchTerms: []
});
const titleExact = lesson({
    id: "title-exact", sourceIds: ["FND-09"], section: "start", title: "Guide",
    outcome: "Follow the route.", order: 100, contexts: ["home"], searchTerms: []
});
const bodyOnly = lesson({
    id: "body-only", sourceIds: ["FND-10"], section: "start", title: "Recovery checklist",
    outcome: "Store the kit.", order: 110, contexts: ["home"], searchTerms: [],
    blocks: [{ type: "paragraph", body: "Keep the lantern lit." }]
});
const lessons = [published, draft, contextPeer, punctuationTitle, related, tiesFirst, tiesSecond];
const rankingLessons = [titleWord, titlePrefix, titleExact];

// Break caught: omitting any lifecycle check lets malformed or unverified lessons enter a catalog.
check(mod.validateLesson(published).length === 0, "valid lesson passes lifecycle validation");
check(mod.validateLesson(null).join(",") === "missing id,missing section,missing title,missing outcome,missing status",
    "null lesson reports every required text field without throwing");
check(mod.validateLesson(lesson({ id: "  ", section: "", title: "", outcome: "", status: "unknown", order: "10", worlds: null, evidence: null })).join(",") ===
    "missing id,missing section,missing title,missing outcome,missing order,missing worlds,missing evidence,invalid status",
    "blank fields, invalid status, and malformed structural fields are rejected");
check(mod.validateLesson(lesson({ verifiedCommit: "", verifiedDate: "" })).includes("published lesson lacks verification"),
    "published lesson requires both verification fields");
check(mod.validateLesson(lesson({ status: "verified", verifiedCommit: "", verifiedDate: "" })).length === 0,
    "only published status requires verification metadata");

// Break caught: changing the publication filter leaks a draft into a production navigation surface.
check(ids(mod.visibleLessons(lessons)) === "open-guide,help-elsewhere,safe-start,next-steps,tie-first,tie-second",
    "visible lessons include only published records in catalog order");
check(mod.visibleLessons(null).length === 0, "visible lessons tolerates an absent catalog");

// Break caught: lookup or section filtering that bypasses the publication gate exposes draft content.
check(mod.lessonById(lessons, "open-guide") === published, "lookup returns the matching published lesson");
check(mod.lessonById(lessons, "first-run-sources") === null, "lookup never returns a draft lesson");
check(mod.lessonById(lessons, "missing") === null, "lookup returns null for an unknown id");
check(ids(mod.lessonsForSection(lessons, "start")) === "open-guide,help-elsewhere,safe-start,next-steps,tie-first,tie-second",
    "section lookup returns only published records in catalog order");
check(mod.lessonsForSection(lessons, "downloads").length === 0, "section lookup never returns a draft");

// Break caught: related resolution that ignores publication, missing IDs, or duplicate IDs shows invalid navigation.
check(ids(mod.relatedLessons(lessons, related)) === "open-guide",
    "related lookup keeps declared order while removing draft, missing, and duplicate records");
check(mod.relatedLessons(lessons, null).length === 0, "related lookup tolerates an absent lesson");

// Break caught: scoring that skips title precedence, normalization, context bonus, or stable ties returns the wrong help result.
check(mod.search(lessons, "help", "home")[0].id === "open-guide", "search ranks a published context match ahead of an equal outcome match");
check(mod.search(lessons, "sources", "extensions").length === 0, "search never leaks a draft");
check(mod.search(lessons, "safe-start downloads", "home")[0].id === "safe-start", "search normalizes punctuation in title queries");
check(mod.search(lessons, "first run", "home")[0].id === "safe-start", "search matches normalized search terms");
check(ids(mod.search(lessons, "archive basics", "home").slice(0, 2)) === "tie-first,tie-second",
    "equal scores preserve catalog order");
check(ids(mod.search(rankingLessons, "guide", "home")) === "title-exact,title-prefix,title-word",
    "search ranks exact title, title prefix, then complete title-word matches");
check(ids(mod.search([bodyOnly], "lantern", "home")) === "body-only",
    "search returns a lesson matched only by body content");
check(mod.search([punctuationTitle], "art", "home").length === 0,
    "search does not treat a substring inside a title word as a title-word match");
check(mod.search(lessons, "", "home").length === 0, "empty search query returns no results");
check(mod.search(null, "help", "home").length === 0, "search tolerates an absent catalog");

if (failures) {
    console.log("guide_logic_test: FAILS=" + failures);
    process.exit(1);
}
console.log("guide_logic_test: ALL PASS");
