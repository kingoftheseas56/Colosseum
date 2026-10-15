// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.
//
// Arc 36 Wave 4A lane N-14: the crash-safe cloud attachment receipt. This is the
// durable local record that lets an existing-account cloud attachment survive
// crashes, restarts, and response loss without losing the identity of the local
// source that still must be retired. Persistence only — no network attachment,
// no source retirement, no runtime behavior.

#include "account/AccountAttachmentReceipt.h"
#include "account/ProfilePaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {
constexpr auto kAccountId = "4c648ba1-cd40-4b47-b1ac-90f07be8e289";
constexpr auto kOtherAccountId = "b3d5da12-a828-4679-aa1d-b93b12bf0840";
constexpr auto kAttachmentId = "9f2c6a4e-3d1b-4f8a-9c25-7b0e6d2a1f34";

AccountAttachmentReceiptData validReceipt() {
    AccountAttachmentReceiptData data;
    data.version = 1;
    data.attachmentId = QString::fromLatin1(kAttachmentId);
    data.sourceKind = QStringLiteral("legacy_local");
    data.sourceProfileId = QStringLiteral("legacy");
    data.sourceSemanticDigest = QStringLiteral("sha256:source-semantic-v1");
    data.sourceActivityDigest = QStringLiteral("sha256:source-activity-v1");
    data.sourceRetired = false;
    return data;
}

bool sameReceipt(const AccountAttachmentReceiptData &left,
                 const AccountAttachmentReceiptData &right) {
    return left.version == right.version
        && left.attachmentId == right.attachmentId
        && left.sourceKind == right.sourceKind
        && left.sourceProfileId == right.sourceProfileId
        && left.sourceSemanticDigest == right.sourceSemanticDigest
        && left.sourceActivityDigest == right.sourceActivityDigest
        && left.sourceRetired == right.sourceRetired;
}

QByteArray receiptBytes(const AccountAttachmentReceiptData &data) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), data.version);
    object.insert(QStringLiteral("attachment_id"), data.attachmentId);
    object.insert(QStringLiteral("source_kind"), data.sourceKind);
    object.insert(QStringLiteral("source_profile_id"), data.sourceProfileId);
    object.insert(QStringLiteral("source_semantic_digest"), data.sourceSemanticDigest);
    object.insert(QStringLiteral("source_activity_digest"), data.sourceActivityDigest);
    object.insert(QStringLiteral("source_retired"), data.sourceRetired);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray receiptBytesWithOverride(const AccountAttachmentReceiptData &data,
                                    const QString &key,
                                    const QJsonValue &value) {
    QJsonDocument document = QJsonDocument::fromJson(receiptBytes(data));
    QJsonObject object = document.object();
    object.insert(key, value);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray receiptBytesWithoutKey(const AccountAttachmentReceiptData &data,
                                  const QString &key) {
    QJsonDocument document = QJsonDocument::fromJson(receiptBytes(data));
    QJsonObject object = document.object();
    object.remove(key);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool writeFileBytes(const QString &path, const QByteArray &payload) {
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(payload) != payload.size())
        return false;
    file.close();
    return true;
}

QByteArray readFileBytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}
}

class tst_account_attachment : public QObject {
    Q_OBJECT

private slots:
    void receiptPathIsAccountOnlyAndInsidePromotedProfile();
    void roundTripWriteReadPreservesEveryField();
    void emptyActivityDigestIsTheNoActivitySentinel();
    void rewriteOfSameReceiptIsIdempotent();
    void failedCommitPreservesPreviouslyValidReceipt();
    void refusedInvalidSavePreservesExistingReceipt();
    void readRejectsInvalidPayloads_data();
    void readRejectsInvalidPayloads();
    void missingReceiptIsAbsenceNotFailure();
    void markRetiredPreservesIdentityAndDigestsAcrossRestart();
    void clearIsExplicitAndIdempotent();
    void nonAccountProfilesFailClosed();
};

// Contract 1: the receipt lives inside the promoted account profile only —
// never in the global adoption/attachment journal directories, never on a
// local or sealed profile.
void tst_account_attachment::receiptPathIsAccountOnlyAndInsidePromotedProfile() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const QString receiptPath = paths->cloudAttachmentReceiptPath();
    QVERIFY(!receiptPath.isEmpty());
    QVERIFY(receiptPath.startsWith(paths->profileRoot() + QLatin1Char('/')));
    QVERIFY(!receiptPath.contains(QStringLiteral("/profile-adoption/")));
    QVERIFY(!receiptPath.contains(QStringLiteral("/profile-attachment/")));
    QVERIFY(paths->isManagedProfilePath(receiptPath));

    const auto other =
        ProfilePaths::account(QString::fromLatin1(kOtherAccountId), temp.path());
    QVERIFY(other.has_value());
    QVERIFY(other->cloudAttachmentReceiptPath() != receiptPath);

    QCOMPARE(ProfilePaths::localOnly(temp.path()).cloudAttachmentReceiptPath(), QString());
    QCOMPARE(ProfilePaths::sealed(temp.path()).cloudAttachmentReceiptPath(), QString());
    QCOMPARE(ProfilePaths::legacyLocal().cloudAttachmentReceiptPath(), QString());
}

// Contract 2/3: QSaveFile round-trip with parent-directory creation.
void tst_account_attachment::roundTripWriteReadPreservesEveryField() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceiptData original = validReceipt();
    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, original, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(paths->cloudAttachmentReceiptPath()));

    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(result.error.isEmpty());
    QVERIFY(sameReceipt(result.data, original));
}

// Contract 4: empty Activity digest is the valid "source had no durable
// Activity ledger" sentinel, not a missing-field failure.
void tst_account_attachment::emptyActivityDigestIsTheNoActivitySentinel() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    AccountAttachmentReceiptData data = validReceipt();
    data.sourceKind = QStringLiteral("local_only");
    data.sourceProfileId = QStringLiteral("local");
    data.sourceActivityDigest = QString();

    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, data, &error), qPrintable(error));

    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(result.data.sourceActivityDigest.isEmpty());
    QVERIFY(sameReceipt(result.data, data));
}

// Contract 6: rewriting the same semantic receipt is idempotent — the file
// bytes do not change.
void tst_account_attachment::rewriteOfSameReceiptIsIdempotent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceiptData data = validReceipt();
    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, data, &error), qPrintable(error));
    const QByteArray firstBytes = readFileBytes(paths->cloudAttachmentReceiptPath());
    QVERIFY(!firstBytes.isEmpty());

    QVERIFY2(AccountAttachmentReceipt::save(*paths, data, &error), qPrintable(error));
    QCOMPARE(readFileBytes(paths->cloudAttachmentReceiptPath()), firstBytes);

    // A flipped retirement flag is a different semantic receipt, but writing
    // that same new semantic receipt twice is again idempotent.
    AccountAttachmentReceiptData retired = data;
    retired.sourceRetired = true;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, retired, &error), qPrintable(error));
    const QByteArray retiredBytes = readFileBytes(paths->cloudAttachmentReceiptPath());
    QVERIFY(retiredBytes != firstBytes);
    QVERIFY2(AccountAttachmentReceipt::save(*paths, retired, &error), qPrintable(error));
    QCOMPARE(readFileBytes(paths->cloudAttachmentReceiptPath()), retiredBytes);
}

// Contract 2: atomic replacement — when a write cannot commit, the previously
// valid receipt survives untouched.
void tst_account_attachment::failedCommitPreservesPreviouslyValidReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceiptData original = validReceipt();
    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, original, &error), qPrintable(error));
    const QByteArray originalBytes = readFileBytes(paths->cloudAttachmentReceiptPath());
    QVERIFY(!originalBytes.isEmpty());

    AccountAttachmentReceiptData updated = original;
    updated.sourceRetired = true;

#ifdef Q_OS_WIN
    // A read handle without FILE_SHARE_DELETE blocks the QSaveFile replace.
    QFile lock(paths->cloudAttachmentReceiptPath());
    QVERIFY(lock.open(QIODevice::ReadOnly));
    QVERIFY(!AccountAttachmentReceipt::save(*paths, updated, &error));
    QVERIFY(!error.isEmpty());
    lock.close();
#else
    // POSIX: strip write permission from the parent directory so the atomic
    // replace cannot land.
    const QString parent =
        QFileInfo(paths->cloudAttachmentReceiptPath()).absolutePath();
    const QFile::Permissions restore = QFileInfo(parent).permissions();
    QVERIFY(QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QVERIFY(!AccountAttachmentReceipt::save(*paths, updated, &error));
    QVERIFY(!error.isEmpty());
    QFile::setPermissions(parent, restore);
#endif

    QCOMPARE(readFileBytes(paths->cloudAttachmentReceiptPath()), originalBytes);
    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(sameReceipt(result.data, original));
}

// Contract 5/8: an invalid receipt is refused at the save seam and never
// silently coerced; the previously stored valid receipt stays intact.
void tst_account_attachment::refusedInvalidSavePreservesExistingReceipt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceiptData original = validReceipt();
    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, original, &error), qPrintable(error));
    const QByteArray originalBytes = readFileBytes(paths->cloudAttachmentReceiptPath());

    AccountAttachmentReceiptData bad;

    bad = original;
    bad.version = 2;
    QVERIFY(!AccountAttachmentReceipt::save(*paths, bad, &error));

    bad = original;
    bad.attachmentId = QStringLiteral("9F2C6A4E-3D1B-4F8A-9C25-7B0E6D2A1F34");
    QVERIFY(!AccountAttachmentReceipt::save(*paths, bad, &error));

    bad = original;
    bad.sourceKind = QStringLiteral("cloud");
    QVERIFY(!AccountAttachmentReceipt::save(*paths, bad, &error));

    bad = original;
    bad.sourceProfileId = QString();
    QVERIFY(!AccountAttachmentReceipt::save(*paths, bad, &error));

    bad = original;
    bad.sourceSemanticDigest = QString();
    QVERIFY(!AccountAttachmentReceipt::save(*paths, bad, &error));

    QCOMPARE(readFileBytes(paths->cloudAttachmentReceiptPath()), originalBytes);
    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(sameReceipt(result.data, original));
}

// Contract 5/8: reads fail closed on malformed JSON, unknown version, invalid
// identity/source kind, missing fields, and type mismatches — never coerced
// into absence (Invalid is distinct from Missing).
void tst_account_attachment::readRejectsInvalidPayloads_data() {
    QTest::addColumn<QByteArray>("payload");

    const AccountAttachmentReceiptData data = validReceipt();

    QTest::newRow("malformed-json") << QByteArrayLiteral("{ not json");
    QTest::newRow("not-an-object")
        << QJsonDocument(QJsonArray{1, 2, 3}).toJson(QJsonDocument::Compact);

    QTest::newRow("version-2")
        << receiptBytesWithOverride(data, QStringLiteral("version"), 2);
    QTest::newRow("version-string")
        << receiptBytesWithOverride(data, QStringLiteral("version"), QStringLiteral("1"));
    QTest::newRow("version-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("version"));

    QTest::newRow("attachment-id-uppercase")
        << receiptBytesWithOverride(
               data, QStringLiteral("attachment_id"),
               QStringLiteral("9F2C6A4E-3D1B-4F8A-9C25-7B0E6D2A1F34"));
    QTest::newRow("attachment-id-braces")
        << receiptBytesWithOverride(
               data, QStringLiteral("attachment_id"),
               QStringLiteral("{9f2c6a4e-3d1b-4f8a-9c25-7b0e6d2a1f34}"));
    QTest::newRow("attachment-id-nil")
        << receiptBytesWithOverride(
               data, QStringLiteral("attachment_id"),
               QStringLiteral("00000000-0000-0000-0000-000000000000"));
    QTest::newRow("attachment-id-garbage")
        << receiptBytesWithOverride(
               data, QStringLiteral("attachment_id"), QStringLiteral("not-a-uuid"));
    QTest::newRow("attachment-id-number")
        << receiptBytesWithOverride(data, QStringLiteral("attachment_id"), 17);
    QTest::newRow("attachment-id-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("attachment_id"));

    QTest::newRow("source-kind-unknown")
        << receiptBytesWithOverride(data, QStringLiteral("source_kind"),
                                    QStringLiteral("cloud"));
    QTest::newRow("source-kind-number")
        << receiptBytesWithOverride(data, QStringLiteral("source_kind"), 3);
    QTest::newRow("source-kind-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("source_kind"));

    QTest::newRow("source-profile-id-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("source_profile_id"));
    QTest::newRow("source-profile-id-empty")
        << receiptBytesWithOverride(data, QStringLiteral("source_profile_id"), QString());
    QTest::newRow("source-profile-id-number")
        << receiptBytesWithOverride(data, QStringLiteral("source_profile_id"), 5);

    QTest::newRow("source-semantic-digest-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("source_semantic_digest"));
    QTest::newRow("source-semantic-digest-empty")
        << receiptBytesWithOverride(data, QStringLiteral("source_semantic_digest"), QString());

    // Empty Activity digest is valid, but a missing or mistyped key is not.
    QTest::newRow("source-activity-digest-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("source_activity_digest"));
    QTest::newRow("source-activity-digest-number")
        << receiptBytesWithOverride(data, QStringLiteral("source_activity_digest"), 9);

    QTest::newRow("source-retired-missing")
        << receiptBytesWithoutKey(data, QStringLiteral("source_retired"));
    QTest::newRow("source-retired-string")
        << receiptBytesWithOverride(data, QStringLiteral("source_retired"),
                                    QStringLiteral("yes"));
}

void tst_account_attachment::readRejectsInvalidPayloads() {
    QFETCH(QByteArray, payload);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());
    QVERIFY(writeFileBytes(paths->cloudAttachmentReceiptPath(), payload));

    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Invalid);
    QVERIFY(!result.error.isEmpty());
}

// Contract 7/8: absence means no cloud attachment is pending — Missing is not
// an error, and no file is invented.
void tst_account_attachment::missingReceiptIsAbsenceNotFailure() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceipt::ReadResult result = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(result.status, AccountAttachmentReceipt::ReadStatus::Missing);
    QVERIFY(result.error.isEmpty());
    QVERIFY(!QFileInfo::exists(paths->cloudAttachmentReceiptPath()));
}

// Contract 6: updating only sourceRetired preserves every source identity and
// digest, and the update survives a restart round-trip.
void tst_account_attachment::markRetiredPreservesIdentityAndDigestsAcrossRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    const AccountAttachmentReceiptData original = validReceipt();
    QString error;
    QVERIFY2(AccountAttachmentReceipt::save(*paths, original, &error), qPrintable(error));

    QVERIFY2(AccountAttachmentReceipt::markSourceRetired(*paths, &error), qPrintable(error));

    // "Restart": every read re-reads the durable file; nothing is cached.
    const AccountAttachmentReceipt::ReadResult restarted = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(restarted.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(restarted.data.sourceRetired);
    QCOMPARE(restarted.data.version, original.version);
    QCOMPARE(restarted.data.attachmentId, original.attachmentId);
    QCOMPARE(restarted.data.sourceKind, original.sourceKind);
    QCOMPARE(restarted.data.sourceProfileId, original.sourceProfileId);
    QCOMPARE(restarted.data.sourceSemanticDigest, original.sourceSemanticDigest);
    QCOMPARE(restarted.data.sourceActivityDigest, original.sourceActivityDigest);

    // Marking retired again is an idempotent semantic rewrite.
    QVERIFY2(AccountAttachmentReceipt::markSourceRetired(*paths, &error), qPrintable(error));
    const AccountAttachmentReceipt::ReadResult again = AccountAttachmentReceipt::read(*paths);
    QCOMPARE(again.status, AccountAttachmentReceipt::ReadStatus::Ok);
    QVERIFY(sameReceipt(again.data, restarted.data));
}

// Contract 7: clear is explicit and idempotent; after a clear the profile has
// no pending cloud attachment.
void tst_account_attachment::clearIsExplicitAndIdempotent() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    // Clearing an absent receipt is a no-op success.
    QString error;
    QVERIFY2(AccountAttachmentReceipt::clear(*paths, &error), qPrintable(error));

    QVERIFY2(AccountAttachmentReceipt::save(*paths, validReceipt(), &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(paths->cloudAttachmentReceiptPath()));

    QVERIFY2(AccountAttachmentReceipt::clear(*paths, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(paths->cloudAttachmentReceiptPath()));

    QVERIFY2(AccountAttachmentReceipt::clear(*paths, &error), qPrintable(error));
    QCOMPARE(AccountAttachmentReceipt::read(*paths).status,
             AccountAttachmentReceipt::ReadStatus::Missing);

    // Clearing does not touch a retired source's other profile state.
    QVERIFY(QFileInfo::exists(paths->profileRoot()));
}

// Contract 8 mirrored at the path seam: profiles that can never own a receipt
// fail closed on every operation instead of pretending absence.
void tst_account_attachment::nonAccountProfilesFailClosed() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const ProfilePaths local = ProfilePaths::localOnly(temp.path());
    const ProfilePaths sealed = ProfilePaths::sealed(temp.path());
    const ProfilePaths legacy = ProfilePaths::legacyLocal();

    const QList<ProfilePaths> nonAccount{local, sealed, legacy};
    for (const ProfilePaths &paths : nonAccount) {
        QString error;
        QVERIFY(!AccountAttachmentReceipt::save(paths, validReceipt(), &error));
        QVERIFY(!error.isEmpty());

        QCOMPARE(AccountAttachmentReceipt::read(paths).status,
                 AccountAttachmentReceipt::ReadStatus::Invalid);
        QVERIFY(!AccountAttachmentReceipt::read(paths).error.isEmpty());

        QVERIFY(!AccountAttachmentReceipt::markSourceRetired(paths, &error));
        QVERIFY(!AccountAttachmentReceipt::clear(paths, &error));
    }
}

QTEST_GUILESS_MAIN(tst_account_attachment)

#include "tst_account_attachment.moc"
