#include "VinylContextGraph.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

template <typename Id>
Id mustId(const char *text)
{
    const auto parsed = Id::parse(QString::fromLatin1(text));
    require(parsed.ok(), "fixture MBID must parse");
    return *parsed.value;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    using namespace vinyl;
    using namespace vinyl::context;

    const auto recording =
        mustId<RecordingMbid>("11111111-1111-4111-8111-111111111111");
    const auto artist =
        mustId<ArtistMbid>("22222222-2222-4222-8222-222222222222");
    const auto work =
        mustId<WorkMbid>("33333333-3333-4333-8333-333333333333");

    ContextGraph graph;
    const ArtistRoleCredit artistCredit{
        artist,
        QStringLiteral("The Credited Name"),
        {{CreditRoleKind::PrimaryArtist, QStringLiteral("artist")}},
    };

    graph.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording),
        CanonicalEntityRef::from(artist),
        RelationshipKind::ArtistCredit,
        RelationshipDirectionality::Directed,
        {artistCredit},
        QStringLiteral("artist"),
    });
    graph.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording),
        CanonicalEntityRef::from(artist),
        RelationshipKind::Performer,
        RelationshipDirectionality::Directed,
        {{artist,
          QStringLiteral("The Credited Name"),
          {{CreditRoleKind::Performer, QStringLiteral("instrument")}}}},
        QStringLiteral("performer"),
    });

    require(graph.relationships().size() == 2,
            "same entity pair may participate in multiple relationship kinds");
    require(graph.relationships().at(0).kind == RelationshipKind::ArtistCredit
                && graph.relationships().at(1).kind == RelationshipKind::Performer,
            "relationship kind must remain explicit per edge");

    graph.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording),
        CanonicalEntityRef::from(work),
        RelationshipKind::PerformanceOf,
        RelationshipDirectionality::Directed,
        {},
        QStringLiteral("performance"),
    });
    const ContextRelationship &performance = graph.relationships().at(2);
    require(performance.source == CanonicalEntityRef::from(recording),
            "directed relationship must preserve source");
    require(performance.target == CanonicalEntityRef::from(work),
            "directed relationship must preserve target");
    require(performance.directionality == RelationshipDirectionality::Directed,
            "relationship directionality must survive graph insertion");

    const auto sharedRecording =
        mustId<RecordingMbid>("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto sharedWork =
        mustId<WorkMbid>("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const CanonicalEntityRef recordingRef = CanonicalEntityRef::from(sharedRecording);
    const CanonicalEntityRef workRef = CanonicalEntityRef::from(sharedWork);
    require(recordingRef != workRef,
            "Work identity must never collapse into Recording identity");

    ContextGraph sameUuidGraph;
    sameUuidGraph.addNode(recordingRef);
    sameUuidGraph.addNode(workRef);
    require(sameUuidGraph.nodes().size() == 2,
            "graph node key must include canonical entity kind");

    const ContextRelationship &credited = graph.relationships().at(0);
    require(credited.artistCredits.size() == 1,
            "artist relationship should retain structured credit metadata");
    require(credited.artistCredits.at(0).artistMbid == artist,
            "credited artist identity must remain ArtistMbid");
    require(credited.artistCredits.at(0).creditedName
                == QStringLiteral("The Credited Name"),
            "credited name must remain metadata separate from ArtistMbid");
    require(credited.target == CanonicalEntityRef::from(artist),
            "credited name must never replace canonical ArtistMbid endpoint");

    require(relationshipKindName(RelationshipKind::PerformanceOf)
                == QStringLiteral("performance_of"),
            "relationship kinds should have stable names");
    require(relationshipDirectionalityName(RelationshipDirectionality::Directed)
                == QStringLiteral("directed"),
            "directionality should have stable names");
    require(creditRoleKindName(CreditRoleKind::Producer)
                == QStringLiteral("producer"),
            "credit roles should have stable names");

    const auto releaseGroup =
        mustId<ReleaseGroupMbid>("44444444-4444-4444-8444-444444444444");
    const auto releaseOne =
        mustId<ReleaseMbid>("55555555-5555-4555-8555-555555555555");
    const auto releaseTwo =
        mustId<ReleaseMbid>("66666666-6666-4666-8666-666666666666");

    ContextGraph versions;
    versions.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(releaseGroup),
        CanonicalEntityRef::from(releaseTwo),
        RelationshipKind::ReleaseVersion,
        RelationshipDirectionality::Directed,
        {},
        QStringLiteral("release"),
        {{QStringLiteral("musicbrainz"), QStringLiteral("rel-2"),
          QStringLiteral("release"), QStringLiteral("assert-2")}},
    });
    versions.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(releaseGroup),
        CanonicalEntityRef::from(releaseOne),
        RelationshipKind::ReleaseVersion,
        RelationshipDirectionality::Directed,
        {},
        QStringLiteral("release"),
        {{QStringLiteral("musicbrainz"), QStringLiteral("rel-1"),
          QStringLiteral("release"), QStringLiteral("assert-1")}},
    });
    versions.normalize();
    require(versions.relationships().size() == 2,
            "multiple Releases must remain separate versions beneath one ReleaseGroupMbid");
    require(versions.relationships().at(0).source == CanonicalEntityRef::from(releaseGroup)
                && versions.relationships().at(1).source == CanonicalEntityRef::from(releaseGroup),
            "each concrete Release version must retain the same ReleaseGroup endpoint");
    require(versions.relationships().at(0).target != versions.relationships().at(1).target,
            "distinct ReleaseMbid values must not collapse during normalization");

    ContextGraph duplicateA;
    duplicateA.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
        RelationshipKind::PerformanceOf, RelationshipDirectionality::Directed, {},
        QStringLiteral("performance"),
        {{QStringLiteral("source-b"), QStringLiteral("item-b"),
          QStringLiteral("performance"), QStringLiteral("b")}},
    });
    duplicateA.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
        RelationshipKind::PerformanceOf, RelationshipDirectionality::Directed, {},
        QStringLiteral("performance"),
        {{QStringLiteral("source-a"), QStringLiteral("item-a"),
          QStringLiteral("performance"), QStringLiteral("a")}},
    });
    duplicateA.normalize();
    require(duplicateA.relationships().size() == 1,
            "equivalent duplicate edges must normalize to one semantic edge");
    require(duplicateA.relationships().at(0).evidence.size() == 2,
            "normalization must preserve distinct provenance from duplicate assertions");
    require(duplicateA.relationships().at(0).evidence.at(0).source == QStringLiteral("source-a")
                && duplicateA.relationships().at(0).evidence.at(1).source == QStringLiteral("source-b"),
            "merged provenance must have deterministic ordering");

    ContextGraph duplicateB;
    duplicateB.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
        RelationshipKind::PerformanceOf, RelationshipDirectionality::Directed, {},
        QStringLiteral("performance"),
        {{QStringLiteral("source-a"), QStringLiteral("item-a"),
          QStringLiteral("performance"), QStringLiteral("a")}},
    });
    duplicateB.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
        RelationshipKind::PerformanceOf, RelationshipDirectionality::Directed, {},
        QStringLiteral("performance"),
        {{QStringLiteral("source-b"), QStringLiteral("item-b"),
          QStringLiteral("performance"), QStringLiteral("b")}},
    });
    duplicateB.normalize();
    require(duplicateB.relationships().size() == 1
                && duplicateB.relationships().at(0).evidence.size() == 2
                && duplicateB.relationships().at(0).evidence.at(0).source
                    == duplicateA.relationships().at(0).evidence.at(0).source
                && duplicateB.relationships().at(0).evidence.at(1).source
                    == duplicateA.relationships().at(0).evidence.at(1).source,
            "equivalent edges must normalize identically regardless of input order");

    ContextGraph unsupported;
    unsupported.addRelationship(ContextRelationship{
        CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
        RelationshipKind::Unsupported, RelationshipDirectionality::Directed, {},
        QStringLiteral("producer-ish-but-unknown"),
        {{QStringLiteral("future-source"), QStringLiteral("opaque-42"),
          QStringLiteral("producer-ish-but-unknown"), QStringLiteral("raw")}},
    });
    unsupported.normalize();
    require(unsupported.relationships().size() == 1,
            "unsupported relationship should survive normalization");
    require(unsupported.relationships().at(0).kind == RelationshipKind::Unsupported,
            "unsupported relationship type must not fabricate a known kind");
    require(unsupported.relationships().at(0).rawRelationshipType
                == QStringLiteral("producer-ish-but-unknown"),
            "unsupported relationship must retain its opaque source type");
    require(unsupported.relationships().at(0).evidence.at(0).source
                == QStringLiteral("future-source"),
            "unsupported relationship must retain provenance");

    const QString regression = app.arguments().value(1);
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-evidence")) {
        ContextGraph collision;
        collision.addRelationship({CanonicalEntityRef::from(recording), CanonicalEntityRef::from(work),
            RelationshipKind::PerformanceOf, RelationshipDirectionality::Directed, {}, {},
            {{QStringLiteral("a:b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")},
             {QStringLiteral("a"), QStringLiteral("b:c"), QStringLiteral("d"), QStringLiteral("e")}}});
        collision.normalize();
        require(collision.relationships().first().evidence.size() == 2,
                "M0R delimiter text must not collapse distinct provenance tuples");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-payload")) {
        auto ordered = artistCredit;
        ordered.roles = {{CreditRoleKind::Producer, QStringLiteral("producer")},
                         {CreditRoleKind::PrimaryArtist, QStringLiteral("artist")}};
        auto reversed = ordered;
        std::reverse(reversed.roles.begin(), reversed.roles.end());
        ContextGraph left, right;
        left.addRelationship({CanonicalEntityRef::from(recording), CanonicalEntityRef::from(artist),
            RelationshipKind::ArtistCredit, RelationshipDirectionality::Directed, {ordered}, {}, {}});
        right.addRelationship({CanonicalEntityRef::from(recording), CanonicalEntityRef::from(artist),
            RelationshipKind::ArtistCredit, RelationshipDirectionality::Directed, {reversed}, {}, {}});
        left.normalize(); right.normalize();
        require(left.relationships().first().artistCredits.first().roles.first().kind
                    == right.relationships().first().artistCredits.first().roles.first().kind,
                "M0R stored credit payload must canonicalize role arrival order");
        if (!regression.isEmpty()) return 0;
    }
    qInfo().noquote() << "VINYL_BF1_CONTEXT_GRAPH_OK checks=" << g_checks;
    return 0;
}
