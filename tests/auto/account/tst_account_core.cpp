// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/ProfileAdoption.h"
#include "account/ProfileContext.h"
#include "account/ProfilePaths.h"
#include "CollectionStore.h"
#include "ProgressStore.h"
#include "SearchHistoryStore.h"
#include "AccountFixtureTransport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {
constexpr auto kAccountId = "4c648ba1-cd40-4b47-b1ac-90f07be8e289";

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char *name)
        : m_name(name),
          m_wasSet(qEnvironmentVariableIsSet(name)),
          m_previous(qgetenv(name)) {}

    ~ScopedEnvironmentVariable() {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

AccountTransportReply okReply(const QString &value) {
    AccountTransportReply reply;
    reply.statusCode = 200;
    reply.body.insert(QStringLiteral("value"), value);
    return reply;
}
}

class tst_account_core : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void fixtureTransportRequiresTaggedSession();
    void fixtureTransportReturnsSeededReply();
    void fixtureTransportOfflineDoesNotConsumeReply();

    void profilePathsRejectUnsafeAccountId();
    void profilePathsSeparateAccountRoots();
    void sealedProfileDoesNotInventPersonalFilePaths();
    void legacyProfileDoesNotInventFilePaths();
    void profileContextTransitionsWithoutDuplicateRevision();
    void realStoresStayIsolatedAcrossAccountProfiles();

    void adoptionRequiresMatchingSemanticDigest();
    void adoptionPromotesVerifiedTarget();
    void adoptionRollsBackBeforeLegacyQuarantine();
    void adoptionReconcilesInterruptedPromotion();
    void adoptionCommitRequiresRollbackBackup();
};

void tst_account_core::initTestCase() {
    qRegisterMetaType<AccountTransportReply>();
}

void tst_account_core::fixtureTransportRequiresTaggedSession() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qunsetenv("COLOSSEUM_APPDATA_TAG");

    QVERIFY(!AccountFixtureTransport::testModeAllowed());
    QVERIFY(AccountFixtureTransport::create() == nullptr);

    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-core-test"));
    QVERIFY(AccountFixtureTransport::testModeAllowed());
    QVERIFY(AccountFixtureTransport::create() != nullptr);
}

void tst_account_core::fixtureTransportReturnsSeededReply() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);

    transport->enqueueReply(QByteArrayLiteral("GET"),
                            QStringLiteral("/v1/fixture"),
                            okReply(QStringLiteral("first")));

    QSignalSpy spy(transport.get(), &AccountTransport::finished);
    AccountTransportRequest request;
    request.method = QByteArrayLiteral("get");
    request.path = QStringLiteral("/v1/fixture");
    request.body.insert(QStringLiteral("password"), QStringLiteral("not-echoed"));
    request.bearerToken = QByteArrayLiteral("fixture-secret-token");

    transport->send(41, request);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64(41));

    const AccountTransportReply reply = arguments.at(1).value<AccountTransportReply>();
    QCOMPARE(reply.statusCode, 200);
    QCOMPARE(reply.body.value(QStringLiteral("value")).toString(), QStringLiteral("first"));
    QVERIFY(reply.errorCode.isEmpty());
    QVERIFY(reply.errorMessage.isEmpty());
    QCOMPARE(transport->pendingReplyCount(QByteArrayLiteral("GET"),
                                          QStringLiteral("/v1/fixture")),
             0);
}

void tst_account_core::fixtureTransportOfflineDoesNotConsumeReply() {
    ScopedEnvironmentVariable restore("COLOSSEUM_APPDATA_TAG");
    qputenv("COLOSSEUM_APPDATA_TAG", QByteArrayLiteral("account-core-test"));

    auto transport = AccountFixtureTransport::create();
    QVERIFY(transport);
    transport->enqueueReply(QByteArrayLiteral("POST"),
                            QStringLiteral("/v1/fixture"),
                            okReply(QStringLiteral("queued")));
    transport->setOnline(false);

    QSignalSpy spy(transport.get(), &AccountTransport::finished);

    AccountTransportRequest request;
    request.method = QByteArrayLiteral("POST");
    request.path = QStringLiteral("/v1/fixture");
    transport->send(7, request);

    QCOMPARE(spy.count(), 1);
    AccountTransportReply reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    QVERIFY(reply.networkError);
    QCOMPARE(reply.errorCode, QStringLiteral("offline"));
    QCOMPARE(transport->pendingReplyCount(QByteArrayLiteral("POST"),
                                          QStringLiteral("/v1/fixture")),
             1);

    transport->setOnline(true);
    transport->send(8, request);
    QCOMPARE(spy.count(), 1);
    reply = spy.takeFirst().at(1).value<AccountTransportReply>();
    QCOMPARE(reply.statusCode, 200);
    QCOMPARE(reply.body.value(QStringLiteral("value")).toString(), QStringLiteral("queued"));
}

void tst_account_core::profilePathsRejectUnsafeAccountId() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QVERIFY(!ProfilePaths::isValidAccountId(QStringLiteral("../profiles/other")));
    QVERIFY(!ProfilePaths::isValidAccountId(QStringLiteral("Hemanth56")));
    QVERIFY(!ProfilePaths::account(QStringLiteral("../profiles/other"), temp.path()).has_value());
    QVERIFY(!ProfilePaths::account(QStringLiteral("Hemanth56"), temp.path()).has_value());
}

void tst_account_core::profilePathsSeparateAccountRoots() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto first = ProfilePaths::account(
        QStringLiteral("4c648ba1-cd40-4b47-b1ac-90f07be8e289"),
        temp.path());
    const auto second = ProfilePaths::account(
        QStringLiteral("b3d5da12-a828-4679-aa1d-b93b12bf0840"),
        temp.path());

    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QVERIFY(first->profileRoot() != second->profileRoot());
    QVERIFY(first->progressIniPath() != second->progressIniPath());
    QVERIFY(first->searchHistoryIniPath() != second->searchHistoryIniPath());
    QVERIFY(first->isManagedProfilePath(first->profileRoot()));
    QVERIFY(!first->isManagedProfilePath(temp.path()));
}

void tst_account_core::sealedProfileDoesNotInventPersonalFilePaths() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const ProfilePaths sealed =
        ProfilePaths::sealed(
            temp.path());

    QCOMPARE(
        sealed.kind(),
        ProfilePaths::Kind::Sealed);
    QVERIFY(!sealed.usesLegacySettings());
    QVERIFY(sealed.profileRoot().isEmpty());
    QVERIFY(sealed.progressIniPath().isEmpty());
    QVERIFY(sealed.collectionIniPath().isEmpty());
    QVERIFY(sealed.searchHistoryIniPath().isEmpty());
    QVERIFY(sealed.syncStatePath().isEmpty());
    QVERIFY(sealed.syncOutboxPath().isEmpty());
    QVERIFY(
        !sealed.isManagedProfilePath(
            temp.path()));
}

void tst_account_core::legacyProfileDoesNotInventFilePaths() {
    const ProfilePaths legacy = ProfilePaths::legacyLocal();

    QVERIFY(legacy.usesLegacySettings());
    QVERIFY(legacy.profileRoot().isEmpty());
    QVERIFY(legacy.progressIniPath().isEmpty());
    QVERIFY(legacy.collectionIniPath().isEmpty());
    QVERIFY(legacy.searchHistoryIniPath().isEmpty());
    QVERIFY(legacy.syncOutboxPath().isEmpty());
}

void tst_account_core::profileContextTransitionsWithoutDuplicateRevision() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfileContext context;
    QSignalSpy changedSpy(&context, &ProfileContext::changed);

    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::Sealed);
    QCOMPARE(context.revision(), quint64(0));

    context.activateSealed(temp.path());
    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::Sealed);
    QCOMPARE(context.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 1);

    context.activateSealed(temp.path());
    QCOMPARE(context.revision(), quint64(1));
    QCOMPARE(changedSpy.count(), 1);

    context.activateLocalOnly(temp.path());
    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::LocalOnly);
    QCOMPARE(context.revision(), quint64(2));
    QCOMPARE(changedSpy.count(), 2);

    QVERIFY(
        context.activateAccount(
            QString::fromLatin1(kAccountId),
            temp.path()));
    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::Account);
    QCOMPARE(context.revision(), quint64(3));
    QCOMPARE(changedSpy.count(), 3);

    QVERIFY(
        !context.activateAccount(
            QStringLiteral("../unsafe"),
            temp.path()));
    QCOMPARE(context.revision(), quint64(3));
    QCOMPARE(changedSpy.count(), 3);

    context.activateSealed(temp.path());
    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::Sealed);
    QCOMPARE(context.revision(), quint64(4));
    QCOMPARE(changedSpy.count(), 4);

    context.activateLegacyLocal();
    QVERIFY(
        context.activeProfile().kind()
        == ProfilePaths::Kind::LegacyLocal);
    QCOMPARE(context.revision(), quint64(5));
    QCOMPARE(changedSpy.count(), 5);
}

void tst_account_core::realStoresStayIsolatedAcrossAccountProfiles() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto accountA = ProfilePaths::account(
        QStringLiteral("4c648ba1-cd40-4b47-b1ac-90f07be8e289"),
        temp.path());
    const auto accountB = ProfilePaths::account(
        QStringLiteral("b3d5da12-a828-4679-aa1d-b93b12bf0840"),
        temp.path());

    QVERIFY(accountA.has_value());
    QVERIFY(accountB.has_value());

    QVariantMap collectionEntry;
    collectionEntry.insert(QStringLiteral("id"), QStringLiteral("fixture-series"));
    collectionEntry.insert(QStringLiteral("type"), QStringLiteral("series"));
    collectionEntry.insert(QStringLiteral("title"), QStringLiteral("Account A Fixture"));

    {
        CollectionStore collectionA(accountA->collectionIniPath());
        CollectionStore collectionB(accountB->collectionIniPath());

        collectionA.add(QStringLiteral("tankoban"), collectionEntry);

        QVERIFY(collectionA.has(QStringLiteral("tankoban"),
                                QStringLiteral("fixture-series")));
        QVERIFY(!collectionB.has(QStringLiteral("tankoban"),
                                 QStringLiteral("fixture-series")));
    }

    QVariantMap progressEntry;
    progressEntry.insert(QStringLiteral("kind"), QStringLiteral("manga"));
    progressEntry.insert(QStringLiteral("id"), QStringLiteral("fixture-chapter"));
    progressEntry.insert(QStringLiteral("title"), QStringLiteral("Account A Progress"));
    progressEntry.insert(QStringLiteral("progress"), 0.42);

    {
        ProgressStore progressA(accountA->progressIniPath());
        progressA.record(progressEntry);
        progressA.flush();
    }

    {
        ProgressStore progressA(accountA->progressIniPath());
        ProgressStore progressB(accountB->progressIniPath());

        QVERIFY(!progressA.get(QStringLiteral("manga"),
                               QStringLiteral("fixture-chapter")).isEmpty());
        QVERIFY(progressB.get(QStringLiteral("manga"),
                              QStringLiteral("fixture-chapter")).isEmpty());
    }

    {
        SearchHistoryStore searchA(accountA->searchHistoryIniPath());
        SearchHistoryStore searchB(accountB->searchHistoryIniPath());

        searchA.record(QStringLiteral("tankoban"), QStringLiteral("account a only"));

        QCOMPARE(searchA.list(QStringLiteral("tankoban")).size(), 1);
        QVERIFY(searchB.list(QStringLiteral("tankoban")).isEmpty());
    }
}

void tst_account_core::adoptionRequiresMatchingSemanticDigest() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    QString error;
    auto adoption = ProfileAdoption::begin(*paths, QStringLiteral("source-digest"), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));

    QVERIFY(!adoption->markTargetVerified(QStringLiteral("different-digest"), &error));
    QVERIFY(adoption->state() == ProfileAdoption::State::Preparing);
    QVERIFY(QFileInfo::exists(paths->accountStagingRoot()));
    QVERIFY(!QFileInfo::exists(paths->profileRoot()));
}

void tst_account_core::adoptionPromotesVerifiedTarget() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    QString error;
    auto adoption = ProfileAdoption::begin(*paths, QStringLiteral("semantic-v1"), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));

    QFile marker(paths->accountStagingRoot() + QLatin1String("/collection.ini"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    QCOMPARE(marker.write("fixture"), qint64(7));
    marker.close();

    QVERIFY2(adoption->markTargetVerified(QStringLiteral("semantic-v1"), &error),
             qPrintable(error));
    QVERIFY2(adoption->promote(&error), qPrintable(error));

    QVERIFY(adoption->state() == ProfileAdoption::State::Promoted);
    QVERIFY(!QFileInfo::exists(paths->accountStagingRoot()));
    QVERIFY(QFileInfo::exists(paths->profileRoot()));
    QVERIFY(QFileInfo::exists(paths->collectionIniPath()));
}

void tst_account_core::adoptionRollsBackBeforeLegacyQuarantine() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    QString error;
    auto adoption = ProfileAdoption::begin(*paths, QStringLiteral("semantic-v1"), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QVERIFY2(adoption->markTargetVerified(QStringLiteral("semantic-v1"), &error),
             qPrintable(error));
    QVERIFY2(adoption->promote(&error), qPrintable(error));
    QVERIFY2(adoption->rollbackBeforeLegacyQuarantine(&error), qPrintable(error));

    QVERIFY(!QFileInfo::exists(paths->accountStagingRoot()));
    QVERIFY(!QFileInfo::exists(paths->profileRoot()));
    QVERIFY(!QFileInfo::exists(paths->adoptionJournalPath()));
}

void tst_account_core::adoptionReconcilesInterruptedPromotion() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    QString error;
    auto adoption = ProfileAdoption::begin(*paths, QStringLiteral("semantic-v1"), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QVERIFY2(adoption->markTargetVerified(QStringLiteral("semantic-v1"), &error),
             qPrintable(error));

    QDir parent(QFileInfo(paths->accountStagingRoot()).absolutePath());
    QVERIFY(parent.rename(QFileInfo(paths->accountStagingRoot()).fileName(),
                          QFileInfo(paths->profileRoot()).fileName()));

    auto recovered = ProfileAdoption::open(*paths, &error);
    QVERIFY2(recovered.has_value(), qPrintable(error));
    QVERIFY(recovered->state() == ProfileAdoption::State::Promoted);
}

void tst_account_core::adoptionCommitRequiresRollbackBackup() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const auto paths = ProfilePaths::account(QString::fromLatin1(kAccountId), temp.path());
    QVERIFY(paths.has_value());

    QString error;
    auto adoption = ProfileAdoption::begin(*paths, QStringLiteral("semantic-v1"), &error);
    QVERIFY2(adoption.has_value(), qPrintable(error));
    QVERIFY2(adoption->markTargetVerified(QStringLiteral("semantic-v1"), &error),
             qPrintable(error));
    QVERIFY2(adoption->promote(&error), qPrintable(error));

    QVERIFY(!adoption->markLegacyQuarantined(QStringLiteral("semantic-v1"), &error));

    QVERIFY(QDir().mkpath(paths->adoptionBackupRoot()));
    QFile backupMarker(paths->adoptionBackupRoot() + QLatin1String("/manifest.json"));
    QVERIFY(backupMarker.open(QIODevice::WriteOnly));
    QCOMPARE(backupMarker.write("fixture"), qint64(7));
    backupMarker.close();

    QVERIFY(!adoption->markLegacyQuarantined(QStringLiteral("wrong-digest"), &error));
    QVERIFY2(adoption->markLegacyQuarantined(QStringLiteral("semantic-v1"), &error),
             qPrintable(error));
    QCOMPARE(adoption->snapshot().legacyBackupSemanticDigest,
             QStringLiteral("semantic-v1"));
    QVERIFY2(adoption->commit(&error), qPrintable(error));
    QVERIFY(adoption->state() == ProfileAdoption::State::Committed);

    QVERIFY(!adoption->rollbackBeforeLegacyQuarantine(&error));
    QVERIFY(QFileInfo::exists(paths->profileRoot()));
    QVERIFY(QFileInfo::exists(paths->adoptionBackupRoot()));
}

QTEST_GUILESS_MAIN(tst_account_core)

#include "tst_account_core.moc"
