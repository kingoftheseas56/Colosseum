#include "account/ProfilePreferencesStore.h"
#include "account/RatingsReviewsConversionMap.h"
#include "account/RatingsReviewsConversionSyncAdapter.h"

#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
RatingsReviewsConversionMap recommendation(
    const QString &provider = QStringLiteral("fixture-a"),
    const QString &domain = QStringLiteral("fixture-halfpoint-v1")) {
    QString error;
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    const auto map = RatingsReviewsConversionMap::recommended(
        provider, domain, 1, hook, &error);
    if (!map.has_value())
        qFatal("conversion fixture failed: %s", qPrintable(error));
    return *map;
}

QString prefsPath(const QTemporaryDir &temp, const QString &name) {
    return temp.filePath(name + QStringLiteral(".ini"));
}
}

class tst_ratings_reviews_conversion final : public QObject {
    Q_OBJECT
private slots:
    void deterministicRecommendationRoundTrips();
    void validatorRejectsWrongCountIllegalAndNonMonotonic();
    void unavailableZeroIsNotARealZero();
    void preferencesUseExactKeysAndReload();
    void failedValidationAndInjectedWriteFailurePreservePriorMap();
    void corruptPersistedMapFailsClosed();
    void syncAdapterExportsAndAppliesWithoutRemoteEcho();
    void twoProfileSyncPreservesExactDigest();
    void productionRejectsSyntheticFixtureMaps();
};

void tst_ratings_reviews_conversion::deterministicRecommendationRoundTrips() {
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    const auto map = recommendation();
    QCOMPARE(map.outputs.size(), 21);
    for (int i = 0; i < 21; ++i)
        QCOMPARE(map.outputs.at(i).toDouble(), i * 0.5);
    QCOMPARE(
        map.digest(),
        QStringLiteral("87c3c0bdac4570e19c97c872814f23cddd8d553612a2a525944a38cf97e0cca8"));

    QString error;
    const auto decoded = RatingsReviewsConversionMap::fromJson(
        map.toJson(), hook, &error);
    QVERIFY2(decoded.has_value(), qPrintable(error));
    QCOMPARE(decoded->digest(), map.digest());

    const auto reset = RatingsReviewsConversionMap::recommended(
        map.providerId, map.domainId, map.domainVersion, hook, &error);
    QVERIFY2(reset.has_value(), qPrintable(error));
    QCOMPARE(reset->outputs, map.outputs);
    QCOMPARE(reset->digest(), map.digest());
}

void tst_ratings_reviews_conversion::validatorRejectsWrongCountIllegalAndNonMonotonic() {
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    QString error;

    auto wrongCount = recommendation();
    wrongCount.outputs.removeLast();
    QVERIFY(!RatingsReviewsConversionMap::validate(wrongCount, hook, &error));

    auto illegal = recommendation();
    illegal.outputs[10] = 5.25;
    QVERIFY(!RatingsReviewsConversionMap::validate(illegal, hook, &error));

    auto reversed = recommendation();
    qSwap(reversed.outputs[10], reversed.outputs[11]);
    QVERIFY(!RatingsReviewsConversionMap::validate(reversed, hook, &error));
}

void tst_ratings_reviews_conversion::unavailableZeroIsNotARealZero() {
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    QString error;
    const auto map = RatingsReviewsConversionMap::recommended(
        QStringLiteral("fixture-a"),
        QStringLiteral("fixture-no-zero-v1"),
        1,
        hook,
        &error);
    QVERIFY2(map.has_value(), qPrintable(error));
    QCOMPARE(map->outputs.size(), 21);
    QVERIFY(map->outputs.first().isNull());

    bool available = true;
    const QJsonValue zero = map->translatedOutput(0.0, &available, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!available);
    QVERIFY(zero.isNull());

    const QJsonValue half = map->translatedOutput(0.5, &available, &error);
    QVERIFY(available);
    QCOMPARE(half.toDouble(), 0.5);
}

void tst_ratings_reviews_conversion::preferencesUseExactKeysAndReload() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    const QString path = prefsPath(temp, QStringLiteral("prefs"));

    ProfilePreferencesStore store(path, hook);
    QCOMPARE(
        store.ratingsReviewsProviderOrder(),
        RatingsReviewsConversionMap::canonicalProviderIds());
    QVERIFY(!store.hasRatingsReviewsProviderOrder());
    QVERIFY(!store.hasRatingsReviewsDefaultRatingDestinations());
    QVERIFY(!store.hasRatingsReviewsDefaultReviewDestinations());

    const QStringList order{
        QStringLiteral("anilist"), QStringLiteral("mal"), QStringLiteral("trakt"),
        QStringLiteral("simkl"), QStringLiteral("imdb"), QStringLiteral("tmdb"),
        QStringLiteral("rotten_tomatoes"), QStringLiteral("metacritic")};
    QVERIFY(store.setRatingsReviewsProviderOrder(order));
    QVERIFY(store.setRatingsReviewsDefaultRatingDestinations(
        {QStringLiteral("anilist"), QStringLiteral("mal")}));
    QVERIFY(store.setRatingsReviewsDefaultReviewDestinations(
        {QStringLiteral("trakt")}));
    QVERIFY(!store.setRatingsReviewsProviderOrder({QStringLiteral("rt")}));

    const auto map = recommendation();
    QVERIFY(store.setRatingsReviewsConversionMap(map));

    QSettings raw(path, QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("ratingsReviews/providerOrder")).toStringList(), order);
    QCOMPARE(
        raw.value(QStringLiteral("ratingsReviews/defaultRatingDestinations")).toStringList(),
        QStringList({QStringLiteral("anilist"), QStringLiteral("mal")}));
    QVERIFY(raw.contains(QStringLiteral("ratingsReviews/conversionMaps/fixture-a")));

    ProfilePreferencesStore reloaded(path, hook);
    QCOMPARE(reloaded.ratingsReviewsProviderOrder(), order);
    const auto restored = reloaded.ratingsReviewsConversionMap(QStringLiteral("fixture-a"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->digest(), map.digest());
}

void tst_ratings_reviews_conversion::failedValidationAndInjectedWriteFailurePreservePriorMap() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto gate = RatingsReviewsConversionTestHook::settingsFailureGate();
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains(gate);
    ProfilePreferencesStore store(prefsPath(temp, QStringLiteral("prefs")), hook);

    const auto original = recommendation();
    QVERIFY(store.setRatingsReviewsConversionMap(original));

    auto invalid = original;
    qSwap(invalid.outputs[4], invalid.outputs[5]);
    QVERIFY(!store.setRatingsReviewsConversionMap(invalid));
    QCOMPARE(store.ratingsReviewsConversionMap(original.providerId)->digest(), original.digest());

    auto changed = original;
    changed.outputs[20] = QJsonValue();
    *gate = true;
    QVERIFY(!store.setRatingsReviewsConversionMap(changed));
    *gate = false;
    QCOMPARE(store.ratingsReviewsConversionMap(original.providerId)->digest(), original.digest());
}

void tst_ratings_reviews_conversion::corruptPersistedMapFailsClosed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = prefsPath(temp, QStringLiteral("prefs"));
    auto corrupt = recommendation();
    qSwap(corrupt.outputs[7], corrupt.outputs[8]);

    QSettings raw(path, QSettings::IniFormat);
    raw.setValue(
        QStringLiteral("ratingsReviews/conversionMaps/fixture-a"),
        QJsonDocument(corrupt.toJson()).toJson(QJsonDocument::Compact));
    raw.sync();

    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    ProfilePreferencesStore store(path, hook);
    QString error;
    QVERIFY(!store.ratingsReviewsConversionMapsHealthy(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.ratingsReviewsConversionMaps().isEmpty());
}

void tst_ratings_reviews_conversion::syncAdapterExportsAndAppliesWithoutRemoteEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    ProfilePreferencesStore store(prefsPath(temp, QStringLiteral("source")), hook);
    RatingsReviewsConversionSyncAdapter adapter(&store);
    QSignalSpy localSpy(&adapter, &SyncAdapter::localMutationAvailable);

    const auto map = recommendation();
    QVERIFY(store.setRatingsReviewsConversionMap(map));
    QCOMPARE(localSpy.count(), 1);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(adapter.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records.first().recordKey, QStringLiteral("conversion/fixture-a"));

    auto remote = map;
    remote.outputs[20] = QJsonValue();
    QVERIFY(adapter.applyRemote(
        QStringLiteral("conversion/fixture-a"),
        SyncWireOperation::Put,
        remote.toJson(),
        1,
        &error));
    QCOMPARE(localSpy.count(), 1);
    QCOMPARE(store.ratingsReviewsConversionMap(QStringLiteral("fixture-a"))->digest(), remote.digest());

    QVERIFY(adapter.applyRemote(
        QStringLiteral("conversion/fixture-a"),
        SyncWireOperation::Delete,
        QJsonValue(),
        1,
        &error));
    QCOMPARE(localSpy.count(), 1);
    QVERIFY(!store.ratingsReviewsConversionMap(QStringLiteral("fixture-a")).has_value());
}

void tst_ratings_reviews_conversion::twoProfileSyncPreservesExactDigest() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto hook = RatingsReviewsConversionTestHook::syntheticDomains();
    ProfilePreferencesStore first(prefsPath(temp, QStringLiteral("first")), hook);
    ProfilePreferencesStore second(prefsPath(temp, QStringLiteral("second")), hook);
    RatingsReviewsConversionSyncAdapter source(&first);
    RatingsReviewsConversionSyncAdapter destination(&second);

    const auto map = recommendation(QStringLiteral("fixture-b"));
    QVERIFY(first.setRatingsReviewsConversionMap(map));

    SyncAdapterExport exported;
    QString error;
    QVERIFY2(source.exportSnapshot(&exported, &error), qPrintable(error));
    QCOMPARE(exported.records.size(), 1);
    QVERIFY(destination.applyRemote(
        exported.records.first().recordKey,
        SyncWireOperation::Put,
        exported.records.first().payload,
        1,
        &error));

    const auto received = second.ratingsReviewsConversionMap(QStringLiteral("fixture-b"));
    QVERIFY(received.has_value());
    QCOMPARE(received->digest(), map.digest());
}

void tst_ratings_reviews_conversion::productionRejectsSyntheticFixtureMaps() {
    const auto fixture = recommendation();
    QString error;
    QVERIFY(!RatingsReviewsConversionMap::validate(fixture, {}, &error));
    QVERIFY(!error.isEmpty());

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProfilePreferencesStore production(prefsPath(temp, QStringLiteral("production")));
    RatingsReviewsConversionSyncAdapter adapter(&production);
    SyncAdapterExport snapshot;
    QVERIFY(adapter.exportSnapshot(&snapshot, &error));
    QVERIFY(snapshot.records.isEmpty());
}

QTEST_MAIN(tst_ratings_reviews_conversion)
#include "tst_ratings_reviews_conversion.moc"
