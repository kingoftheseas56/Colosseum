.pragma library

function lessons() {
    var fixtures = [
        {
            id: "fixture.published", sourceIds: ["FIXTURE-START-01"], section: "start",
            title: "Fixture published lesson", outcome: "Exercise the published Guide path.",
            status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
            worlds: [], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
            evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["home"],
            searchTerms: ["guide fixture"], blocks: [], related: [], asset: ""
        },
        {
            id: "fixture.draft", sourceIds: ["FIXTURE-START-02"], section: "start",
            title: "Draft secret", outcome: "Remain unavailable until verification.",
            status: "draft", verifiedCommit: "", verifiedDate: "", order: 20,
            worlds: [], firstSupportedVersion: "", lastVerifiedVersion: "", evidence: [], openQuestions: [],
            contexts: ["home"], searchTerms: ["draft secret"], blocks: [], related: [], asset: ""
        }
    ];
    var production = [];
    return fixtures.concat(production);
}
