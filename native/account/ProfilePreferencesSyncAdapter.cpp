// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePreferencesSyncAdapter.h"

#include "ProfilePreferencesStore.h"

#include <QJsonObject>
#include <QtGlobal>

ProfilePreferencesSyncAdapter::
ProfilePreferencesSyncAdapter(
    ProfilePreferencesStore *store,
    QObject *parent)
    : SyncAdapter(parent),
      m_store(store) {
    Q_ASSERT(m_store);

    setObjectName(
        QStringLiteral(
            "profilePreferencesSyncAdapter"));

    connect(
        m_store,
        &ProfilePreferencesStore::syncDirty,
        this,
        [this]() {
            emit localMutationAvailable(
                revision());
        });
}

QString ProfilePreferencesSyncAdapter::
categoryId() const {
    return QStringLiteral(
        "explicit_content_preference");
}

int ProfilePreferencesSyncAdapter::
schemaVersion() const {
    return 1;
}

quint64 ProfilePreferencesSyncAdapter::
revision() const {
    return m_store
        ? static_cast<quint64>(
              qMax(
                  0,
                  m_store->revision()))
        : 0;
}

bool ProfilePreferencesSyncAdapter::
exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        return fail(
            error,
            QStringLiteral(
                "A preference sync snapshot output is required."));
    }

    if (!m_store) {
        return fail(
            error,
            QStringLiteral(
                "The profile preference owner is unavailable."));
    }

    snapshot->revision =
        revision();
    snapshot->records.clear();

    if (!m_store->hasShowExplicitValue())
        return true;

    QJsonObject payload;
    payload.insert(
        QStringLiteral("showExplicit"),
        m_store->showExplicit());

    snapshot->records = {
        SyncAdapterRecord{
            fixedRecordKey(),
            payload,
            -1}
    };

    return true;
}

bool ProfilePreferencesSyncAdapter::
applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_store) {
        return fail(
            error,
            QStringLiteral(
                "The profile preference owner is unavailable."));
    }

    if (schemaVersionValue
        != schemaVersion()) {
        return fail(
            error,
            QStringLiteral(
                "The explicit-content preference sync schema is unsupported."));
    }

    if (recordKey
        != fixedRecordKey()) {
        return fail(
            error,
            QStringLiteral(
                "The explicit-content preference record key is invalid."));
    }

    if (operation
        == SyncWireOperation::Delete) {
        return m_store
            ->clearSyncedShowExplicit();
    }

    if (!payload.isObject()) {
        return fail(
            error,
            QStringLiteral(
                "The explicit-content preference PUT requires an object payload."));
    }

    const QJsonObject object =
        payload.toObject();

    if (object.size() != 1
        || !object.contains(
            QStringLiteral(
                "showExplicit"))
        || !object
                .value(
                    QStringLiteral(
                        "showExplicit"))
                .isBool()) {
        return fail(
            error,
            QStringLiteral(
                "The explicit-content preference payload is malformed."));
    }

    return m_store
        ->applySyncedShowExplicit(
            object
                .value(
                    QStringLiteral(
                        "showExplicit"))
                .toBool());
}

QString ProfilePreferencesSyncAdapter::
fixedRecordKey() {
    return QStringLiteral(
        "preferences/explicit-content");
}

bool ProfilePreferencesSyncAdapter::fail(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
