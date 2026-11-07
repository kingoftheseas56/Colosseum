#include "account/NuvioNextUp.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const auto kAccount = QStringLiteral("08d67ad2-1abf-469d-9d88-e6bd17759106");
const auto kProfile = QStringLiteral("3d786faa-e366-42b0-844f-6b127d5137bf");
const auto kOtherProfile = QStringLiteral("0907a219-55a6-414f-8c80-0e38709d2ec4");
const auto kSeed = QStringLiteral("tmdb:1396|1|5");
const auto kDifferentSeed = QStringLiteral("tmdb:1396|1|6");
const auto kTimestamp = QStringLiteral("2026-11-07T12:34:56.123456+00:00");
const auto kNewTimestamp = QStringLiteral("2026-11-07T12:34:56.654321+00:00");
const auto kFeature = QStringLiteral("continue_watching_settings_payload");
const auto kList = QStringLiteral("dismissedNextUpKeys");

QJsonArray pull(const QJsonObject &settings, int profileIndex = 2) {
    return QJsonArray{QJsonObject{{"profile_id", profileIndex}, {"settings_json", settings},
                                  {"updated_at", kTimestamp}}};
}

QString compact(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject settingsWith(const QJsonObject &payload) {
    return {{"features", QJsonObject{{kFeature, compact(payload)}}}};
}

QJsonObject nextUpPayload(const QJsonObject &settings) {
    const auto feature = settings.value("features").toObject().value(kFeature);
    return QJsonDocument::fromJson(feature.toString().toUtf8()).object();
}
}

class NuvioNextUpTest final : public QObject {
    Q_OBJECT

private slots:
    void usesProviderSeedIdentity();
    void offlineEditsSurviveReopenAndScopeSwitch();
    void mergesExistingMobileBlobWithoutReplacingOtherSettings();
    void acceptsMissingBlobAndEmptyMobilePayload();
    void newerEditsSurviveInFlightAcknowledgement();
    void remoteChangesUpdateOfflineCache();
    void malformedDataCannotReplaceCacheOrClearPending();
};

void NuvioNextUpTest::usesProviderSeedIdentity() {
    QCOMPARE(NuvioNextUp::key(QStringLiteral(" tmdb:1396 "), 1, 5), kSeed);
    QCOMPARE(NuvioNextUp::key(QStringLiteral("tt123"), 0, -1), QStringLiteral("tt123|0|-1"));
    QCOMPARE(NuvioNextUp::key(QStringLiteral("addon:%2"), 1, 5), QStringLiteral("addon:%2|1|5"));
    QVERIFY(NuvioNextUp::key(QStringLiteral("tt123|x"), 1, 5).isEmpty());
    QVERIFY(NuvioNextUp::key(QStringLiteral("tt123"), -2, 5).isEmpty());
    QVERIFY(NuvioNextUp::key(QStringLiteral("tmdb:"), 1, 5).isEmpty());
}

void NuvioNextUpTest::offlineEditsSurviveReopenAndScopeSwitch() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(state.setDismissed(kSeed, true));
    QVERIFY(!state.setDismissed(QStringLiteral("tmdb:1396|1|05"), true));

    NuvioNextUp reopened;
    QVERIFY(reopened.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(reopened.hasPending());
    QVERIFY(reopened.isDismissed(kSeed));
    QVERIFY(!reopened.isDismissed(kDifferentSeed));
    QVERIFY(reopened.setDismissed(kSeed, false));
    QVERIFY(!reopened.isDismissed(kSeed));

    QVERIFY(reopened.activate(directory.path(), kAccount, kOtherProfile, 2));
    QVERIFY(!reopened.hasPending());
    QVERIFY(!reopened.isDismissed(kSeed));
    QVERIFY(reopened.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(reopened.hasPending());
    QVERIFY(!reopened.isDismissed(kSeed));
}

void NuvioNextUpTest::mergesExistingMobileBlobWithoutReplacingOtherSettings() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(state.setDismissed(kSeed, false));
    QVERIFY(state.setDismissed(kDifferentSeed, true));

    const auto legacy = QStringLiteral("tmdb:1396");
    const auto other = QStringLiteral("addon:show|4|12");
    const QJsonObject unrelated{{"enabled", true}, {"threshold", 9}};
    const QJsonObject blob{{"version", 7}, {"newTopLevelField", 21},
                           {"features", QJsonObject{
                               {"futureFeature", unrelated},
                               {kFeature, compact({{"futureInnerField", unrelated},
                                                    {kList, QJsonArray{legacy, kSeed, other}}})}}}};

    std::optional<NuvioNextUp::Push> push;
    QVERIFY(state.applyPull(pull(blob), &push));
    QVERIFY(push);
    QCOMPARE(push->expectedUpdatedAt.toString(), kTimestamp);
    QCOMPARE(push->settings.value("version"), QJsonValue(7));
    QCOMPARE(push->settings.value("newTopLevelField"), QJsonValue(21));
    QCOMPARE(push->settings.value("features").toObject().value("futureFeature").toObject(), unrelated);
    QCOMPARE(nextUpPayload(push->settings).value("futureInnerField").toObject(), unrelated);
    QCOMPARE(nextUpPayload(push->settings).value(kList).toArray(),
             (QJsonArray{legacy, other, kDifferentSeed}));
    QVERIFY(!state.isDismissed(kSeed));
    QVERIFY(state.isDismissed(legacy));
    QVERIFY(state.isDismissed(kDifferentSeed));
    QVERIFY(state.isDismissed(other));
}

void NuvioNextUpTest::acceptsMissingBlobAndEmptyMobilePayload() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(state.setDismissed(kSeed, true));
    std::optional<NuvioNextUp::Push> push;
    QVERIFY(state.applyPull(QJsonArray{}, &push));
    QVERIFY(push);
    QVERIFY(push->expectedUpdatedAt.isNull());
    QCOMPARE(nextUpPayload(push->settings).value(kList).toArray(), QJsonArray{kSeed});
    const auto previous = *push;

    QVERIFY(state.applyPull(pull({{"features", QJsonObject{{kFeature, QString()}}}}), &push));
    QVERIFY(push);
    QCOMPARE(push->expectedUpdatedAt.toString(), kTimestamp);
    QCOMPARE(nextUpPayload(push->settings).value(kList).toArray(), QJsonArray{kSeed});
    QVERIFY(!state.acknowledge(previous, kNewTimestamp));
}

void NuvioNextUpTest::newerEditsSurviveInFlightAcknowledgement() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(state.setDismissed(kSeed, true));
    QVERIFY(state.setDismissed(kDifferentSeed, true));
    std::optional<NuvioNextUp::Push> push;
    QVERIFY(state.applyPull(QJsonArray{}, &push));
    QVERIFY(push);
    QVERIFY(state.setDismissed(kSeed, false));
    QVERIFY(state.setDismissed(kDifferentSeed, false));
    QVERIFY(state.setDismissed(kDifferentSeed, true));
    QVERIFY(state.acknowledge(*push, kNewTimestamp));
    QVERIFY(!state.acknowledge(*push, kNewTimestamp));

    NuvioNextUp reopened;
    QVERIFY(reopened.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(!reopened.isDismissed(kSeed));
    QVERIFY(reopened.isDismissed(kDifferentSeed));
    QVERIFY(reopened.hasPending());
    const auto remote = pull(settingsWith({{kList, QJsonArray{kSeed}}}));
    QVERIFY(reopened.applyPull(remote, &push));
    QVERIFY(push);
    QCOMPARE(nextUpPayload(push->settings).value(kList).toArray(), QJsonArray{kDifferentSeed});
    QVERIFY(reopened.acknowledge(*push, kNewTimestamp));
    QVERIFY(!reopened.hasPending());
}

void NuvioNextUpTest::remoteChangesUpdateOfflineCache() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    std::optional<NuvioNextUp::Push> push;
    QVERIFY(state.applyPull(pull(settingsWith({{kList, QJsonArray{kSeed}}})), &push));
    QVERIFY(!push);
    QVERIFY(state.isDismissed(kSeed));
    QVERIFY(!state.isDismissed(kDifferentSeed));

    NuvioNextUp reopened;
    QVERIFY(reopened.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(reopened.isDismissed(kSeed));
    QVERIFY(reopened.applyPull(pull(settingsWith({{kList, QJsonArray{kDifferentSeed}}})), &push));
    QVERIFY(!push);
    QVERIFY(!reopened.isDismissed(kSeed));
    QVERIFY(reopened.isDismissed(kDifferentSeed));
}

void NuvioNextUpTest::malformedDataCannotReplaceCacheOrClearPending() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    NuvioNextUp state;
    QVERIFY(state.activate(directory.path(), kAccount, kProfile, 2));
    std::optional<NuvioNextUp::Push> push;
    QVERIFY(state.applyPull(pull(settingsWith({{kList, QJsonArray{kSeed}}})), &push));
    QVERIFY(state.setDismissed(kDifferentSeed, true));
    QVERIFY(!state.applyPull(pull({{"features", QJsonObject{{kFeature, QStringLiteral("{bad")}}}}), &push));
    QVERIFY(!state.applyPull(pull(settingsWith({{kList, QJsonArray{kSeed, 99}}})), &push));
    QVERIFY(!state.applyPull(pull({{"version", QStringLiteral("invalid")}}), &push));
    QVERIFY(!state.applyPull(pull(settingsWith({}), 3), &push));
    QVERIFY(!state.applyPull(QJsonObject{}, &push));
    QVERIFY(!push);
    QVERIFY(state.isDismissed(kSeed));
    QVERIFY(state.isDismissed(kDifferentSeed));
    QVERIFY(state.applyPull(pull(settingsWith({{kList, QJsonArray{kSeed}}})), &push));
    QVERIFY(push);
    QVERIFY(!state.acknowledge(*push, QStringLiteral("invalid")));
    QVERIFY(state.hasPending());

    const auto files = QDir(directory.path()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile corrupt(files.first().absoluteFilePath());
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(corrupt.write("{bad"), 4);
    corrupt.close();
    QVERIFY(!state.activate(directory.path(), kAccount, kProfile, 2));
    QVERIFY(!state.setDismissed(kSeed, false));
}

QTEST_GUILESS_MAIN(NuvioNextUpTest)
#include "tst_nuvio_next_up.moc"
