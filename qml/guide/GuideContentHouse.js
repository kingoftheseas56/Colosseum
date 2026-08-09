.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.house", sourceIds: ["FIXTURE-HOUSE-01"], section: "house",
        title: "House fixture lesson", outcome: "Exercise a published House catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["house"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["house"],
        searchTerms: ["house fixture"], blocks: [], related: [], asset: ""
    }];
    var production = [];
    return fixtures.concat(production);
}
