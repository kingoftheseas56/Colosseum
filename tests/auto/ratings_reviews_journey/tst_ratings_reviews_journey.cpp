#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfileStoreRuntime.h"
#include "account/RatingsReviewsController.h"
#include "account/RatingsReviewsStore.h"
#include "engine/ColosseumTitleIdentityRegistry.h"
#include "engine/RatingsReviewsProviderReadProjection.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class tst_ratings_reviews_journey final : public QObject {
    Q_OBJECT
private slots:
    void identityRegistryResolvesFrozenFrierenAndRefusesProviderIdentity();
    void identityRegistryDerivesStableCatalogueIdentities();
    void identityRegistryLearnsAndPersistsAnimePivotUnions();
    void identityRegistryFilmSeedAnswersBothWorlds();
    void identityRegistryExcludesComicsAndUnadmittedRoutes();
    void controllerOpenRequiresRegistryValidatedIdentityTuple();
    void controllerOpenAcceptsDerivedCatalogueIdentity();
    void canonicalSaveEditDeleteHasZeroProviderOperations();
    void staleRouteAndProfileGenerationsCannotMutate();
    void productionProviderReadFailsClosedAndFixtureGateIsExact();
    void providerOrderingPreservesHiddenSlots();
    void sourceLinksAreNativeValidated();
};

namespace {
constexpr auto kFrierenMediaId =
    "ct1:49f10000-0000-4000-8000-000000000001";

QVariantMap identityContext() {
    return {
        {QStringLiteral("identity"), QVariantMap{
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("series")},
            {QStringLiteral("mediaId"), QString::fromLatin1(kFrierenMediaId)}}},
        {QStringLiteral("title"), QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Frieren: Beyond Journey's End")}}},
        {QStringLiteral("origin"), QStringLiteral("theatre-detail")},
        {QStringLiteral("readIds"), QVariantMap{}}
    };
}

struct RuntimeFixture {
    QTemporaryDir temp;
    QString legacyRoot;
    QString appDataRoot;
    LegacyPersonalStateStorage legacy;
    ProfileStoreRuntime runtime;

    RuntimeFixture()
        : legacyRoot(QDir(temp.path()).filePath(QStringLiteral("legacy"))),
          appDataRoot(QDir(temp.path()).filePath(QStringLiteral("appdata"))),
          legacy(LegacyPersonalStateStorage::isolated(legacyRoot)),
          runtime(legacy, appDataRoot) {
        if (!temp.isValid())
            qFatal("Could not create Ratings & Reviews runtime fixture.");
        QDir().mkpath(legacyRoot);
        QDir().mkpath(appDataRoot);
        QString error;
        if (!runtime.suspendPersonalStoresForMigration(&error))
            qFatal("Could not suspend sealed profile stores: %s", qPrintable(error));
        if (!runtime.reloadLegacyProfile(&error))
            qFatal("Could not activate legacy profile: %s", qPrintable(error));
    }
};

QStringList variantStrings(const QVariantList &values) {
    QStringList out;
    for (const QVariant &value : values)
        out.append(value.toString());
    return out;
}
}

void tst_ratings_reviews_journey::
identityRegistryResolvesFrozenFrierenAndRefusesProviderIdentity() {
    ColosseumTitleIdentityRegistry embeddedRegistry;
    QVERIFY2(embeddedRegistry.ready(), qPrintable(embeddedRegistry.errorCode()));

    ColosseumTitleIdentityRegistry registry(QString::fromUtf8(RR_IDENTITY_FILE));
    QVERIFY2(registry.ready(), qPrintable(registry.errorCode()));

    const QVariantList aliases{QVariantMap{
        {QStringLiteral("namespace"), QStringLiteral("theatre-source-id")},
        {QStringLiteral("value"), QStringLiteral("tt22248376")}}};
    const QVariantMap embeddedResolved = embeddedRegistry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(), aliases);
    QVERIFY(embeddedResolved.value(QStringLiteral("available")).toBool());
    QCOMPARE(embeddedResolved.value(QStringLiteral("mediaId")).toString(),
             QString::fromLatin1(kFrierenMediaId));

    const QVariantMap resolved = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(), aliases);
    QVERIFY(resolved.value(QStringLiteral("available")).toBool());
    QCOMPARE(resolved.value(QStringLiteral("mediaId")).toString(),
             QString::fromLatin1(kFrierenMediaId));

    const QVariantMap providerAsCanonical = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("tt22248376"), {});
    QVERIFY(!providerAsCanonical.value(QStringLiteral("available")).toBool());

    const QVariantMap guessedByTitle = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {QVariantMap{{QStringLiteral("namespace"), QStringLiteral("title")},
                     {QStringLiteral("value"),
                      QStringLiteral("Frieren: Beyond Journey's End")}}});
    QVERIFY(!guessedByTitle.value(QStringLiteral("available")).toBool());
}

QVariantMap alias(const QString &nameSpace, const QString &value) {
    return {{QStringLiteral("namespace"), nameSpace},
            {QStringLiteral("value"), value}};
}

void tst_ratings_reviews_journey::
identityRegistryDerivesStableCatalogueIdentities() {
    ColosseumTitleIdentityRegistry registry;
    QVERIFY2(registry.ready(), qPrintable(registry.errorCode()));

    // Exact match + restart stability: a fresh registry instance derives the
    // same canonical id for the same exact alias tuple.
    const QVariantMap first = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"))});
    QVERIFY(first.value(QStringLiteral("available")).toBool());
    const QString derived = first.value(QStringLiteral("mediaId")).toString();
    QVERIFY(ColosseumTitleIdentityRegistry::isCanonicalMediaId(derived));
    QVERIFY(derived != QString::fromLatin1(kFrierenMediaId));

    ColosseumTitleIdentityRegistry restarted;
    QVERIFY2(restarted.ready(), qPrintable(restarted.errorCode()));
    const QVariantMap second = restarted.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"))});
    QCOMPARE(second.value(QStringLiteral("mediaId")).toString(), derived);

    // The open() re-check has no aliases; a derived id must self-validate for
    // exactly its pair.
    const QVariantMap direct = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), derived, {});
    QVERIFY(direct.value(QStringLiteral("available")).toBool());
    QCOMPARE(direct.value(QStringLiteral("mediaId")).toString(), derived);
    const QVariantMap wrongPair = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("movie"), derived, {});
    QVERIFY(!wrongPair.value(QStringLiteral("available")).toBool());

    // Anime alias pivot: original kitsu id + pivoted IMDb id resolve to the
    // IMDb-anchored identity, with or without the kitsu alias present.
    const QVariantMap pivoted = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("kitsu:42041")),
         alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt22248376"))});
    // Frieren is seed-pinned, so the pivot pair resolves to the frozen id.
    QCOMPARE(pivoted.value(QStringLiteral("mediaId")).toString(),
             QString::fromLatin1(kFrierenMediaId));
    const QVariantMap genericPivotOnly = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"))});
    const QVariantMap genericBothIds = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("kitsu:9999")),
         alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"))});
    QCOMPARE(genericBothIds.value(QStringLiteral("mediaId")).toString(),
             genericPivotOnly.value(QStringLiteral("mediaId")).toString());

    // Collision discipline: distinct aliases, kinds, and worlds derive
    // distinct ids; two editions of one book never collapse.
    const auto idFor = [&registry](const QString &world, const QString &kind,
                                   const QString &nameSpace, const QString &value) {
        const QVariantMap resolved = registry.resolve(
            world, kind, QString(), {alias(nameSpace, value)});
        return resolved.value(QStringLiteral("mediaId")).toString();
    };
    const QString breakingBadSeries =
        idFor(QStringLiteral("theatre"), QStringLiteral("series"),
              QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"));
    const QString breakingBadMovie =
        idFor(QStringLiteral("theatre"), QStringLiteral("movie"),
              QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"));
    const QString mangaIdentity =
        idFor(QStringLiteral("tankoban"), QStringLiteral("manga"),
              QStringLiteral("tankoban-source-id"), QStringLiteral("mal:2"));
    const QString vaultIdentity =
        idFor(QStringLiteral("vault"), QStringLiteral("film"),
              QStringLiteral("vault-source-id"), QStringLiteral("imdb:tt0903747"));
    QVERIFY(!breakingBadSeries.isEmpty());
    QVERIFY(!breakingBadMovie.isEmpty());
    QVERIFY(!mangaIdentity.isEmpty());
    QVERIFY(!vaultIdentity.isEmpty());
    QVERIFY(breakingBadSeries != breakingBadMovie);
    QVERIFY(breakingBadSeries != mangaIdentity);
    QVERIFY(breakingBadSeries != vaultIdentity);

    // Film identity space: the same identified IMDb film is ONE title from the
    // theatre door and the vault door — identical derived id, and that id
    // self-validates on open from either pair.
    QCOMPARE(breakingBadMovie, vaultIdentity);
    const QVariantMap filmDirectTheatre = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("movie"), breakingBadMovie, {});
    QVERIFY(filmDirectTheatre.value(QStringLiteral("available")).toBool());
    const QVariantMap filmDirectVault = registry.resolve(
        QStringLiteral("vault"), QStringLiteral("film"), breakingBadMovie, {});
    QVERIFY(filmDirectVault.value(QStringLiteral("available")).toBool());
    const QVariantMap filmWrongPair = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), breakingBadMovie, {});
    QVERIFY(!filmWrongPair.value(QStringLiteral("available")).toBool());
    // Vault admits only identified IMDb films; anything else stays unavailable.
    QVERIFY(!registry.resolve(
        QStringLiteral("vault"), QStringLiteral("film"), QString(),
        {alias(QStringLiteral("vault-source-id"), QStringLiteral("tmdb:123"))})
        .value(QStringLiteral("available")).toBool());

    // Biblio: the edition-level isbn anchor outranks the shared work key.
    const QVariantMap editionOne = registry.resolve(
        QStringLiteral("biblio"), QStringLiteral("book"), QString(),
        {alias(QStringLiteral("openlibrary-work"), QStringLiteral("/works/OL1W")),
         alias(QStringLiteral("isbn"), QStringLiteral("9780001"))});
    const QVariantMap editionTwo = registry.resolve(
        QStringLiteral("biblio"), QStringLiteral("book"), QString(),
        {alias(QStringLiteral("openlibrary-work"), QStringLiteral("/works/OL1W")),
         alias(QStringLiteral("isbn"), QStringLiteral("9780002"))});
    const QVariantMap isbnOnly = registry.resolve(
        QStringLiteral("biblio"), QStringLiteral("book"), QString(),
        {alias(QStringLiteral("isbn"), QStringLiteral("9780001"))});
    QVERIFY(editionOne.value(QStringLiteral("available")).toBool());
    QVERIFY(editionTwo.value(QStringLiteral("available")).toBool());
    const QString editionOneId =
        editionOne.value(QStringLiteral("mediaId")).toString();
    QCOMPARE(editionOneId,
             isbnOnly.value(QStringLiteral("mediaId")).toString());
    QVERIFY(editionOneId != editionTwo.value(QStringLiteral("mediaId")).toString());

    // Non-identity namespaces never derive: title guesses stay unavailable.
    const QVariantMap guessed = registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("title"), QStringLiteral("Breaking Bad"))});
    QVERIFY(!guessed.value(QStringLiteral("available")).toBool());
}

void tst_ratings_reviews_journey::
identityRegistryLearnsAndPersistsAnimePivotUnions() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString unionsPath = QDir(temp.path()).filePath(QStringLiteral("unions.json"));

    const QVariantList providerOnly{
        alias(QStringLiteral("theatre-source-id"), QStringLiteral("kitsu:4242"))};
    const QVariantList pivoted{
        alias(QStringLiteral("theatre-source-id"), QStringLiteral("kitsu:4242")),
        alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt424242"))};

    // Stateless default: the registry never records or reads unions, so the
    // provider-only and pivoted forms derive their own anchors.
    ColosseumTitleIdentityRegistry stateless;
    QVERIFY2(stateless.ready(), qPrintable(stateless.errorCode()));
    const QString providerAnchored = stateless.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(), providerOnly)
        .value(QStringLiteral("mediaId")).toString();
    const QString imdbAnchored = stateless.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(), pivoted)
        .value(QStringLiteral("mediaId")).toString();
    QVERIFY(!providerAnchored.isEmpty());
    QVERIFY(!imdbAnchored.isEmpty());
    QVERIFY(providerAnchored != imdbAnchored);
    QVERIFY(!QFile::exists(unionsPath));

    // With persistence enabled, the pivoted resolve teaches the union and the
    // provider-only lookup converges on the IMDb-anchored identity — across a
    // full registry restart.
    {
        ColosseumTitleIdentityRegistry learning;
        QVERIFY2(learning.ready(), qPrintable(learning.errorCode()));
        learning.setAliasUnionsPath(unionsPath);
        QCOMPARE(learning.resolve(
            QStringLiteral("theatre"), QStringLiteral("series"), QString(), pivoted)
            .value(QStringLiteral("mediaId")).toString(), imdbAnchored);
        QVERIFY(QFile::exists(unionsPath));
    }
    {
        ColosseumTitleIdentityRegistry restarted;
        QVERIFY2(restarted.ready(), qPrintable(restarted.errorCode()));
        restarted.setAliasUnionsPath(unionsPath);
        QCOMPARE(restarted.resolve(
            QStringLiteral("theatre"), QStringLiteral("series"), QString(), providerOnly)
            .value(QStringLiteral("mediaId")).toString(), imdbAnchored);
        QCOMPARE(restarted.resolve(
            QStringLiteral("theatre"), QStringLiteral("series"), QString(), pivoted)
            .value(QStringLiteral("mediaId")).toString(), imdbAnchored);
    }

    // A corrupt union file degrades silently to stateless behaviour.
    QFile corrupt(unionsPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QVERIFY(corrupt.write("{\"version\":1,\"unions\":[{broken") > 0);
    corrupt.close();
    ColosseumTitleIdentityRegistry degraded;
    QVERIFY2(degraded.ready(), qPrintable(degraded.errorCode()));
    degraded.setAliasUnionsPath(unionsPath);
    QCOMPARE(degraded.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(), providerOnly)
        .value(QStringLiteral("mediaId")).toString(), providerAnchored);
}

void tst_ratings_reviews_journey::
identityRegistryFilmSeedAnswersBothWorlds() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString seedPath = QDir(temp.path()).filePath(QStringLiteral("film-seed.json"));
    QFile seed(seedPath);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write(
        "{\n  \"version\": 1,\n  \"titles\": [\n    {\n"
        "      \"world\": \"theatre\",\n      \"kind\": \"movie\",\n"
        "      \"media_id\": \"ct1:49f10000-0000-4000-8000-0000000000aa\",\n"
        "      \"aliases\": [\n"
        "        { \"namespace\": \"theatre-source-id\", \"value\": \"tt7777777\" }\n"
        "      ]\n    }\n  ]\n}\n");
    seed.close();

    ColosseumTitleIdentityRegistry registry(seedPath);
    QVERIFY2(registry.ready(), qPrintable(registry.errorCode()));
    const QString pinned = QStringLiteral("ct1:49f10000-0000-4000-8000-0000000000aa");

    // The theatre seed row answers its own pair…
    QCOMPARE(registry.resolve(
        QStringLiteral("theatre"), QStringLiteral("movie"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt7777777"))})
        .value(QStringLiteral("mediaId")).toString(), pinned);
    // …and the vault door for the same identified IMDb film gets the SAME id.
    QCOMPARE(registry.resolve(
        QStringLiteral("vault"), QStringLiteral("film"), QString(),
        {alias(QStringLiteral("vault-source-id"), QStringLiteral("imdb:tt7777777"))})
        .value(QStringLiteral("mediaId")).toString(), pinned);

    // Two seeds disagreeing about one IMDb film stay a conflict, never a guess.
    const QString conflictPath = QDir(temp.path()).filePath(QStringLiteral("conflict.json"));
    QFile conflict(conflictPath);
    QVERIFY(conflict.open(QIODevice::WriteOnly));
    conflict.write(
        "{\n  \"version\": 1,\n  \"titles\": [\n"
        "    { \"world\": \"theatre\", \"kind\": \"movie\",\n"
        "      \"media_id\": \"ct1:49f10000-0000-4000-8000-0000000000aa\",\n"
        "      \"aliases\": [ { \"namespace\": \"theatre-source-id\", \"value\": \"tt7777777\" } ] },\n"
        "    { \"world\": \"vault\", \"kind\": \"film\",\n"
        "      \"media_id\": \"ct1:49f10000-0000-4000-8000-0000000000bb\",\n"
        "      \"aliases\": [ { \"namespace\": \"vault-source-id\", \"value\": \"imdb:tt7777777\" } ] }\n"
        "  ]\n}\n");
    conflict.close();
    ColosseumTitleIdentityRegistry conflicted(conflictPath);
    QVERIFY2(conflicted.ready(), qPrintable(conflicted.errorCode()));
    QVERIFY(!conflicted.resolve(
        QStringLiteral("vault"), QStringLiteral("film"), QString(),
        {alias(QStringLiteral("vault-source-id"), QStringLiteral("imdb:tt7777777"))})
        .value(QStringLiteral("available")).toBool());
}

void tst_ratings_reviews_journey::
identityRegistryExcludesComicsAndUnadmittedRoutes() {
    ColosseumTitleIdentityRegistry registry;
    QVERIFY2(registry.ready(), qPrintable(registry.errorCode()));

    const auto verifyUnavailable = [](const QVariantMap &resolved) {
        QVERIFY(!resolved.value(QStringLiteral("available")).toBool());
    };

    // Comics are excluded from the route entirely: no gcd or locg alias can
    // ever open Ratings & Reviews, and the pair itself is not admitted.
    verifyUnavailable(registry.resolve(
        QStringLiteral("tankoban"), QStringLiteral("comic"), QString(),
        {alias(QStringLiteral("gcd-series-id"), QStringLiteral("1234"))}));
    verifyUnavailable(registry.resolve(
        QStringLiteral("tankoban"), QStringLiteral("comic"), QString(),
        {alias(QStringLiteral("locg-series-id"), QStringLiteral("getcomics-slug"))}));
    verifyUnavailable(registry.resolve(
        QStringLiteral("tankoban"), QStringLiteral("comic"),
        QString::fromLatin1(kFrierenMediaId), {}));
    // A comic-seeded alias must not leak into the manga pair.
    verifyUnavailable(registry.resolve(
        QStringLiteral("tankoban"), QStringLiteral("manga"), QString(),
        {alias(QStringLiteral("gcd-series-id"), QStringLiteral("1234"))}));

    // Manga and books remain admitted and derive.
    QVERIFY(registry.resolve(
        QStringLiteral("tankoban"), QStringLiteral("manga"), QString(),
        {alias(QStringLiteral("tankoban-source-id"), QStringLiteral("mal:2"))})
        .value(QStringLiteral("available")).toBool());
    QVERIFY(registry.resolve(
        QStringLiteral("biblio"), QStringLiteral("book"), QString(),
        {alias(QStringLiteral("biblio-source-id"), QStringLiteral("/works/OL1W"))})
        .value(QStringLiteral("available")).toBool());
}

void tst_ratings_reviews_journey::
controllerOpenAcceptsDerivedCatalogueIdentity() {
    RuntimeFixture fixture;
    ColosseumTitleIdentityRegistry identityRegistry;
    QVERIFY2(identityRegistry.ready(), qPrintable(identityRegistry.errorCode()));
    RatingsReviewsProviderReadProjection providerRead;
    RatingsReviewsController controller(
        &fixture.runtime, &identityRegistry, &providerRead);

    const QVariantMap resolved = identityRegistry.resolve(
        QStringLiteral("theatre"), QStringLiteral("series"), QString(),
        {alias(QStringLiteral("theatre-source-id"), QStringLiteral("tt0903747"))});
    QVERIFY(resolved.value(QStringLiteral("available")).toBool());

    QVariantMap context = identityContext();
    QVariantMap identity = context.value(QStringLiteral("identity")).toMap();
    identity.insert(QStringLiteral("mediaId"),
                    resolved.value(QStringLiteral("mediaId")));
    context.insert(QStringLiteral("identity"), identity);

    const QVariantMap opened = controller.open(context, 91);
    QVERIFY2(opened.value(QStringLiteral("ok")).toBool(),
             qPrintable(opened.value(QStringLiteral("errorCode")).toString()));
    QCOMPARE(opened.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    QVERIFY(controller.routeActive());
}

void tst_ratings_reviews_journey::
controllerOpenRequiresRegistryValidatedIdentityTuple() {
    RuntimeFixture fixture;
    ColosseumTitleIdentityRegistry identityRegistry;
    QVERIFY2(identityRegistry.ready(), qPrintable(identityRegistry.errorCode()));
    RatingsReviewsProviderReadProjection providerRead;
    RatingsReviewsController controller(
        &fixture.runtime, &identityRegistry, &providerRead);

    RatingsReviewsStore *store = fixture.runtime.ratingsReviewsStore();
    ProfilePreferencesStore *preferences = fixture.runtime.preferencesStore();
    QVERIFY(store);
    QVERIFY(preferences);

    const quint64 storeRevision = store->revision();
    const int preferencesRevision = preferences->revision();

    auto rejectedContext = [](const QString &world,
                              const QString &kind,
                              const QString &mediaId) {
        QVariantMap context = identityContext();
        context.insert(QStringLiteral("identity"), QVariantMap{
            {QStringLiteral("world"), world},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("mediaId"), mediaId}});
        return context;
    };
    auto verifyRejected = [&](const QVariantMap &context, quint64 routeGeneration) {
        const QVariantMap result = controller.open(context, routeGeneration);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("identity_unavailable"));
        QCOMPARE(result.value(QStringLiteral("providerOperationCount")).toInt(), 0);
        QVERIFY(!controller.routeActive());
        QCOMPARE(store->revision(), storeRevision);
        QCOMPARE(preferences->revision(), preferencesRevision);
    };

    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("ct1:49f10000-0000-4000-8000-000000000099")),
        70);
    verifyRejected(
        rejectedContext(QStringLiteral("biblio"), QStringLiteral("book"),
                        QString::fromLatin1(kFrierenMediaId)),
        71);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("movie"),
                        QString::fromLatin1(kFrierenMediaId)),
        72);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("tt22248376")),
        73);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("C:/media/Frieren.mkv")),
        74);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("Frieren: Beyond Journey's End")),
        75);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("frieren-beyond-journeys-end")),
        76);
    verifyRejected(
        rejectedContext(QStringLiteral("theatre"), QStringLiteral("series"),
                        QStringLiteral("frieren")),
        77);

    const QVariantMap valid = controller.open(identityContext(), 78);
    QVERIFY2(valid.value(QStringLiteral("ok")).toBool(),
             qPrintable(valid.value(QStringLiteral("errorCode")).toString()));
    QCOMPARE(valid.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    QVERIFY(controller.routeActive());
    QCOMPARE(store->revision(), storeRevision);
    QCOMPARE(preferences->revision(), preferencesRevision);
}

void tst_ratings_reviews_journey::
canonicalSaveEditDeleteHasZeroProviderOperations() {
    RuntimeFixture fixture;
    ColosseumTitleIdentityRegistry identityRegistry;
    QVERIFY2(identityRegistry.ready(), qPrintable(identityRegistry.errorCode()));
    RatingsReviewsProviderReadProjection providerRead;
    RatingsReviewsController controller(
        &fixture.runtime, &identityRegistry, &providerRead);

    const QVariantMap opened = controller.open(identityContext(), 7);
    QVERIFY(opened.value(QStringLiteral("ok")).toBool());
    QCOMPARE(opened.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    const quint64 profileGeneration =
        opened.value(QStringLiteral("profileGeneration")).toULongLong();

    QVariantMap saved = controller.saveLocal(
        0.0, QStringLiteral("時間を感じる静かな旅。"), true,
        7, profileGeneration);
    QVERIFY2(saved.value(QStringLiteral("ok")).toBool(),
             qPrintable(saved.value(QStringLiteral("errorCode")).toString()));
    QCOMPARE(saved.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    QVERIFY(saved.value(QStringLiteral("canonical")).toMap()
                .value(QStringLiteral("hasRating")).toBool());
    QCOMPARE(saved.value(QStringLiteral("canonical")).toMap()
                 .value(QStringLiteral("rating")).toDouble(), 0.0);

    const QVariantMap cleared = controller.clearRating(7, profileGeneration);
    QVERIFY(cleared.value(QStringLiteral("ok")).toBool());
    QCOMPARE(cleared.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    const QVariantMap afterClear =
        cleared.value(QStringLiteral("canonical")).toMap();
    QVERIFY(!afterClear.value(QStringLiteral("hasRating")).toBool());
    QVERIFY(afterClear.value(QStringLiteral("hasReview")).toBool());
    QCOMPARE(afterClear.value(QStringLiteral("review")).toString(),
             QStringLiteral("時間を感じる静かな旅。"));

    const QVariantMap deleted = controller.deleteReview(7, profileGeneration);
    QVERIFY(deleted.value(QStringLiteral("ok")).toBool());
    QCOMPARE(deleted.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    QVERIFY(!deleted.value(QStringLiteral("canonical")).toMap()
                 .value(QStringLiteral("hasReview")).toBool());

    QString keyError;
    const QString recordKey = RatingsReviewsStore::recordKeyForIdentity(
        {QStringLiteral("theatre"), QStringLiteral("series"),
         QString::fromLatin1(kFrierenMediaId)}, &keyError);
    QVERIFY2(!recordKey.isEmpty(), qPrintable(keyError));
    QVERIFY(!fixture.runtime.ratingsReviewsStore()->recordsJson().contains(recordKey));
    QVERIFY(fixture.runtime.ratingsReviewsStore()->tombstonesJson().contains(recordKey));
}

void tst_ratings_reviews_journey::
staleRouteAndProfileGenerationsCannotMutate() {
    RuntimeFixture fixture;
    ColosseumTitleIdentityRegistry identityRegistry;
    QVERIFY2(identityRegistry.ready(), qPrintable(identityRegistry.errorCode()));
    RatingsReviewsProviderReadProjection providerRead;
    RatingsReviewsController controller(
        &fixture.runtime, &identityRegistry, &providerRead);

    const QVariantMap opened = controller.open(identityContext(), 11);
    QVERIFY(opened.value(QStringLiteral("ok")).toBool());
    const quint64 generation =
        opened.value(QStringLiteral("profileGeneration")).toULongLong();

    const quint64 before = fixture.runtime.ratingsReviewsStore()->revision();
    const QVariantMap staleRoute = controller.saveLocal(
        8.5, QStringLiteral("stale"), false, 10, generation);
    QVERIFY(!staleRoute.value(QStringLiteral("ok")).toBool());
    QCOMPARE(staleRoute.value(QStringLiteral("errorCode")).toString(),
             QStringLiteral("stale_generation"));
    QCOMPARE(staleRoute.value(QStringLiteral("providerOperationCount")).toInt(), 0);
    QCOMPARE(fixture.runtime.ratingsReviewsStore()->revision(), before);

    QString error;
    QVERIFY2(fixture.runtime.activateLocalOnlyProfile(&error), qPrintable(error));
    QVERIFY(!controller.routeActive());
    QVERIFY(controller.profileGeneration() > generation);
    const QVariantMap staleProfile = controller.saveLocal(
        8.5, QStringLiteral("late profile A"), false, 11, generation);
    QVERIFY(!staleProfile.value(QStringLiteral("ok")).toBool());
    QCOMPARE(staleProfile.value(QStringLiteral("providerOperationCount")).toInt(), 0);
}

void tst_ratings_reviews_journey::
productionProviderReadFailsClosedAndFixtureGateIsExact() {
    RatingsReviewsProviderReadProjection projection;
    const QVariantMap ids{{QStringLiteral("fixtureVariant"),
                           QStringLiteral("all_ready_visual")}};

    qunsetenv("COLOSSEUM_APPDATA_TAG");
    QVariantMap productionLike = projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QString::fromLatin1(kFrierenMediaId), ids, 4, 9);
    QCOMPARE(productionLike.value(QStringLiteral("aggregates")).toList().size(), 0);
    QCOMPARE(productionLike.value(QStringLiteral("reviews")).toList().size(), 0);
    QVERIFY(!productionLike.value(QStringLiteral("fixtureOnly")).toBool());

    qputenv("COLOSSEUM_APPDATA_TAG", "wrong-tag");
    QVariantMap wrongTag = projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QString::fromLatin1(kFrierenMediaId), ids, 4, 10);
    QCOMPARE(wrongTag.value(QStringLiteral("aggregates")).toList().size(), 0);

    qputenv("COLOSSEUM_APPDATA_TAG", "ratings-reviews-delivery-fixture");
    QVariantMap wrongTitle = projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QString::fromLatin1(kFrierenMediaId), ids, 12, 41);
    QCOMPARE(wrongTitle.value(QStringLiteral("aggregates")).toList().size(), 0);
    QCOMPARE(wrongTitle.value(QStringLiteral("reviews")).toList().size(), 0);
    QVERIFY(!wrongTitle.value(QStringLiteral("fixtureOnly")).toBool());

    QVariantMap fixture = projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series"), ids, 12, 42);
    QCOMPARE(fixture.value(QStringLiteral("aggregates")).toList().size(), 8);
    QCOMPARE(fixture.value(QStringLiteral("reviews")).toList().size(), 5);
    QVERIFY(fixture.value(QStringLiteral("fixtureOnly")).toBool());
    QVERIFY(projection.acceptsResult(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series"), 12, 42));
    QVERIFY(!projection.acceptsResult(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series"), 11, 41));
    qunsetenv("COLOSSEUM_APPDATA_TAG");
}

void tst_ratings_reviews_journey::
providerOrderingPreservesHiddenSlots() {
    RatingsReviewsProviderReadProjection projection;
    const QVariantList malformed{
        QStringLiteral("mal"), QStringLiteral("anilist"), QStringLiteral("mal"),
        QStringLiteral("unknown-fixture"), QStringLiteral("trakt"),
        QStringLiteral("simkl"), QStringLiteral("imdb"), QStringLiteral("tmdb"),
        QStringLiteral("metacritic")};
    QCOMPARE(
        variantStrings(projection.normalizeProviderOrder(malformed)),
        QStringList({QStringLiteral("mal"), QStringLiteral("anilist"),
                     QStringLiteral("trakt"), QStringLiteral("simkl"),
                     QStringLiteral("imdb"), QStringLiteral("tmdb"),
                     QStringLiteral("metacritic"),
                     QStringLiteral("rotten_tomatoes")}));

    const QVariantList full{
        QStringLiteral("mal"), QStringLiteral("anilist"), QStringLiteral("trakt"),
        QStringLiteral("simkl"), QStringLiteral("imdb"), QStringLiteral("tmdb"),
        QStringLiteral("rotten_tomatoes"), QStringLiteral("metacritic")};
    const QVariantMap moved = projection.moveVisibleProvider(
        full, {QStringLiteral("mal"), QStringLiteral("imdb")},
        QStringLiteral("imdb"), -1);
    QVERIFY(moved.value(QStringLiteral("changed")).toBool());
    QCOMPARE(
        variantStrings(moved.value(QStringLiteral("savedOrder")).toList()),
        QStringList({QStringLiteral("imdb"), QStringLiteral("anilist"),
                     QStringLiteral("trakt"), QStringLiteral("simkl"),
                     QStringLiteral("mal"), QStringLiteral("tmdb"),
                     QStringLiteral("rotten_tomatoes"),
                     QStringLiteral("metacritic")}));
    QCOMPARE(moved.value(QStringLiteral("announcement")).toString(),
             QStringLiteral("IMDb moved to position 1 of 2"));
}

void tst_ratings_reviews_journey::
sourceLinksAreNativeValidated() {
    QVERIFY(RatingsReviewsProviderReadProjection::isSafeSourceUrl(
        QUrl(QStringLiteral("https://tmdb.fixtures.invalid/review/1"))));
    QVERIFY(!RatingsReviewsProviderReadProjection::isSafeSourceUrl(
        QUrl(QStringLiteral("http://tmdb.fixtures.invalid/unsafe"))));
    QVERIFY(!RatingsReviewsProviderReadProjection::isSafeSourceUrl(
        QUrl(QStringLiteral("https://user:pass@tmdb.fixtures.invalid/review"))));
    QVERIFY(!RatingsReviewsProviderReadProjection::isSafeSourceUrl(
        QUrl(QStringLiteral(
            "https://tmdb.fixtures.invalid/review?access_token=secret"))));

    qputenv("COLOSSEUM_APPDATA_TAG", "ratings-reviews-delivery-fixture");
    RatingsReviewsProviderReadProjection projection;
    int openCount = 0;
    projection.setUrlOpenerForTests([&openCount](const QUrl &) {
        ++openCount;
        return true;
    });
    projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series"),
        {{QStringLiteral("fixtureVariant"), QStringLiteral("unsafe_link")}}, 1, 1);
    QVERIFY(!projection.reviewSourceAvailable(QStringLiteral("tmdb")));
    QVERIFY(!projection.openReviewSource(QStringLiteral("tmdb")));
    QCOMPARE(openCount, 0);

    projection.presentation(
        QStringLiteral("theatre"), QStringLiteral("series"),
        QStringLiteral("fixture-provider-read-series"),
        {{QStringLiteral("fixtureVariant"),
          QStringLiteral("all_ready_visual")}}, 1, 2);
    QVERIFY(projection.reviewSourceAvailable(QStringLiteral("anilist")));
    QVERIFY(projection.openReviewSource(QStringLiteral("anilist")));
    QCOMPARE(openCount, 1);
    qunsetenv("COLOSSEUM_APPDATA_TAG");
}

QTEST_MAIN(tst_ratings_reviews_journey)
#include "tst_ratings_reviews_journey.moc"
