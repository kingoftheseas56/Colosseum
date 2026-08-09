.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.tankoban", sourceIds: ["FIXTURE-TANKOBAN-01"], section: "tankoban",
        title: "Tankoban fixture lesson", outcome: "Exercise a published Tankoban catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["tankoban"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["tankoban"],
        searchTerms: ["tankoban fixture"], blocks: [], related: [], asset: ""
    }];
    var production = [];
    return fixtures.concat(production);
}
