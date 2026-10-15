#include "account/RatingsReviewsStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>

namespace {

using Store = RatingsReviewsStore;
using Identity = Store::Identity;
using Record = Store::Record;

Identity identity(const QString &mediaId = QStringLiteral("media-1"))
{
    return {QStringLiteral("theatre"), QStringLiteral("series"), mediaId};
}
Store::Clock clockFrom(qint64 first, qint64 step = 100)
{
    return [next = first, step]() mutable {
        const qint64 value = next;
        next += step;
        return value;
    };
}

QJsonObject recordJson(const Identity &id,
                       const std::optional<double> &rating,
                       const std::optional<QString> &review,
                       bool spoiler,
                       qint64 createdAtMs,
                       qint64 updatedAtMs)
{
    QJsonObject object;
    object.insert(QStringLiteral("world"), id.world);
    object.insert(QStringLiteral("kind"), id.kind);
    object.insert(QStringLiteral("media_id"), id.mediaId);
    object.insert(QStringLiteral("rating"),
                  rating ? QJsonValue(*rating) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("review"),
                  review ? QJsonValue(*review) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("spoiler"), spoiler);
    object.insert(QStringLiteral("created_at_ms"),
                  static_cast<double>(createdAtMs));
    object.insert(QStringLiteral("updated_at_ms"),
                  static_cast<double>(updatedAtMs));
    return object;
}

QJsonObject tombstoneJson(qint64 deletedAtMs)
{
    return {{QStringLiteral("deleted_at_ms"),
             static_cast<double>(deletedAtMs)}};
}

QJsonObject storeJson(quint64 revision,
                      const QJsonObject &records,
                      const QJsonObject &tombstones)
{
    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("revision"), static_cast<double>(revision)},
        {QStringLiteral("records"), records},
        {QStringLiteral("tombstones"), tombstones},
    };
}
QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

QByteArray tieDigest(const QJsonObject &record)
{
    QJsonArray array;
    array.append(record.value(QStringLiteral("world")));
    array.append(record.value(QStringLiteral("kind")));
    array.append(record.value(QStringLiteral("media_id")));
    array.append(record.value(QStringLiteral("rating")));
    array.append(record.value(QStringLiteral("review")));
    array.append(record.value(QStringLiteral("spoiler")));
    array.append(record.value(QStringLiteral("created_at_ms")));
    array.append(record.value(QStringLiteral("updated_at_ms")));
    return QCryptographicHash::hash(
        QJsonDocument(array).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
}
bool writeObject(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size();
}

class tst_ratings_reviews_store : public QObject
{
    Q_OBJECT

private slots:
    void identity_golden_vectors();
    void identity_rejects_noncanonical_world_and_kind();
    void legal_ratings_data();
    void legal_ratings();
    void invalid_ratings_data();
    void invalid_ratings();
    void review_boundary_unicode_and_empty();
    void independent_clears_tombstone_restart_recreate();
    void no_op_and_commit_before_signal();
    void remote_apply_is_non_echoing();
    void restore_advances_destination_revision_once();
    void deterministic_merge();
    void malformed_state_fails_closed();
    void persistence_failure_is_atomic();
    void restart_persists_exact_state_and_revision();
};

} // namespace
void tst_ratings_reviews_store::identity_golden_vectors()
{
    QString error;
    QCOMPARE(Store::recordKeyForIdentity(
                 identity(QStringLiteral("canonical-colosseum-id")), &error),
             QStringLiteral("rr1:6bafc00634639f004882d0416bff84791f6e7708fe349948fc06d1d4a92969a9"));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const Identity left{QStringLiteral("a"), QStringLiteral("bc"), QStringLiteral("def")};
    const Identity right{QStringLiteral("ab"), QStringLiteral("c"), QStringLiteral("def")};
    QCOMPARE(Store::recordKeyForIdentity(left),
             QStringLiteral("rr1:80854f715f7e16456219cb83f56df8e34ccfac0d06ce7c88e992426e24f96639"));
    QCOMPARE(Store::recordKeyForIdentity(right),
             QStringLiteral("rr1:760ee33da75c545aa2eccbd6632610f10e4bdacfc82b0256cef25efbc8f73acb"));
    QVERIFY(Store::recordKeyForIdentity(left) != Store::recordKeyForIdentity(right));

    const Identity control{QStringLiteral("tankoban"), QStringLiteral("manga"),
                           QString::fromUtf8("id:\0/\n;|", 8)};
    QCOMPARE(Store::recordKeyForIdentity(control),
             QStringLiteral("rr1:9f4115218665ba3b5b27c1e1591fc7eb40a89c52403bdced20d57f2cafce6f23"));
}
void tst_ratings_reviews_store::identity_rejects_noncanonical_world_and_kind()
{
    const QList<Identity> invalid{
        {QString(), QStringLiteral("series"), QStringLiteral("id")},
        {QStringLiteral("Theatre"), QStringLiteral("series"), QStringLiteral("id")},
        {QStringLiteral(" theatre"), QStringLiteral("series"), QStringLiteral("id")},
        {QStringLiteral("theatre"), QString(), QStringLiteral("id")},
        {QStringLiteral("theatre"), QStringLiteral("Series"), QStringLiteral("id")},
        {QStringLiteral("theatre"), QStringLiteral("series "), QStringLiteral("id")},
        {QStringLiteral("theatre"), QStringLiteral("series"), QString()},
    };
    for (const Identity &value : invalid) {
        QString error;
        QVERIFY2(Store::recordKeyForIdentity(value, &error).isEmpty(),
                 qPrintable(QStringLiteral("unexpected key for invalid identity")));
        QVERIFY(!error.isEmpty());
    }
}
void tst_ratings_reviews_store::legal_ratings_data()
{
    QTest::addColumn<double>("rating");
    for (int half = 0; half <= 20; ++half) {
        const double rating = static_cast<double>(half) / 2.0;
        const QByteArray name = QByteArray("rating-") + QByteArray::number(rating, 'f', 1);
        QTest::newRow(name.constData()) << rating;
    }
}

void tst_ratings_reviews_store::legal_ratings()
{
    QFETCH(double, rating);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Store store(dir.filePath(QStringLiteral("ratings-reviews.json")), clockFrom(1000));
    QVERIFY(store.healthy());

    Store::CommitResult result;
    QString error;
    QVERIFY2(store.setRating(identity(), rating, &result, &error), qPrintable(error));
    QVERIFY(result.changed);
    QCOMPARE(result.revision, quint64(1));
    const auto saved = store.record(identity());
    QVERIFY(saved.has_value());
    QVERIFY(saved->rating.has_value());
    QCOMPARE(*saved->rating, rating);
}

void tst_ratings_reviews_store::invalid_ratings_data()
{
    QTest::addColumn<double>("rating");
    QTest::newRow("below") << -0.5;
    QTest::newRow("above") << 10.5;
    QTest::newRow("quarter-step") << 0.25;
    QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("infinity") << std::numeric_limits<double>::infinity();
}
void tst_ratings_reviews_store::invalid_ratings()
{
    QFETCH(double, rating);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Store store(dir.filePath(QStringLiteral("ratings-reviews.json")), clockFrom(1000));
    QSignalSpy changed(&store, &Store::changed);
    QSignalSpy dirty(&store, &Store::syncDirty);

    QString error;
    QVERIFY(!store.setRating(identity(), rating, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.revision(), quint64(0));
    QVERIFY(!store.record(identity()).has_value());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(dirty.count(), 0);
}
void tst_ratings_reviews_store::review_boundary_unicode_and_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Store store(dir.filePath(QStringLiteral("ratings-reviews.json")), clockFrom(1000));

    const QString boundary(16384, QLatin1Char('a'));
    QString error;
    QVERIFY2(store.setReview(identity(), boundary, false, nullptr, &error), qPrintable(error));
    QCOMPARE(store.record(identity())->review->toUtf8().size(), 16384);

    const quint64 before = store.revision();
    const QString over(16385, QLatin1Char('b'));
    QVERIFY(!store.setReview(identity(), over, false, nullptr, &error));
    QCOMPARE(store.revision(), before);
    QCOMPARE(*store.record(identity())->review, boundary);

    const QString exact = QString::fromUtf8("  Cafe\xCC\x81 \xF0\x9F\xA7\xAA\n\xE8\xB7\xAF\xE5\xBE\x84\t  ");
    QVERIFY2(store.setReview(identity(), exact, true, nullptr, &error), qPrintable(error));
    QCOMPARE(*store.record(identity())->review, exact);
    QVERIFY(store.record(identity())->spoiler);
    QVERIFY2(store.setReview(identity(), QString(), true, nullptr, &error), qPrintable(error));
    const auto empty = store.record(identity());
    QVERIFY(empty->review.has_value());
    QVERIFY(empty->review->isEmpty());
    QVERIFY(empty->spoiler);

    QVERIFY2(store.deleteReview(identity(), nullptr, &error), qPrintable(error));
    QVERIFY(!store.record(identity()).has_value());
    const QString key = Store::recordKeyForIdentity(identity());
    QVERIFY(store.tombstonesJson().contains(key));
}
void tst_ratings_reviews_store::independent_clears_tombstone_restart_recreate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ratings-reviews.json"));
    const Identity id = identity();
    const QString key = Store::recordKeyForIdentity(id);

    Store store(path, clockFrom(1000, 100));
    QString error;
    QVERIFY2(store.saveLocal(id, std::optional<double>(8.0),
                             std::optional<QString>(QStringLiteral("review")),
                             true, nullptr, &error), qPrintable(error));
    QVERIFY2(store.clearRating(id, nullptr, &error), qPrintable(error));
    auto current = store.record(id);
    QVERIFY(current.has_value());
    QVERIFY(!current->rating.has_value());
    QCOMPARE(*current->review, QStringLiteral("review"));
    QVERIFY(current->spoiler);

    QVERIFY2(store.setRating(id, 6.0, nullptr, &error), qPrintable(error));
    QVERIFY2(store.deleteReview(id, nullptr, &error), qPrintable(error));
    current = store.record(id);
    QVERIFY(current.has_value());
    QCOMPARE(*current->rating, 6.0);
    QVERIFY(!current->review.has_value());
    QVERIFY(!current->spoiler);

    QVERIFY2(store.clearRating(id, nullptr, &error), qPrintable(error));
    QVERIFY(!store.record(id).has_value());
    QVERIFY(store.tombstonesJson().contains(key));
    const qint64 deletedAt =
        store.tombstonesJson().value(key).toObject()
            .value(QStringLiteral("deleted_at_ms")).toInteger();
    QVERIFY(deletedAt > 0);
    const quint64 tombstoneRevision = store.revision();

    Store reopened(path, clockFrom(9000, 100));
    QVERIFY(reopened.healthy());
    QCOMPARE(reopened.revision(), tombstoneRevision);
    QVERIFY(!reopened.record(id).has_value());
    QCOMPARE(reopened.tombstonesJson(), store.tombstonesJson());
    QVERIFY2(reopened.setReview(id, QStringLiteral("reborn"), false, nullptr, &error),
             qPrintable(error));
    const auto recreated = reopened.record(id);
    QVERIFY(recreated.has_value());
    QCOMPARE(recreated->createdAtMs, qint64(9000));
    QCOMPARE(recreated->updatedAtMs, qint64(9000));
    QVERIFY(!reopened.tombstonesJson().contains(key));
    QCOMPARE(reopened.revision(), tombstoneRevision + 1);
}
void tst_ratings_reviews_store::no_op_and_commit_before_signal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ratings-reviews.json"));
    Store store(path, clockFrom(1000, 100));
    QSignalSpy changed(&store, &Store::changed);
    QSignalSpy dirty(&store, &Store::syncDirty);

    bool signalObservedDurableState = false;
    connect(&store, &Store::changed, &store, [&] {
        const QJsonObject disk = readObject(path);
        signalObservedDurableState =
            disk.value(QStringLiteral("revision")).toInteger()
                == static_cast<qint64>(store.revision())
            && disk.value(QStringLiteral("records")).toObject()
                == store.recordsJson();
    });

    Store::CommitResult first;
    QString error;
    QVERIFY2(store.saveLocal(identity(), std::optional<double>(0.0),
                             std::optional<QString>(QStringLiteral("zero is valid")),
                             false, &first, &error), qPrintable(error));
    QVERIFY(first.changed);
    QCOMPARE(first.revision, quint64(1));
    QVERIFY(signalObservedDurableState);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(dirty.count(), 1);

    Store::CommitResult second;
    QVERIFY2(store.saveLocal(identity(), std::optional<double>(0.0),
                             std::optional<QString>(QStringLiteral("zero is valid")),
                             false, &second, &error), qPrintable(error));
    QVERIFY(!second.changed);
    QCOMPARE(second.revision, quint64(1));
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(dirty.count(), 1);
}
void tst_ratings_reviews_store::remote_apply_is_non_echoing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Store store(dir.filePath(QStringLiteral("ratings-reviews.json")), clockFrom(5000));
    const Identity id = identity(QStringLiteral("remote"));
    const QString key = Store::recordKeyForIdentity(id);
    Record remote{id, 8.5, QStringLiteral("/home/me/opinion"),
                  true, 100, 200};

    QSignalSpy changed(&store, &Store::changed);
    QSignalSpy dirty(&store, &Store::syncDirty);
    QSignalSpy applied(&store, &Store::remoteApplied);
    Store::CommitResult result;
    QString error;
    QVERIFY2(store.applySyncedPut(key, remote, &result, &error), qPrintable(error));
    QVERIFY(result.changed);
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(dirty.count(), 0);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(*store.record(id)->review, QStringLiteral("/home/me/opinion"));

    Record invalidRemote = remote;
    invalidRemote.rating = std::numeric_limits<double>::quiet_NaN();
    const quint64 beforeInvalidRemote = store.revision();
    QVERIFY(!store.applySyncedPut(key, invalidRemote, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.revision(), beforeInvalidRemote);
    QCOMPARE(*store.record(id)->rating, 8.5);

    QVERIFY2(store.applySyncedPut(key, remote, &result, &error), qPrintable(error));
    QVERIFY(!result.changed);
    QCOMPARE(store.revision(), quint64(1));
    QCOMPARE(applied.count(), 1);

    const QString wrongKey =
        QStringLiteral("rr1:0000000000000000000000000000000000000000000000000000000000000000");
    QVERIFY(!store.applySyncedPut(wrongKey, remote, nullptr, &error));
    QCOMPARE(store.revision(), quint64(1));

    QVERIFY2(store.applySyncedDelete(key, 777, &result, &error), qPrintable(error));
    QVERIFY(result.changed);
    QCOMPARE(store.revision(), quint64(2));
    QVERIFY(!store.record(id).has_value());
    QCOMPARE(store.tombstonesJson().value(key).toObject()
                 .value(QStringLiteral("deleted_at_ms")).toInteger(), qint64(777));
    QCOMPARE(dirty.count(), 0);
    QCOMPARE(applied.count(), 2);
}
void tst_ratings_reviews_store::restore_advances_destination_revision_once()
{
    QTemporaryDir sourceDir;
    QTemporaryDir destinationDir;
    QVERIFY(sourceDir.isValid());
    QVERIFY(destinationDir.isValid());

    Store source(sourceDir.filePath(QStringLiteral("ratings-reviews.json")), clockFrom(1000));
    QString error;
    QVERIFY2(source.setRating(identity(QStringLiteral("source")), 7.5, nullptr, &error),
             qPrintable(error));
    QVERIFY2(source.setReview(identity(QStringLiteral("deleted")),
                              QStringLiteral("gone"), false, nullptr, &error),
             qPrintable(error));
    QVERIFY2(source.deleteReview(identity(QStringLiteral("deleted")), nullptr, &error),
             qPrintable(error));

    Store destination(destinationDir.filePath(QStringLiteral("ratings-reviews.json")),
                      clockFrom(9000));
    QVERIFY2(destination.setRating(identity(QStringLiteral("destination")), 2.0,
                                   nullptr, &error), qPrintable(error));
    const quint64 before = destination.revision();
    QSignalSpy changed(&destination, &Store::changed);
    QSignalSpy dirty(&destination, &Store::syncDirty);
    QSignalSpy remote(&destination, &Store::remoteApplied);
    Store::CommitResult result;
    QVERIFY2(destination.restoreCanonicalState(source.recordsJson(),
                                                source.tombstonesJson(),
                                                &result, &error),
             qPrintable(error));
    QVERIFY(result.changed);
    QCOMPARE(destination.revision(), before + 1);
    QCOMPARE(destination.recordsJson(), source.recordsJson());
    QCOMPARE(destination.tombstonesJson(), source.tombstonesJson());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(dirty.count(), 0);
    QCOMPARE(remote.count(), 0);

    QVERIFY2(destination.restoreCanonicalState(source.recordsJson(),
                                                source.tombstonesJson(),
                                                &result, &error),
             qPrintable(error));
    QCOMPARE(destination.revision(), before + 2);
    QCOMPARE(changed.count(), 2);
}
void tst_ratings_reviews_store::deterministic_merge()
{
    const Identity id = identity(QStringLiteral("merge"));
    const QString key = Store::recordKeyForIdentity(id);
    const QJsonObject alpha = recordJson(
        id, 7.0, QStringLiteral("alpha"), false, 10, 500);
    const QJsonObject beta = recordJson(
        id, 8.0, QStringLiteral("beta"), true, 20, 500);
    const QJsonObject alphaRecords{{key, alpha}};
    const QJsonObject betaRecords{{key, beta}};

    QJsonObject abRecords;
    QJsonObject abTombstones;
    QJsonObject baRecords;
    QJsonObject baTombstones;
    QString error;
    QVERIFY2(Store::mergeCanonicalState(
                 alphaRecords, {}, betaRecords, {},
                 &abRecords, &abTombstones, &error), qPrintable(error));
    QVERIFY2(Store::mergeCanonicalState(
                 betaRecords, {}, alphaRecords, {},
                 &baRecords, &baTombstones, &error), qPrintable(error));
    QCOMPARE(abRecords, baRecords);
    QCOMPARE(abTombstones, baTombstones);
    QVERIFY(abTombstones.isEmpty());
    const QJsonObject expected =
        tieDigest(alpha) > tieDigest(beta) ? alpha : beta;
    QCOMPARE(abRecords.value(key).toObject(), expected);

    QJsonObject replayRecords;
    QJsonObject replayTombstones;
    QVERIFY2(Store::mergeCanonicalState(
                 abRecords, abTombstones, abRecords, abTombstones,
                 &replayRecords, &replayTombstones, &error), qPrintable(error));
    QCOMPARE(replayRecords, abRecords);
    QCOMPARE(replayTombstones, abTombstones);

    const QJsonObject equalTombstones{{key, tombstoneJson(500)}};
    QVERIFY2(Store::mergeCanonicalState(
                 betaRecords, {}, {}, equalTombstones,
                 &replayRecords, &replayTombstones, &error), qPrintable(error));
    QVERIFY(replayRecords.isEmpty());
    QCOMPARE(replayTombstones, equalTombstones);
    const QJsonObject newerLive = recordJson(
        id, 9.0, QStringLiteral("newer"), false, 30, 600);
    QVERIFY2(Store::mergeCanonicalState(
                 QJsonObject{{key, newerLive}}, {}, {}, equalTombstones,
                 &replayRecords, &replayTombstones, &error), qPrintable(error));
    QCOMPARE(replayRecords.value(key).toObject(), newerLive);
    QVERIFY(replayTombstones.isEmpty());

    QVERIFY2(Store::mergeCanonicalState(
                 {}, {}, alphaRecords, {},
                 &replayRecords, &replayTombstones, &error), qPrintable(error));
    QCOMPARE(replayRecords, alphaRecords);
    QVERIFY(replayTombstones.isEmpty());
}
void tst_ratings_reviews_store::malformed_state_fails_closed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Identity id = identity(QStringLiteral("bad"));
    const QString key = Store::recordKeyForIdentity(id);
    const QJsonObject validRecord =
        recordJson(id, 8.0, QStringLiteral("ok"), false, 10, 20);

    int caseNumber = 0;
    const auto expectUnhealthy = [&](const QJsonObject &object) {
        const QString path =
            dir.filePath(QStringLiteral("bad-%1.json").arg(++caseNumber));
        QVERIFY(writeObject(path, object));
        Store store(path, clockFrom(1000));
        QString error;
        QVERIFY(!store.healthy(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.revision(), quint64(0));
        QVERIFY(store.recordsJson().isEmpty());
        QVERIFY(store.tombstonesJson().isEmpty());
    };

    QJsonObject wrongVersion = storeJson(0, {}, {});
    wrongVersion.insert(QStringLiteral("version"), 2);
    expectUnhealthy(wrongVersion);
    QJsonObject fractionalVersion = storeJson(0, {}, {});
    fractionalVersion.insert(QStringLiteral("version"), 1.5);
    expectUnhealthy(fractionalVersion);
    QJsonObject missingField = storeJson(0, {}, {});
    missingField.remove(QStringLiteral("tombstones"));
    expectUnhealthy(missingField);

    QJsonObject extraField = storeJson(0, {}, {});
    extraField.insert(QStringLiteral("future"), true);
    expectUnhealthy(extraField);

    const QString wrongKey =
        QStringLiteral("rr1:0000000000000000000000000000000000000000000000000000000000000000");
    expectUnhealthy(storeJson(1, QJsonObject{{wrongKey, validRecord}}, {}));

    QJsonObject badRating = validRecord;
    badRating.insert(QStringLiteral("rating"), 0.25);
    expectUnhealthy(storeJson(1, QJsonObject{{key, badRating}}, {}));

    QJsonObject bothNull = validRecord;
    bothNull.insert(QStringLiteral("rating"), QJsonValue(QJsonValue::Null));
    bothNull.insert(QStringLiteral("review"), QJsonValue(QJsonValue::Null));
    bothNull.insert(QStringLiteral("spoiler"), false);
    expectUnhealthy(storeJson(1, QJsonObject{{key, bothNull}}, {}));
    QJsonObject spoilerWithoutReview = validRecord;
    spoilerWithoutReview.insert(QStringLiteral("review"), QJsonValue(QJsonValue::Null));
    spoilerWithoutReview.insert(QStringLiteral("spoiler"), true);
    expectUnhealthy(storeJson(1, QJsonObject{{key, spoilerWithoutReview}}, {}));

    QJsonObject oversized = validRecord;
    oversized.insert(QStringLiteral("review"), QString(16385, QLatin1Char('x')));
    expectUnhealthy(storeJson(1, QJsonObject{{key, oversized}}, {}));

    QJsonObject badTimestamp = validRecord;
    badTimestamp.insert(QStringLiteral("created_at_ms"), 0);
    expectUnhealthy(storeJson(1, QJsonObject{{key, badTimestamp}}, {}));

    expectUnhealthy(storeJson(
        1, QJsonObject{{key, validRecord}},
        QJsonObject{{key, tombstoneJson(30)}}));

    const QString malformedPath = dir.filePath(QStringLiteral("malformed.json"));
    QFile malformed(malformedPath);
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write("{not-json"), qint64(9));
    malformed.close();
    Store malformedStore(malformedPath, clockFrom(1000));
    QString malformedError;
    QVERIFY(!malformedStore.healthy(&malformedError));
    QVERIFY(!malformedError.isEmpty());

    const QString directoryPath =
        dir.filePath(QStringLiteral("ratings-reviews-as-directory.json"));
    QVERIFY(QDir().mkpath(directoryPath));
    Store directoryStore(directoryPath, clockFrom(1000));
    QString directoryError;
    QVERIFY(!directoryStore.healthy(&directoryError));
    QVERIFY(!directoryError.isEmpty());

    Store zeroClock(
        dir.filePath(QStringLiteral("zero-clock.json")),
        [] { return qint64(0); });
    QVERIFY(zeroClock.healthy());
    QString clockError;
    QVERIFY(!zeroClock.setRating(id, 5.0, nullptr, &clockError));
    QVERIFY(!clockError.isEmpty());
    QCOMPARE(zeroClock.revision(), quint64(0));
    QVERIFY(!zeroClock.record(id).has_value());

    Store healthy(dir.filePath(QStringLiteral("healthy.json")), clockFrom(2000));
    QVERIFY(healthy.healthy());
    QVERIFY(healthy.setRating(id, 4.0));
    const quint64 before = healthy.revision();
    QString restoreError;
    QVERIFY(!healthy.restoreCanonicalState(
        QJsonObject{{key, badRating}}, {}, nullptr, &restoreError));
    QVERIFY(!restoreError.isEmpty());
    QCOMPARE(healthy.revision(), before);
    QCOMPARE(*healthy.record(id)->rating, 4.0);
}
void tst_ratings_reviews_store::persistence_failure_is_atomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString blockerPath = dir.filePath(QStringLiteral("blocker"));
    QFile blocker(blockerPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    const QByteArray sentinel("do-not-touch");
    QCOMPARE(blocker.write(sentinel), sentinel.size());
    blocker.close();

    const QString path = blockerPath + QStringLiteral("/ratings-reviews.json");
    Store store(path, clockFrom(1000));
    QVERIFY(store.healthy());
    QSignalSpy changed(&store, &Store::changed);
    QSignalSpy dirty(&store, &Store::syncDirty);

    QString error;
    QVERIFY(!store.setRating(identity(), 5.0, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.revision(), quint64(0));
    QVERIFY(!store.record(identity()).has_value());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(dirty.count(), 0);
    QFile verify(blockerPath);
    QVERIFY(verify.open(QIODevice::ReadOnly));
    QCOMPARE(verify.readAll(), sentinel);
    QVERIFY(!QFileInfo::exists(path));
    QString healthError;
    QVERIFY(!store.healthy(&healthError));
    QVERIFY(!healthError.isEmpty());
}
void tst_ratings_reviews_store::restart_persists_exact_state_and_revision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ratings-reviews.json"));
    const Identity liveId = identity(QStringLiteral("live"));
    const Identity deletedId = identity(QStringLiteral("deleted"));

    quint64 committedRevision = 0;
    QJsonObject committedRecords;
    QJsonObject committedTombstones;
    {
        Store store(path, clockFrom(1000, 100));
        QString error;
        QVERIFY2(store.saveLocal(liveId, std::optional<double>(0.0),
                                 std::optional<QString>(QString()),
                                 false, nullptr, &error), qPrintable(error));
        QVERIFY2(store.setReview(deletedId, QStringLiteral("remove me"),
                                 false, nullptr, &error), qPrintable(error));
        QVERIFY2(store.deleteReview(deletedId, nullptr, &error), qPrintable(error));
        committedRevision = store.revision();
        committedRecords = store.recordsJson();
        committedTombstones = store.tombstonesJson();
        QVERIFY2(store.flush(&error), qPrintable(error));
    }

    Store reopened(path, clockFrom(9000));
    QString error;
    QVERIFY2(reopened.healthy(&error), qPrintable(error));
    QCOMPARE(reopened.revision(), committedRevision);
    QCOMPARE(reopened.recordsJson(), committedRecords);
    QCOMPARE(reopened.tombstonesJson(), committedTombstones);
    const auto live = reopened.record(liveId);
    QVERIFY(live.has_value());
    QVERIFY(live->rating.has_value());
    QCOMPARE(*live->rating, 0.0);
    QVERIFY(live->review.has_value());
    QVERIFY(live->review->isEmpty());

    QSignalSpy dirty(&reopened, &Store::syncDirty);
    const quint64 beforeClear = reopened.revision();
    QVERIFY2(reopened.clearForProfileRetirement(&error), qPrintable(error));
    QCOMPARE(reopened.revision(), beforeClear + 1);
    QVERIFY(reopened.recordsJson().isEmpty());
    QVERIFY(reopened.tombstonesJson().isEmpty());
    QCOMPARE(dirty.count(), 0);

    Store cleared(path, clockFrom(12000));
    QVERIFY2(cleared.healthy(&error), qPrintable(error));
    QCOMPARE(cleared.revision(), beforeClear + 1);
    QVERIFY(cleared.recordsJson().isEmpty());
    QVERIFY(cleared.tombstonesJson().isEmpty());
}

QTEST_GUILESS_MAIN(tst_ratings_reviews_store)
#include "tst_ratings_reviews_store.moc"
