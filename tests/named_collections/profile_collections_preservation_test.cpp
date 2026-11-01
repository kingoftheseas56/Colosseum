#include "account/ProfileAdoption.h"
#include "account/ProfilePaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdlib>
#include <iostream>

namespace {
int assertions = 0;

void require(bool condition, const char *message)
{
    ++assertions;
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "open preserved file");
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open named Collections fixture");
    require(file.write(bytes) == bytes.size(), "write complete named Collections fixture");
}

ProfilePaths account(const QString &root)
{
    const auto paths = ProfilePaths::account(QUuid::createUuid().toString(QUuid::WithoutBraces), root);
    require(paths.has_value(), "valid isolated account paths");
    return *paths;
}

ProfileAdoption begin(const ProfilePaths &paths, const QString &digest)
{
    QString error;
    auto adoption = ProfileAdoption::begin(paths, digest, ProfilePaths::Kind::LocalOnly, &error);
    require(adoption.has_value(), "begin isolated account adoption");
    return *adoption;
}

void stagingRollbackPreservesUnknownFile(const QString &root, const QString &digest)
{
    const ProfilePaths paths = account(root);
    auto adoption = begin(paths, digest);
    const QString namedPath = QDir(paths.accountStagingRoot()).filePath(
        QFileInfo(paths.namedCollectionsPath()).fileName());
    // A rollback must preserve unrecognized data without assuming the current
    // parser can read it or that an empty/malformed file is safe to destroy.
    const QByteArray original("{\"schemaVersion\":99,\"future\":\"private data\"}");
    write(namedPath, original);
    const QByteArray journal = read(paths.adoptionJournalPath());
    QString error;
    require(!adoption.rollbackBeforeLegacyQuarantine(&error), "staging rollback refuses Collections deletion");
    require(error.contains(QStringLiteral("Collections")) && error.contains(namedPath), "staging failure identifies Collections path");
    require(read(namedPath) == original, "unknown-schema staging file preserved exactly");
    require(read(paths.adoptionJournalPath()) == journal, "blocked staging rollback preserves journal bytes");
    require(adoption.state() == ProfileAdoption::State::Preparing, "blocked staging rollback preserves state");
    auto reopened = ProfileAdoption::open(paths, &error);
    require(reopened.has_value() && reopened->state() == ProfileAdoption::State::Preparing, "blocked adoption remains recoverable after restart");
}

void promotedRollbackPreservesMalformedFile(const QString &root, const QString &digest)
{
    const ProfilePaths paths = account(root);
    auto adoption = begin(paths, digest);
    QString error;
    require(adoption.markTargetVerified(digest, &error), "verify staged target");
    require(adoption.promote(&error), "promote staged target");
    const QByteArray original("malformed-but-user-owned\0bytes", 30);
    write(paths.namedCollectionsPath(), original);
    const QByteArray promotedJournal = read(paths.adoptionJournalPath());
    require(!adoption.rollbackBeforeLegacyQuarantine(&error), "promoted rollback refuses Collections deletion");
    require(error.contains(QStringLiteral("Collections")) && error.contains(paths.namedCollectionsPath()), "promoted failure identifies Collections path");
    require(read(paths.namedCollectionsPath()) == original, "malformed promoted data preserved exactly");
    require(read(paths.adoptionJournalPath()) == promotedJournal, "blocked promoted rollback preserves journal");
    require(adoption.state() == ProfileAdoption::State::Promoted, "blocked promoted rollback preserves state");

    require(QDir().mkpath(paths.adoptionBackupRoot()), "create verified source backup directory");
    require(adoption.markLegacyQuarantined(digest, &error), "record source quarantine");
    const QByteArray quarantineJournal = read(paths.adoptionJournalPath());
    require(!adoption.rollbackAfterLegacyRestore(digest, &error), "post-restore rollback refuses Collections deletion");
    require(error.contains(QStringLiteral("Collections")) && error.contains(paths.namedCollectionsPath()), "post-restore failure identifies Collections path");
    require(read(paths.namedCollectionsPath()) == original, "post-restore rollback preserves all named bytes");
    require(read(paths.adoptionJournalPath()) == quarantineJournal, "blocked post-restore rollback preserves journal");
    require(adoption.state() == ProfileAdoption::State::LegacyQuarantined, "blocked post-restore rollback retains recovery state");
    require(QDir(paths.adoptionBackupRoot()).exists(), "blocked rollback preserves verified source backup");
    auto reopened = ProfileAdoption::open(paths, &error);
    require(reopened.has_value() && reopened->state() == ProfileAdoption::State::LegacyQuarantined, "post-restore refusal remains recoverable after restart");
}

void emptyNamedFileIsOwned(const QString &root, const QString &digest)
{
    const ProfilePaths paths = account(root);
    auto adoption = begin(paths, digest);
    QString error;
    require(adoption.markTargetVerified(digest, &error) && adoption.promote(&error), "promote target for empty-file protection");
    write(paths.namedCollectionsPath(), QByteArray());
    require(!adoption.rollbackBeforeLegacyQuarantine(&error), "empty named file still prevents destructive rollback");
    require(QFileInfo::exists(paths.namedCollectionsPath()) && read(paths.namedCollectionsPath()).isEmpty(), "empty named file preserved");
}

void verifiedCopyRollbackAndChangedCopyProtection(const QString &root, const QString &digest)
{
    const QByteArray original("future-but-captured\0bytes", 25);
    const QString sourceDigest = QString::fromLatin1(
        QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex());
    QString error;
    const auto firstPaths = account(root);
    auto first = ProfileAdoption::begin(firstPaths, digest, ProfilePaths::Kind::LocalOnly, &error, sourceDigest);
    require(first.has_value(), "begin adoption with captured named-state digest");
    const QString firstCopy = QDir(firstPaths.accountStagingRoot()).filePath(QStringLiteral("named-collections-v2.json"));
    write(firstCopy, original);
    auto reopened = ProfileAdoption::open(firstPaths, &error);
    require(reopened.has_value() && reopened->snapshot().namedCollectionsSourceDigest == sourceDigest,
        "captured private-state digest survives journal restart");
    require(reopened->rollbackBeforeLegacyQuarantine(&error), "exact captured staging copy may roll back safely");
    require(!QFileInfo::exists(firstPaths.accountStagingRoot()), "verified staging copy rollback removes only managed copy");

    const auto changedPaths = account(root);
    auto changed = ProfileAdoption::begin(changedPaths, digest, ProfilePaths::Kind::LocalOnly, &error, sourceDigest);
    require(changed.has_value(), "begin changed-copy protection fixture");
    require(changed->markTargetVerified(digest, &error) && changed->promote(&error), "promote changed-copy fixture");
    write(changedPaths.namedCollectionsPath(), original + QByteArrayLiteral("new user edits"));
    const QByteArray before = read(changedPaths.namedCollectionsPath());
    require(!changed->rollbackBeforeLegacyQuarantine(&error), "changed promoted collection bytes block rollback");
    require(read(changedPaths.namedCollectionsPath()) == before, "changed private bytes remain exact");

    const auto restoredPaths = account(root);
    auto restored = ProfileAdoption::begin(restoredPaths, digest, ProfilePaths::Kind::LocalOnly, &error, sourceDigest);
    require(restored.has_value(), "begin restored-copy fixture");
    require(restored->markTargetVerified(digest, &error) && restored->promote(&error), "promote restored-copy fixture");
    write(restoredPaths.namedCollectionsPath(), original);
    require(QDir().mkpath(restoredPaths.adoptionBackupRoot()), "prepare verified source backup fixture");
    require(restored->markLegacyQuarantined(digest, &error), "record restored-copy backup");
    require(restored->rollbackAfterLegacyRestore(digest, &error), "restored verified source permits copy rollback");
    require(!QFileInfo::exists(restoredPaths.profileRoot()), "rollback removes exact verified managed copy");
}

void existingRollbackStillWorksWithoutNamedData(const QString &root, const QString &digest)
{
    const ProfilePaths beforePaths = account(root);
    auto before = begin(beforePaths, digest);
    QString error;
    require(before.markTargetVerified(digest, &error) && before.promote(&error), "promote ordinary target");
    require(before.rollbackBeforeLegacyQuarantine(&error), "ordinary pre-quarantine rollback succeeds");
    require(!QFileInfo::exists(beforePaths.profileRoot()) && !QFileInfo::exists(beforePaths.adoptionJournalPath()), "ordinary rollback retains existing cleanup behavior");

    const ProfilePaths afterPaths = account(root);
    auto after = begin(afterPaths, digest);
    require(after.markTargetVerified(digest, &error) && after.promote(&error), "promote ordinary post-quarantine target");
    require(QDir().mkpath(afterPaths.adoptionBackupRoot()), "create ordinary verified backup");
    require(after.markLegacyQuarantined(digest, &error), "quarantine ordinary source");
    require(after.rollbackAfterLegacyRestore(digest, &error), "ordinary post-restore rollback succeeds");
    require(!QFileInfo::exists(afterPaths.profileRoot()), "ordinary post-restore rollback removes promoted tree");
    require(after.state() == ProfileAdoption::State::RetryPending, "ordinary post-restore rollback becomes retryable");
    require(QFileInfo::exists(afterPaths.adoptionJournalPath()) && QDir(afterPaths.adoptionBackupRoot()).exists(), "ordinary post-restore rollback retains recovery journal and backup");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "temporary preservation-test root");
    const QString digest(64, QLatin1Char('a'));
    stagingRollbackPreservesUnknownFile(root.path(), digest);
    promotedRollbackPreservesMalformedFile(root.path(), digest);
    emptyNamedFileIsOwned(root.path(), digest);
    existingRollbackStillWorksWithoutNamedData(root.path(), digest);
    verifiedCopyRollbackAndChangedCopyProtection(root.path(), digest);
    std::cout << "PROFILE_COLLECTIONS_PRESERVATION_OK " << assertions << '\n';
    return 0;
}
