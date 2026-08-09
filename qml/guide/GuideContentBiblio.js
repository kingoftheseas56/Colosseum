.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.biblio", sourceIds: ["FIXTURE-BIBLIO-01"], section: "biblio",
        title: "Biblio fixture lesson", outcome: "Exercise a published Biblio catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["biblio"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["biblio"],
        searchTerms: ["biblio fixture"], blocks: [], related: [], asset: ""
    }];
    var production = [];
    return fixtures.concat(production);
}
