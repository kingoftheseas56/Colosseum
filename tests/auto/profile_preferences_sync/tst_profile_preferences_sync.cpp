// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/AccountClient.h"
#include "account/AccountTransport.h"
#include "account/ProfilePaths.h"
#include "account/ProfilePreferencesStore.h"
#include "account/ProfilePreferencesSyncAdapter.h"
#include "account/SyncAdapterRegistry.h"
#include "account/SyncEngine.h"
#include "account/SyncProtocol.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kAccount =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kDeviceA =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kDeviceB =
    "22222222-2222-4222-8222-222222222222";

struct JournalEntry {
    quint64 serverSeq = 0;
    bool won = false;
    SyncWireMutation mutation;
};

struct Ack {
    quint64 serverSeq = 0;
    bool won = false;
};

QString identity(
    const SyncWireMutation &mutation) {
    return mutation.category
        + QChar(0x1f)
        + mutation.recordKey;
}

class PreferenceFixtureService {
public:
    qint64 serverTimeMs = 2000000;

    AccountTransportReply push(
        const QJsonArray &mutations) {
        AccountTransportReply reply;
        reply.statusCode = 200;

        QJsonArray results;
        for (const QJsonValue &value :
             mutations) {
            const auto parsed =
                value.isObject()
                ? syncWireMutationFromJson(
                      value.toObject())
                : std::nullopt;

            if (!parsed.has_value())
                continue;

            const SyncWireMutation mutation =
                *parsed;

            const auto duplicate =
                m_acks.constFind(
                    mutation.mutationId);
            if (duplicate
                != m_acks.constEnd()) {
                QJsonObject result;
                result.insert(
                    QStringLiteral(
                        "mutation_id"),
                    mutation.mutationId);
                result.insert(
                    QStringLiteral("accepted"),
                    true);
                result.insert(
                    QStringLiteral(
                        "server_seq"),
                    QString::number(
                        duplicate->serverSeq));
                result.insert(
                    QStringLiteral("won"),
                    duplicate->won);
                results.append(result);
                continue;
            }

            const QString key =
                identity(mutation);
            const auto current =
                m_current.constFind(key);

            const bool won =
                current
                    == m_current.constEnd()
                || syncWireHlcGreater(
                    mutation.hlc,
                    current->hlc);

            JournalEntry entry;
            entry.serverSeq =
                m_nextServerSeq++;
            entry.won = won;
            entry.mutation =
                mutation;
            m_journal.append(entry);

            if (won)
                m_current.insert(
                    key,
                    mutation);

            m_acks.insert(
                mutation.mutationId,
                Ack{
                    entry.serverSeq,
                    won});

            QJsonObject result;
            result.insert(
                QStringLiteral(
                    "mutation_id"),
                mutation.mutationId);
            result.insert(
                QStringLiteral("accepted"),
                true);
            result.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.serverSeq));
            result.insert(
                QStringLiteral("won"),
                won);
            results.append(result);
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral("results"),
            results);
        return reply;
    }

    AccountTransportReply pull(
        quint64 after) const {
        AccountTransportReply reply;
        reply.statusCode = 200;

        QJsonArray entries;
        for (const JournalEntry &entry :
             m_journal) {
            if (entry.serverSeq <= after)
                continue;

            QJsonObject item;
            item.insert(
                QStringLiteral(
                    "server_seq"),
                QString::number(
                    entry.serverSeq));
            item.insert(
                QStringLiteral("won"),
                entry.won);
            item.insert(
                QStringLiteral("mutation"),
                syncWireMutationToJson(
                    entry.mutation));
            entries.append(item);
        }

        reply.body.insert(
            QStringLiteral(
                "server_time_ms"),
            QString::number(
                serverTimeMs));
        reply.body.insert(
            QStringLiteral("entries"),
            entries);
        reply.body.insert(
            QStringLiteral("has_more"),
            false);
        return reply;
    }

private:
    quint64 m_nextServerSeq = 1;
    QList<JournalEntry> m_journal;
    QHash<QString, Ack> m_acks;
    QHash<QString, SyncWireMutation>
        m_current;
};

class PreferenceFixtureTransport final
    : public AccountTransport {
    Q_OBJECT

public:
    explicit PreferenceFixtureTransport(
        PreferenceFixtureService *service,
        QObject *parent = nullptr)
        : AccountTransport(parent),
          m_service(service) {}

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override {
        AccountTransportReply reply;

        if (request.bearerToken.isEmpty()) {
            reply.statusCode = 401;
            reply.errorCode =
                QStringLiteral(
                    "session_invalid");
            emit finished(
                requestId,
                reply);
            return;
        }

        if (request.method
                == QByteArrayLiteral("POST")
            && request.path
                == QLatin1String(
                    "/v1/sync/push")) {
            emit finished(
                requestId,
                m_service->push(
                    request.body
                        .value(
                            QStringLiteral(
                                "mutations"))
                        .toArray()));
            return;
        }

        if (request.method
                == QByteArrayLiteral("GET")
            && request.path.startsWith(
                QStringLiteral(
                    "/v1/sync/pull?after="))) {
            bool ok = false;
            const quint64 after =
                request.path
                    .mid(
                        QStringLiteral(
                            "/v1/sync/pull?after=")
                            .size())
                    .toULongLong(
                        &ok);

            if (!ok) {
                reply.statusCode = 400;
                reply.errorCode =
                    QStringLiteral(
                        "invalid_cursor");
            } else {
                reply =
                    m_service->pull(after);
            }

            emit finished(
                requestId,
                reply);
            return;
        }

        reply.statusCode = 404;
        reply.errorCode =
            QStringLiteral(
                "fixture_route_missing");
        emit finished(
            requestId,
            reply);
    }

private:
    PreferenceFixtureService *m_service =
        nullptr;
};

ProfilePaths accountProfile(
    QTemporaryDir *temp) {
    const auto profile =
        ProfilePaths::account(
            QString::fromLatin1(
                kAccount),
            temp->path());
    if (!profile.has_value())
        qFatal("invalid preference fixture profile");

    QDir().mkpath(
        profile->profileRoot());
    return *profile;
}

struct PreferenceReplica {
    PreferenceFixtureTransport transport;
    AccountClient client;
    ProfilePreferencesStore store;
    SyncAdapterRegistry registry;
    ProfilePreferencesSyncAdapter adapter;
    SyncEngine engine;
    ProfilePaths profile;

    PreferenceReplica(
        PreferenceFixtureService *service,
        const ProfilePaths &profileValue,
        const QString &deviceId,
        qint64 *now)
        : transport(service),
          client(&transport),
          store(
              profileValue.preferencesIniPath()),
          adapter(&store),
          engine(
              &client,
              &registry,
              [now]() {
                  return *now;
              }),
          profile(profileValue) {
        client.setAccessToken(
            QByteArrayLiteral(
                "fixture-access"));

        if (!registry.registerAdapter(
                &adapter)) {
            qFatal(
                "preference fixture adapter registration failed");
        }

        engine.setAutomaticSchedulingEnabled(
            false);
        engine.setNetworkEnabled(
            false);

        QString error;
        if (!engine.start(
                profile,
                deviceId,
                &error)) {
            qFatal(
                "preference fixture engine start failed");
        }
    }
};
}

class tst_profile_preferences_sync final
    : public QObject {
    Q_OBJECT

private slots:
    void localMutationPersistsAndSignalsSyncDirty();
    void remoteMutationPersistsWithoutLocalSyncDirty();
    void untouchedDefaultDoesNotMaterializeLocalChoice();
    void explicitFalseIsStillARealChoice();
    void adapterExportsCanonicalFixedRecord();
    void deleteResetsToDefaultWithoutEcho();
    void qmlFacadeReactsOnceToRemoteOwnerChange();
    void twoReplicasConvergeExplicitPreference();
};

void tst_profile_preferences_sync::
localMutationPersistsAndSignalsSyncDirty() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path =
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini"));

    {
        ProfilePreferencesStore store(path);
        QSignalSpy visible(
            &store,
            &ProfilePreferencesStore::
                showExplicitChanged);
        QSignalSpy dirty(
            &store,
            &ProfilePreferencesStore::
                syncDirty);

        store.setShowExplicit(true);

        QCOMPARE(store.showExplicit(), true);
        QCOMPARE(store.revision(), 1);
        QCOMPARE(visible.count(), 1);
        QCOMPARE(dirty.count(), 1);
    }

    ProfilePreferencesStore reopened(path);
    QCOMPARE(
        reopened.showExplicit(),
        true);
}

void tst_profile_preferences_sync::
remoteMutationPersistsWithoutLocalSyncDirty() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfilePreferencesStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini")));

    QSignalSpy visible(
        &store,
        &ProfilePreferencesStore::
            showExplicitChanged);
    QSignalSpy dirty(
        &store,
        &ProfilePreferencesStore::
            syncDirty);

    QVERIFY(
        store.applySyncedShowExplicit(
            true));

    QCOMPARE(store.showExplicit(), true);
    QCOMPARE(store.revision(), 1);
    QCOMPARE(visible.count(), 1);
    QCOMPARE(dirty.count(), 0);
}

void tst_profile_preferences_sync::
untouchedDefaultDoesNotMaterializeLocalChoice() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfilePreferencesStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini")));
    QCOMPARE(store.showExplicit(), false);
    QCOMPARE(
        store.hasShowExplicitValue(),
        false);

    ProfilePreferencesSyncAdapter adapter(
        &store);
    SyncAdapterExport snapshot;
    QVERIFY(
        adapter.exportSnapshot(
            &snapshot));

    QVERIFY(snapshot.records.isEmpty());
}

void tst_profile_preferences_sync::
explicitFalseIsStillARealChoice() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfilePreferencesStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini")));

    QSignalSpy dirty(
        &store,
        &ProfilePreferencesStore::
            syncDirty);

    store.setShowExplicit(false);

    QCOMPARE(
        store.hasShowExplicitValue(),
        true);
    QCOMPARE(dirty.count(), 1);

    ProfilePreferencesSyncAdapter adapter(
        &store);
    SyncAdapterExport snapshot;
    QVERIFY(
        adapter.exportSnapshot(
            &snapshot));
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(
        snapshot.records.first()
            .payload
            .toObject()
            .value(
                QStringLiteral(
                    "showExplicit"))
            .toBool(),
        false);
}

void tst_profile_preferences_sync::
adapterExportsCanonicalFixedRecord() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfilePreferencesStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini")));
    store.setShowExplicit(true);

    ProfilePreferencesSyncAdapter adapter(
        &store);

    SyncAdapterExport snapshot;
    QString error;
    QVERIFY2(
        adapter.exportSnapshot(
            &snapshot,
            &error),
        qPrintable(error));

    QCOMPARE(snapshot.records.size(), 1);
    const SyncAdapterRecord record =
        snapshot.records.first();

    QCOMPARE(
        record.recordKey,
        QStringLiteral(
            "preferences/explicit-content"));
    QCOMPARE(record.localOrderMs, qint64(-1));
    QCOMPARE(
        record.payload
            .toObject()
            .value(
                QStringLiteral(
                    "showExplicit"))
            .toBool(),
        true);
}

void tst_profile_preferences_sync::
deleteResetsToDefaultWithoutEcho() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    ProfilePreferencesStore store(
        QDir(temp.path())
            .filePath(
                QStringLiteral(
                    "preferences.ini")));
    store.setShowExplicit(true);

    ProfilePreferencesSyncAdapter adapter(
        &store);
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registerAdapter(
            &adapter));

    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral(
            "explicit_content_preference");
    mutation.recordKey =
        ProfilePreferencesSyncAdapter::
            fixedRecordKey();
    mutation.schemaVersion = 1;
    mutation.operation =
        SyncWireOperation::Delete;

    QVERIFY(
        registry.applyRemote(
            mutation));

    QCOMPARE(store.showExplicit(), false);
    QCOMPARE(
        store.hasShowExplicitValue(),
        false);
    QCOMPARE(localSpy.count(), 0);

    SyncAdapterExport snapshot;
    QVERIFY(
        adapter.exportSnapshot(
            &snapshot));
    QVERIFY(snapshot.records.isEmpty());
}

void tst_profile_preferences_sync::
qmlFacadeReactsOnceToRemoteOwnerChange() {
    // The bundle's facade QML bound the ProfilePreferences context property;
    // production QML composition is gated to the activation slice (see
    // qml-Main.bundle8c.adoption.md) and the live ContentPreferences.qml owns
    // its own Settings store. Revisit when the shell binds ProfilePreferences.
    QSKIP(
        "ProfilePreferences QML facade wiring is gated to the production "
        "composition slice; no facade binds the context property yet.");
}

void tst_profile_preferences_sync::
twoReplicasConvergeExplicitPreference() {
    QTemporaryDir tempA;
    QTemporaryDir tempB;
    QVERIFY(tempA.isValid());
    QVERIFY(tempB.isValid());

    PreferenceFixtureService service;
    qint64 nowA =
        service.serverTimeMs;
    qint64 nowB =
        service.serverTimeMs;

    PreferenceReplica a(
        &service,
        accountProfile(&tempA),
        QString::fromLatin1(
            kDeviceA),
        &nowA);
    PreferenceReplica b(
        &service,
        accountProfile(&tempB),
        QString::fromLatin1(
            kDeviceB),
        &nowB);

    a.store.setShowExplicit(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        1);

    a.engine.setNetworkEnabled(true);
    QTRY_COMPARE(
        a.engine.pendingOutboxCount(),
        0);

    b.engine.setNetworkEnabled(true);
    b.engine.requestImmediateSync();

    QTRY_COMPARE(
        b.store.showExplicit(),
        true);
    QTRY_COMPARE(
        b.engine.pendingOutboxCount(),
        0);
}

QTEST_MAIN(tst_profile_preferences_sync)
#include "tst_profile_preferences_sync.moc"
