.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.theatre", sourceIds: ["FIXTURE-THEATRE-01"], section: "theatre",
        title: "Theatre fixture lesson", outcome: "Exercise a published Theatre catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["theatre"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["theatre"],
        searchTerms: ["theatre fixture"], blocks: [], related: [], asset: ""
    }];
    var production = [];
    return fixtures.concat(production);
}
