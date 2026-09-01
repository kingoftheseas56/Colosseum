// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/SyncAdapter.h"
#include "account/SyncAdapterRegistry.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <memory>
#include <vector>

class FakeSyncAdapter final : public SyncAdapter {
    Q_OBJECT

public:
    explicit FakeSyncAdapter(
        const QString &categoryId,
        QObject *parent = nullptr)
        : SyncAdapter(parent),
          m_categoryId(categoryId) {}

    QString categoryId() const override {
        return m_categoryId;
    }

    int schemaVersion() const override {
        return m_schemaVersion;
    }

    quint64 revision() const override {
        return m_revision;
    }

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error) const override {
        ++m_exportCalls;

        if (!m_exportResult) {
            if (error) {
                *error = QStringLiteral(
                    "fixture export failure");
            }
            return false;
        }

        if (!snapshot) {
            if (error) {
                *error = QStringLiteral(
                    "fixture output missing");
            }
            return false;
        }

        snapshot->revision =
            m_revision;
        snapshot->records =
            m_records;
        return true;
    }

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error) override {
        ++m_applyCalls;
        m_lastAppliedRecordKey =
            recordKey;
        m_lastAppliedOperation =
            operation;
        m_lastAppliedSchemaVersion =
            schemaVersion;

        if (!m_applyResult) {
            if (error) {
                *error = QStringLiteral(
                    "fixture apply failure");
            }
            return false;
        }

        if (operation
            == SyncWireOperation::Put) {
            upsert(
                recordKey,
                payload);
        } else {
            remove(
                recordKey);
        }

        ++m_revision;

        if (m_emitDuringApply) {
            emit localMutationAvailable(
                m_revision);
        }

        return true;
    }

    void mutateLocal(
        const QString &recordKey,
        const QJsonValue &payload) {
        upsert(
            recordKey,
            payload);
        ++m_revision;
        emit localMutationAvailable(
            m_revision);
    }

    void setRecords(
        const QList<SyncAdapterRecord> &records) {
        m_records =
            records;
    }

    void setSchemaVersion(
        int schemaVersion) {
        m_schemaVersion =
            schemaVersion;
    }

    void setCategoryId(
        const QString &categoryId) {
        m_categoryId =
            categoryId;
    }

    void setExportResult(
        bool result) {
        m_exportResult =
            result;
    }

    void setApplyResult(
        bool result) {
        m_applyResult =
            result;
    }

    void setEmitDuringApply(
        bool enabled) {
        m_emitDuringApply =
            enabled;
    }

    int exportCalls() const {
        return m_exportCalls;
    }

    int applyCalls() const {
        return m_applyCalls;
    }

    QString lastAppliedRecordKey() const {
        return m_lastAppliedRecordKey;
    }

    SyncWireOperation
    lastAppliedOperation() const {
        return m_lastAppliedOperation;
    }

    int lastAppliedSchemaVersion() const {
        return m_lastAppliedSchemaVersion;
    }

private:
    void upsert(
        const QString &recordKey,
        const QJsonValue &payload) {
        for (SyncAdapterRecord &record :
             m_records) {
            if (record.recordKey
                == recordKey) {
                record.payload =
                    payload;
                return;
            }
        }

        m_records.append(
            SyncAdapterRecord{
                recordKey,
                payload});
    }

    void remove(
        const QString &recordKey) {
        for (qsizetype index = 0;
             index < m_records.size();
             ++index) {
            if (m_records.at(index)
                    .recordKey
                == recordKey) {
                m_records.removeAt(index);
                return;
            }
        }
    }

    QString m_categoryId;
    int m_schemaVersion = 1;
    quint64 m_revision = 0;

    QList<SyncAdapterRecord> m_records = {
        {
            QStringLiteral("fixture"),
            QJsonObject{
                {
                    QStringLiteral(
                        "logicalId"),
                    QStringLiteral(
                        "fixture")
                }
            }
        }
    };

    bool m_exportResult = true;
    bool m_applyResult = true;
    bool m_emitDuringApply = false;

    mutable int m_exportCalls = 0;
    int m_applyCalls = 0;
    QString m_lastAppliedRecordKey;
    SyncWireOperation m_lastAppliedOperation =
        SyncWireOperation::Put;
    int m_lastAppliedSchemaVersion = 0;
};

QStringList contractRegisterableCategories() {
    return {
        QStringLiteral("collection"),
        QStringLiteral("continue_progress"),
        QStringLiteral("watch_state"),
        QStringLiteral("full_history"),
        QStringLiteral(
            "explicit_content_preference"),
        QStringLiteral(
            "theatre_track_preferences"),
        QStringLiteral(
            "theatre_row_customization"),
        QStringLiteral("extension_roster")
    };
}

QJsonObject safePayload(
    const QString &id =
        QStringLiteral("fixture")) {
    return QJsonObject{
        {
            QStringLiteral("logicalId"),
            id
        },
        {
            QStringLiteral("value"),
            QStringLiteral("safe")
        }
    };
}

SyncAdapterMutation remotePut(
    int schemaVersion = 1) {
    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("fixture");
    mutation.schemaVersion =
        schemaVersion;
    mutation.operation =
        SyncWireOperation::Put;
    mutation.payload =
        safePayload(
            QStringLiteral("remote"));
    return mutation;
}

class tst_sync_adapter_registry
    : public QObject {
    Q_OBJECT

private slots:
    void productionRegistryStartsEmpty();
    void every5AEligibleCategoryCanRegisterContractAdapter();
    void duplicateCategoryIsRejected();
    void sameAdapterInstanceCannotOwnTwoCategories();
    void unknownCategoryIsRejected();
    void localOnlyCategoryIsRejected();
    void secretCategoryIsRejected();
    void absentOwnerIsRejected();
    void blockedPortableSeamIsRejected();
    void noncanonicalCategoryIsRejected();
    void invalidSchemaVersionIsRejected();
    void registeredCategoriesAreSorted();

    void exportSnapshotContainsRecordKeysAndRevision();
    void duplicateExportRecordKeyIsRejected();
    void invalidExportRecordKeyIsRejected();
    void exportFailureIsReported();
    void exportPayloadStillPasses5AFirewall();

    void matchingRemotePutApplies();
    void matchingRemoteDeleteApplies();
    void mismatchedRemoteSchemaIsRejectedBeforeAdapterCall();
    void noncanonicalRemoteCategoryIsRejected();
    void invalidRemoteRecordKeyIsRejected();
    void remotePayloadStillPasses5AFirewall();
    void remoteDeletePayloadIsRejected();
    void remoteApplyFailureIsReported();

    void localMutationIsForwarded();
    void synchronousImportEchoIsSuppressed();
    void remoteAppliedIncludesRecordKey();

    void adapterIdentityCannotChangeAfterRegistration();
    void adapterSchemaCannotChangeAfterRegistration();
    void adapterDestructionUnregistersAutomatically();
    void explicitUnregisterDisconnectsMutationForwarding();
};

void tst_sync_adapter_registry::
productionRegistryStartsEmpty() {
    SyncAdapterRegistry registry;
    QVERIFY(
        registry.registeredCategories()
            .isEmpty());
}

void tst_sync_adapter_registry::
every5AEligibleCategoryCanRegisterContractAdapter() {
    SyncAdapterRegistry registry;
    std::vector<
        std::unique_ptr<FakeSyncAdapter>>
        adapters;

    for (const QString &category :
         contractRegisterableCategories()) {
        auto adapter =
            std::make_unique<
                FakeSyncAdapter>(
                    category);

        SyncAdapterRegistryError error;
        QVERIFY2(
            registry.registerAdapter(
                adapter.get(),
                &error),
            qPrintable(
                error.code
                + QStringLiteral(": ")
                + error.detail));

        adapters.push_back(
            std::move(adapter));
    }

    QStringList expected =
        contractRegisterableCategories();
    expected.sort();

    QCOMPARE(
        registry.registeredCategories(),
        expected);
}

void tst_sync_adapter_registry::
duplicateCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter first(
        QStringLiteral("collection"));
    FakeSyncAdapter second(
        QStringLiteral("collection"));

    QVERIFY(
        registry.registerAdapter(
            &first));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &second,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "duplicate_category"));
}

void tst_sync_adapter_registry::
sameAdapterInstanceCannotOwnTwoCategories() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));

    QVERIFY(
        registry.registerAdapter(
            &adapter));

    adapter.setCategoryId(
        QStringLiteral(
            "continue_progress"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "adapter_already_registered"));
}

void tst_sync_adapter_registry::
unknownCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(
            "future_magic_category"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "unknown_category"));
}

void tst_sync_adapter_registry::
localOnlyCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(
            "search_history"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "local_only_category"));
}

void tst_sync_adapter_registry::
secretCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(
            "refresh_token"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "secret_category"));
}

void tst_sync_adapter_registry::
absentOwnerIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(
            "theatre_watched_history"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "owner_not_confirmed"));
}

void tst_sync_adapter_registry::
blockedPortableSeamIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(
            "biblio_bookmarks"));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "category_not_exportable_yet"));
}

void tst_sync_adapter_registry::
noncanonicalCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral(" Collection "));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "noncanonical_category"));
}

void tst_sync_adapter_registry::
invalidSchemaVersionIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setSchemaVersion(0);

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.registerAdapter(
            &adapter,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "invalid_schema_version"));
}

void tst_sync_adapter_registry::
registeredCategoriesAreSorted() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter z(
        QStringLiteral(
            "extension_roster"));
    FakeSyncAdapter a(
        QStringLiteral("collection"));
    FakeSyncAdapter m(
        QStringLiteral(
            "theatre_track_preferences"));

    QVERIFY(registry.registerAdapter(&z));
    QVERIFY(registry.registerAdapter(&a));
    QVERIFY(registry.registerAdapter(&m));

    const QStringList expected = {
        QStringLiteral("collection"),
        QStringLiteral(
            "extension_roster"),
        QStringLiteral(
            "theatre_track_preferences")
    };

    QCOMPARE(
        registry.registeredCategories(),
        expected);
}

void tst_sync_adapter_registry::
exportSnapshotContainsRecordKeysAndRevision() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));

    adapter.setSchemaVersion(3);
    adapter.mutateLocal(
        QStringLiteral("manga/item"),
        safePayload(
            QStringLiteral("local")));

    QVERIFY(
        registry.registerAdapter(
            &adapter));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY2(
        registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error),
        qPrintable(error.detail));

    QCOMPARE(
        snapshot.categoryId,
        QStringLiteral("collection"));
    QCOMPARE(snapshot.schemaVersion, 3);
    QCOMPARE(snapshot.revision, quint64(1));
    QCOMPARE(snapshot.records.size(), 2);

    bool found = false;
    for (const SyncAdapterRecord &record :
         snapshot.records) {
        if (record.recordKey
            == QLatin1String(
                "manga/item")) {
            found = true;
            QCOMPARE(
                record.payload,
                QJsonValue(
                    safePayload(
                        QStringLiteral(
                            "local"))));
        }
    }
    QVERIFY(found);
    QCOMPARE(adapter.exportCalls(), 1);
}

void tst_sync_adapter_registry::
duplicateExportRecordKeyIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setRecords({
        {
            QStringLiteral("same"),
            safePayload()
        },
        {
            QStringLiteral("same"),
            safePayload(
                QStringLiteral("two"))
        }
    });
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "duplicate_record_key"));
}

void tst_sync_adapter_registry::
invalidExportRecordKeyIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setRecords({
        {
            QStringLiteral("../private"),
            safePayload()
        }
    });
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "invalid_record_key"));
}

void tst_sync_adapter_registry::
exportFailureIsReported() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setExportResult(false);
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "adapter_export_failed"));
}

void tst_sync_adapter_registry::
exportPayloadStillPasses5AFirewall() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setRecords({
        {
            QStringLiteral("fixture"),
            QJsonObject{
                {
                    QStringLiteral("path"),
                    QStringLiteral(
                        "C:\\Private\\book.cbz")
                }
            }
        }
    });
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral("forbidden_field"));
}

void tst_sync_adapter_registry::
matchingRemotePutApplies() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setSchemaVersion(2);
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut(2);

    QVERIFY(
        registry.applyRemote(
            mutation));

    QCOMPARE(adapter.applyCalls(), 1);
    QCOMPARE(
        adapter.lastAppliedRecordKey(),
        QStringLiteral("fixture"));
    QCOMPARE(
        adapter.lastAppliedOperation(),
        SyncWireOperation::Put);
    QCOMPARE(
        adapter.lastAppliedSchemaVersion(),
        2);
}

void tst_sync_adapter_registry::
matchingRemoteDeleteApplies() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation;
    mutation.categoryId =
        QStringLiteral("collection");
    mutation.recordKey =
        QStringLiteral("fixture");
    mutation.schemaVersion = 1;
    mutation.operation =
        SyncWireOperation::Delete;

    QVERIFY(
        registry.applyRemote(
            mutation));

    QCOMPARE(adapter.applyCalls(), 1);
    QCOMPARE(
        adapter.lastAppliedOperation(),
        SyncWireOperation::Delete);
}

void tst_sync_adapter_registry::
mismatchedRemoteSchemaIsRejectedBeforeAdapterCall() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setSchemaVersion(2);
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut(1);

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            mutation,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "unsupported_schema_version"));
    QCOMPARE(adapter.applyCalls(), 0);
}

void tst_sync_adapter_registry::
noncanonicalRemoteCategoryIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut();
    mutation.categoryId =
        QStringLiteral(" Collection ");

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            mutation,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "noncanonical_category"));
    QCOMPARE(adapter.applyCalls(), 0);
}

void tst_sync_adapter_registry::
invalidRemoteRecordKeyIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut();
    mutation.recordKey =
        QStringLiteral("a/../b");

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            mutation,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "invalid_record_key"));
    QCOMPARE(adapter.applyCalls(), 0);
}

void tst_sync_adapter_registry::
remotePayloadStillPasses5AFirewall() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut();
    mutation.payload =
        QJsonObject{
            {
                QStringLiteral("path"),
                QStringLiteral(
                    "C:\\Private\\book.cbz")
            }
        };

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            mutation,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral("forbidden_field"));
    QCOMPARE(adapter.applyCalls(), 0);
}

void tst_sync_adapter_registry::
remoteDeletePayloadIsRejected() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterMutation mutation =
        remotePut();
    mutation.operation =
        SyncWireOperation::Delete;

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            mutation,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "delete_payload_not_empty"));
    QCOMPARE(adapter.applyCalls(), 0);
}

void tst_sync_adapter_registry::
remoteApplyFailureIsReported() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setApplyResult(false);
    QVERIFY(registry.registerAdapter(&adapter));

    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.applyRemote(
            remotePut(),
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "adapter_apply_failed"));
}

void tst_sync_adapter_registry::
localMutationIsForwarded() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    QSignalSpy spy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    adapter.mutateLocal(
        QStringLiteral("fixture"),
        safePayload(
            QStringLiteral("local")));

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args =
        spy.takeFirst();
    QCOMPARE(
        args.at(0).toString(),
        QStringLiteral("collection"));
    QCOMPARE(
        args.at(1).toULongLong(),
        quint64(1));
}

void tst_sync_adapter_registry::
synchronousImportEchoIsSuppressed() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    adapter.setEmitDuringApply(true);
    QVERIFY(registry.registerAdapter(&adapter));

    QSignalSpy localSpy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    QVERIFY(
        registry.applyRemote(
            remotePut()));

    QCOMPARE(localSpy.count(), 0);
}

void tst_sync_adapter_registry::
remoteAppliedIncludesRecordKey() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    QSignalSpy spy(
        &registry,
        &SyncAdapterRegistry::
            remoteApplied);

    QVERIFY(
        registry.applyRemote(
            remotePut()));

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args =
        spy.takeFirst();
    QCOMPARE(
        args.at(0).toString(),
        QStringLiteral("collection"));
    QCOMPARE(
        args.at(1).toString(),
        QStringLiteral("fixture"));
    QCOMPARE(
        args.at(2).toULongLong(),
        quint64(1));
}

void tst_sync_adapter_registry::
adapterIdentityCannotChangeAfterRegistration() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    adapter.setCategoryId(
        QStringLiteral(
            "continue_progress"));

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "adapter_identity_changed"));
}

void tst_sync_adapter_registry::
adapterSchemaCannotChangeAfterRegistration() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    adapter.setSchemaVersion(2);

    SyncAdapterSnapshot snapshot;
    SyncAdapterRegistryError error;
    QVERIFY(
        !registry.exportSnapshot(
            QStringLiteral("collection"),
            &snapshot,
            &error));
    QCOMPARE(
        error.code,
        QStringLiteral(
            "adapter_schema_changed"));
}

void tst_sync_adapter_registry::
adapterDestructionUnregistersAutomatically() {
    SyncAdapterRegistry registry;
    auto adapter =
        std::make_unique<FakeSyncAdapter>(
            QStringLiteral("collection"));
    QVERIFY(
        registry.registerAdapter(
            adapter.get()));

    QSignalSpy spy(
        &registry,
        &SyncAdapterRegistry::
            adapterUnregistered);

    adapter.reset();

    QCOMPARE(spy.count(), 1);
    QVERIFY(
        !registry.contains(
            QStringLiteral("collection")));
}

void tst_sync_adapter_registry::
explicitUnregisterDisconnectsMutationForwarding() {
    SyncAdapterRegistry registry;
    FakeSyncAdapter adapter(
        QStringLiteral("collection"));
    QVERIFY(registry.registerAdapter(&adapter));

    QSignalSpy spy(
        &registry,
        &SyncAdapterRegistry::
            localMutationAvailable);

    QVERIFY(
        registry.unregisterAdapter(
            QStringLiteral("collection")));

    adapter.mutateLocal(
        QStringLiteral("fixture"),
        safePayload());

    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(tst_sync_adapter_registry)
#include "tst_sync_adapter_registry.moc"
