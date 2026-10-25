#include "DiscoveryEnrichment.h"
#include "../bg1-catalogue-client-prototype/VinylCatalogueTypes.h"

#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QFile>
#include <QList>
#include <QTime>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <type_traits>

using namespace vinyl;
using namespace vinyl::catalogue;
using namespace vinyl::discovery;

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (!condition) {
        qCritical().noquote() << "FAIL:" << message;
        std::exit(1);
    }
}

template <typename T>
T mustId(const char *text)
{
    const auto parsed =
        T::parse(QString::fromLatin1(text));
    require(parsed.ok(), "fixture MBID should parse");
    return *parsed.value;
}

ProviderId mustProvider(const char *text)
{
    const auto parsed =
        ProviderId::parse(QString::fromLatin1(text));
    require(parsed.ok(), "fixture provider ID should parse");
    return *parsed.value;
}

ProviderItemId mustProviderItem(const char *text)
{
    const auto parsed =
        ProviderItemId::parse(QString::fromLatin1(text));
    require(
        parsed.ok(),
        "fixture provider item ID should parse");
    return *parsed.value;
}

DiscoverySnapshot snapshot(
    const char *token,
    DiscoveryFreshness freshness,
    int secondsFromBase = 0)
{
    const QDateTime base(
        QDate(2026, 10, 6),
        QTime(6, 0, 0),
        Qt::UTC);
    return DiscoverySnapshot{
        QString::fromLatin1(token),
        base.addSecs(secondsFromBase),
        freshness,
    };
}

DiscoveryProvenance provenance(
    const ProviderId &provider,
    const char *evidenceId,
    const char *detail = "")
{
    return DiscoveryProvenance{
        provider,
        QString::fromLatin1(evidenceId),
        QString::fromUtf8(detail),
    };
}

DiscoveryItem item(
    const CanonicalEntityRef &entity,
    const ProviderId &provider,
    const char *evidenceId,
    std::optional<int> rank = std::nullopt,
    std::optional<double> score = std::nullopt)
{
    return DiscoveryItem{
        entity,
        rank,
        score,
        provenance(provider, evidenceId),
    };
}

DiscoveryCollection mustCollection(
    DiscoveryCollectionSpec spec)
{
    const auto created =
        DiscoveryCollection::create(std::move(spec));
    require(
        created.ok(),
        "fixture collection should be valid");
    return *created.value;
}

bool sameItemOrder(
    const QVector<DiscoveryItem> &left,
    const QVector<DiscoveryItem> &right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype i = 0; i < left.size(); ++i) {
        const DiscoveryItem &a = left.at(i);
        const DiscoveryItem &b = right.at(i);
        if (a.entity != b.entity
            || a.sourceRank != b.sourceRank
            || a.sourceScore != b.sourceScore
            || !(a.provenance.providerId
                == b.provenance.providerId)
            || a.provenance.evidenceId
                != b.provenance.evidenceId) {
            return false;
        }
    }
    return true;
}

bool sameCollectionOrder(
    const QVector<DiscoveryCollection> &left,
    const QVector<DiscoveryCollection> &right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (qsizetype i = 0; i < left.size(); ++i) {
        if (left.at(i).deterministicOrderKey()
            != right.at(i).deterministicOrderKey()) {
            return false;
        }
    }
    return true;
}

void provePublicSurfaceHasNoBoundaryLeaks()
{
    QFile header(
        QStringLiteral(VINYL_BG2_SOURCE_DIR)
        + QStringLiteral("/DiscoveryEnrichment.h"));
    require(
        header.open(
            QIODevice::ReadOnly | QIODevice::Text),
        "BG2 public header should be readable");

    const QByteArray text = header.readAll();

    const QList<QByteArray> forbidden = {
        QByteArray("VinylCatalogueTypes"),
        QByteArray("catalogue::Recording"),
        QByteArray("catalogue::ReleaseGroup"),
        QByteArray("QObject"),
        QByteArray("QNetwork"),
        QByteArray("QML"),
        QByteArray("Worker"),
        QByteArray("R2"),
        QByteArray("For You"),
        QByteArray("Quick Picks"),
    };

    for (const QByteArray &token : forbidden) {
        require(
            !text.contains(token),
            "BG2 public surface must not leak canonical metadata/UI/serving implementation concepts");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    static_assert(
        std::is_same_v<
            decltype(DiscoveryItem::entity),
            CanonicalEntityRef>,
        "DiscoveryItem must reference canonical identity instead of embedding BG1 metadata");

    const auto recA = mustId<RecordingMbid>(
        "11111111-1111-4111-8111-111111111111");
    const auto recB = mustId<RecordingMbid>(
        "22222222-2222-4222-8222-222222222222");
    const auto recC = mustId<RecordingMbid>(
        "33333333-3333-4333-8333-333333333333");
    const auto recD = mustId<RecordingMbid>(
        "44444444-4444-4444-8444-444444444444");
    const auto artistA = mustId<ArtistMbid>(
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto groupA = mustId<ReleaseGroupMbid>(
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");

    const ProviderId listenbrainz =
        mustProvider("listenbrainz");
    const ProviderId editorialProvider =
        mustProvider("vinyl.editorial");
    const ProviderId aggregateProvider =
        mustProvider("vinyl.discovery.aggregate");

    Recording canonicalRecording{
        recA,
        QStringLiteral("Canonical Catalogue Title"),
        {},
        183000,
        {},
    };
    const QString canonicalTitleBefore =
        canonicalRecording.title;

    DiscoveryItem popularityItem = item(
        CanonicalEntityRef::from(
            canonicalRecording.mbid),
        listenbrainz,
        "global-popularity:2026-10-06",
        5,
        -2.75);

    const DiscoveryCollection popularity =
        mustCollection(
            DiscoveryCollectionSpec{
                listenbrainz,
                DiscoveryKind::Popularity,
                snapshot(
                    "lb-popularity-2026-10-06",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {popularityItem},
            });

    require(
        canonicalRecording.mbid == recA,
        "discovery construction must not alter BG1 RecordingMbid");
    require(
        canonicalRecording.title
            == canonicalTitleBefore,
        "discovery construction must not mutate BG1 canonical metadata");
    require(
        popularity.items().at(0).entity
            == CanonicalEntityRef::from(recA),
        "discovery item must point back to canonical identity only");
    require(
        popularity.items().at(0).sourceScore
            == std::optional<double>(-2.75),
        "source score must survive unchanged, including negative provider scales");
    require(
        popularity.items().at(0).sourceRank
            == std::optional<int>(5),
        "source rank must survive normalization unchanged");

    const ProviderItemRef essentialIdentity(
        editorialProvider,
        mustProviderItem(
            "essential:albums:2026:q4"));

    const DiscoveryCollection editorial =
        mustCollection(
            DiscoveryCollectionSpec{
                editorialProvider,
                DiscoveryKind::Editorial,
                snapshot(
                    "editorial-2026-q4",
                    DiscoveryFreshness::Fresh,
                    5),
                essentialIdentity,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    editorialProvider,
                    "curator:essential:slot-01",
                    1,
                    std::nullopt)},
            });

    require(
        editorial.items().at(0).entity
            == popularity.items().at(0).entity,
        "same canonical entity may appear in multiple discovery collections");
    require(
        editorial.items()
                .at(0)
                .provenance.evidenceId
            != popularity.items()
                   .at(0)
                   .provenance.evidenceId,
        "same entity must retain collection-specific evidence");
    require(
        editorial.kind()
                == DiscoveryKind::Editorial
            && popularity.kind()
                == DiscoveryKind::Popularity,
        "editorial and popularity signals must remain distinguishable");
    require(
        editorial.editorialCollectionRef()
                .has_value()
            && !popularity
                    .editorialCollectionRef()
                    .has_value(),
        "editorial collection identity must remain explicit and editorial-only");

    const DiscoveryCollection stale =
        mustCollection(
            DiscoveryCollectionSpec{
                listenbrainz,
                DiscoveryKind::TrendingCurrentInterest,
                snapshot(
                    "trend-window-2026-10-05",
                    DiscoveryFreshness::Stale),
                std::nullopt,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recB),
                    listenbrainz,
                    "trend-window:item-1",
                    1,
                    42.0)},
            });

    require(
        popularity.snapshot().freshness
            == DiscoveryFreshness::Fresh,
        "freshness must remain explicit on fresh snapshots");
    require(
        stale.snapshot().freshness
            == DiscoveryFreshness::Stale,
        "freshness must remain explicit on stale snapshots");
    require(
        requireFresh(popularity).ok(),
        "requireFresh should accept explicitly fresh collection");

    const auto staleRequired =
        requireFresh(stale);
    require(
        !staleRequired.ok()
            && staleRequired.error.code
                == DiscoveryErrorCode::Stale,
        "requireFresh must reject stale snapshot with typed Stale");

    const DiscoveryRegionContext india{
        QStringLiteral("iso-3166-1-alpha-2"),
        QStringLiteral("IN"),
        QStringLiteral("India"),
    };

    const DiscoveryCollection regional =
        mustCollection(
            DiscoveryCollectionSpec{
                aggregateProvider,
                DiscoveryKind::Regional,
                snapshot(
                    "in-current-2026-10-06",
                    DiscoveryFreshness::Fresh,
                    10),
                std::nullopt,
                india,
                {item(
                    CanonicalEntityRef::from(recA),
                    listenbrainz,
                    "regional:in:item-1",
                    3,
                    9.5)},
            });

    require(
        regional.region().has_value()
            && regional.region()->code
                == QStringLiteral("IN"),
        "regional context should remain explicit");
    require(
        regional.items().at(0).entity
            == CanonicalEntityRef::from(recA),
        "region metadata must never change canonical music identity");

    QVector<DiscoveryItem> orderingInput = {
        item(
            CanonicalEntityRef::from(recA),
            aggregateProvider,
            "order:a",
            2,
            100.0),
        item(
            CanonicalEntityRef::from(recB),
            aggregateProvider,
            "order:b",
            1,
            1.0),
        item(
            CanonicalEntityRef::from(groupA),
            aggregateProvider,
            "order:c",
            std::nullopt,
            0.5),
        item(
            CanonicalEntityRef::from(artistA),
            aggregateProvider,
            "order:d",
            std::nullopt,
            0.5),
        item(
            CanonicalEntityRef::from(recC),
            aggregateProvider,
            "order:e",
            std::nullopt,
            std::nullopt),
        item(
            CanonicalEntityRef::from(recD),
            aggregateProvider,
            "order:f",
            std::nullopt,
            std::nullopt),
    };

    QVector<DiscoveryItem> reversedInput =
        orderingInput;
    std::reverse(
        reversedInput.begin(),
        reversedInput.end());

    const DiscoveryCollection orderedA =
        mustCollection(
            DiscoveryCollectionSpec{
                aggregateProvider,
                DiscoveryKind::ChartAggregate,
                snapshot(
                    "ordering-proof",
                    DiscoveryFreshness::Fresh,
                    20),
                std::nullopt,
                std::nullopt,
                orderingInput,
            });

    const DiscoveryCollection orderedB =
        mustCollection(
            DiscoveryCollectionSpec{
                aggregateProvider,
                DiscoveryKind::ChartAggregate,
                snapshot(
                    "ordering-proof",
                    DiscoveryFreshness::Fresh,
                    20),
                std::nullopt,
                std::nullopt,
                reversedInput,
            });

    require(
        sameItemOrder(
            orderedA.items(),
            orderedB.items()),
        "deterministic item ordering must survive input-order changes");
    require(
        orderedA.items().at(0).entity
                == CanonicalEntityRef::from(recB)
            && orderedA.items().at(1).entity
                == CanonicalEntityRef::from(recA),
        "source rank must dominate deterministic item ordering");
    require(
        orderedA.items().at(2).entity.kind()
                == CanonicalEntityKind::Artist
            && orderedA.items().at(3).entity.kind()
                == CanonicalEntityKind::ReleaseGroup,
        "canonical identity must break equal-score ties deterministically");

    QVector<DiscoveryCollection> collectionsA = {
        regional,
        editorial,
        popularity,
        stale,
    };
    QVector<DiscoveryCollection> collectionsB =
        collectionsA;
    std::reverse(
        collectionsB.begin(),
        collectionsB.end());

    const auto normalizedCollectionsA =
        orderedDiscoveryCollections(
            collectionsA);
    const auto normalizedCollectionsB =
        orderedDiscoveryCollections(
            collectionsB);

    require(
        sameCollectionOrder(
            normalizedCollectionsA,
            normalizedCollectionsB),
        "deterministic collection ordering must survive input-order changes");

    const auto duplicate =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                listenbrainz,
                DiscoveryKind::Popularity,
                snapshot(
                    "duplicate-proof",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {
                    item(
                        CanonicalEntityRef::from(recA),
                        listenbrainz,
                        "duplicate:a",
                        1,
                        5.0),
                    item(
                        CanonicalEntityRef::from(recA),
                        listenbrainz,
                        "duplicate:b",
                        2,
                        4.0),
                },
            });
    require(
        !duplicate.ok()
            && duplicate.error.code
                == DiscoveryErrorCode::Invalid,
        "duplicate canonical item inside one collection must be Invalid");

    const auto invalidRank =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                listenbrainz,
                DiscoveryKind::Popularity,
                snapshot(
                    "invalid-rank",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    listenbrainz,
                    "invalid-rank:item",
                    0,
                    1.0)},
            });
    require(
        !invalidRank.ok()
            && invalidRank.error.code
                == DiscoveryErrorCode::Invalid,
        "non-positive source rank must be Invalid");

    const auto invalidScore =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                listenbrainz,
                DiscoveryKind::Popularity,
                snapshot(
                    "invalid-score",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    listenbrainz,
                    "invalid-score:item",
                    1,
                    std::numeric_limits<
                        double>::quiet_NaN())},
            });
    require(
        !invalidScore.ok()
            && invalidScore.error.code
                == DiscoveryErrorCode::Invalid,
        "non-finite source score must be Invalid");

    const auto missingEditorialIdentity =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                editorialProvider,
                DiscoveryKind::Editorial,
                snapshot(
                    "missing-editorial-id",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    editorialProvider,
                    "editorial:item",
                    1,
                    std::nullopt)},
            });
    require(
        !missingEditorialIdentity.ok()
            && missingEditorialIdentity
                   .error.code
                == DiscoveryErrorCode::Invalid,
        "Editorial collection without provider identity must be Invalid");

    const ProviderItemRef
        wrongProviderEditorialIdentity(
            listenbrainz,
            mustProviderItem(
                "editorial:foreign"));

    const auto mismatchedEditorialIdentity =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                editorialProvider,
                DiscoveryKind::Editorial,
                snapshot(
                    "foreign-editorial-id",
                    DiscoveryFreshness::Fresh),
                wrongProviderEditorialIdentity,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    editorialProvider,
                    "editorial:item",
                    1,
                    std::nullopt)},
            });
    require(
        !mismatchedEditorialIdentity.ok()
            && mismatchedEditorialIdentity
                   .error.code
                == DiscoveryErrorCode::Invalid,
        "Editorial identity from another provider must be Invalid");

    const auto missingRegion =
        DiscoveryCollection::create(
            DiscoveryCollectionSpec{
                aggregateProvider,
                DiscoveryKind::Regional,
                snapshot(
                    "missing-region",
                    DiscoveryFreshness::Fresh),
                std::nullopt,
                std::nullopt,
                {item(
                    CanonicalEntityRef::from(recA),
                    aggregateProvider,
                    "regional:item",
                    1,
                    1.0)},
            });
    require(
        !missingRegion.ok()
            && missingRegion.error.code
                == DiscoveryErrorCode::Invalid,
        "Regional collection without region context must be Invalid");

    const auto unavailable =
        DiscoveryResult<int>::failure(
            DiscoveryErrorCode::Unavailable,
            QStringLiteral("provider"),
            QStringLiteral("fixture outage"));
    require(
        !unavailable.ok()
            && discoveryErrorCodeName(
                   unavailable.error.code)
                == QStringLiteral("unavailable"),
        "Unavailable must remain a typed stable error");
    require(
        discoveryErrorCodeName(
            DiscoveryErrorCode::Stale)
                == QStringLiteral("stale")
            && discoveryErrorCodeName(
                   DiscoveryErrorCode::Invalid)
                == QStringLiteral("invalid"),
        "Stale and Invalid must expose stable names");
    require(
        discoveryKindName(
            DiscoveryKind::Editorial)
                == QStringLiteral("editorial")
            && discoveryKindName(
                   DiscoveryKind::Popularity)
                == QStringLiteral("popularity"),
        "discovery kinds must expose stable distinct names");
    require(
        discoveryFreshnessName(
            DiscoveryFreshness::Fresh)
                == QStringLiteral("fresh")
            && discoveryFreshnessName(
                   DiscoveryFreshness::Stale)
                == QStringLiteral("stale"),
        "freshness states must expose stable names");

    provePublicSurfaceHasNoBoundaryLeaks();

    qInfo().noquote()
        << "VINYL_BG2_DISCOVERY_OK checks="
        << g_checks;
    return 0;
}
