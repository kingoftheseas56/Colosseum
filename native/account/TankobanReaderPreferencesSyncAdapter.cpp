// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "TankobanReaderPreferencesSyncAdapter.h"

#include "SyncPayloadFirewall.h"
#include "TankobanReaderPreferencesCodec.h"
#include "TankobanReaderPreferencesOwner.h"

#include <QJsonObject>
#include <QSet>
#include <QtGlobal>

#include <algorithm>

TankobanReaderPreferencesSyncAdapter::
TankobanReaderPreferencesSyncAdapter(
    TankobanReaderPreferencesOwner *owner,
    QObject *parent)
    : SyncAdapter(parent),
      m_owner(owner) {
    Q_ASSERT(m_owner);

    setObjectName(
        QStringLiteral(
            "tankobanReaderPreferencesSyncAdapter"));

    connect(
        m_owner,
        &TankobanReaderPreferencesOwner::
            localMutationAvailable,
        this,
        [this](quint64) {
            emit localMutationAvailable(
                revision());
        });
}

QString TankobanReaderPreferencesSyncAdapter::
categoryId() const {
    return QStringLiteral(
        "tankoban_reader_preferences");
}

int TankobanReaderPreferencesSyncAdapter::
schemaVersion() const {
    return TankobanReaderPreferencesCodec::
        kSchemaVersion;
}

quint64 TankobanReaderPreferencesSyncAdapter::
revision() const {
    return m_owner
        ? m_owner->revision()
        : 0;
}

bool TankobanReaderPreferencesSyncAdapter::
exportSnapshot(
    SyncAdapterExport *snapshot,
    QString *error) const {
    if (!snapshot) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban reader-preference snapshot output is required."));
    }

    if (!m_owner) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban reader-preference owner is unavailable."));
    }

    snapshot->revision =
        revision();
    snapshot->records.clear();

    QStringList seriesIds =
        m_owner->tankobanSeriesIds();
    std::sort(
        seriesIds.begin(),
        seriesIds.end());

    QSet<QString> seenKeys;

    for (const QString &seriesId :
         seriesIds) {
        if (seriesId.trimmed().isEmpty()
            || seriesId
                != seriesId.trimmed()) {
            return fail(
                error,
                QStringLiteral(
                    "The Tankoban owner enumerated an invalid logical series id."));
        }

        if (SyncPayloadFirewall::
                isFilesystemPathValue(
                    seriesId)) {
            // A legacy/path-keyed record is machine-local and cannot become a
            // cloud identity. It does not block other logical series.
            continue;
        }

        const QVariantMap raw =
            m_owner->rawRecord(
                seriesId);

        if (!TankobanReaderPreferencesCodec::
                hasApprovedOpinion(raw)) {
            continue;
        }

        const QString key =
            TankobanReaderPreferencesCodec::
                recordKey(seriesId);

        if (key.isEmpty()
            || !isValidSyncWireRecordKey(key)) {
            return fail(
                error,
                QStringLiteral(
                    "A Tankoban logical series id could not form a valid sync record key."));
        }

        if (seenKeys.contains(key)) {
            return fail(
                error,
                QStringLiteral(
                    "The Tankoban owner enumerated the same logical series more than once."));
        }
        seenKeys.insert(key);

        QJsonObject payload;
        QString codecError;
        if (!TankobanReaderPreferencesCodec::
                canonicalPayload(
                    seriesId,
                    raw,
                    &payload,
                    &codecError)) {
            return fail(
                error,
                codecError);
        }

        SyncAdapterRecord record;
        record.recordKey = key;
        record.payload = payload;
        record.localOrderMs = -1;

        snapshot->records.append(
            record);
    }

    return true;
}

bool TankobanReaderPreferencesSyncAdapter::
applyRemote(
    const QString &recordKey,
    SyncWireOperation operation,
    const QJsonValue &payload,
    int schemaVersionValue,
    QString *error) {
    if (!m_owner) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban reader-preference owner is unavailable."));
    }

    if (schemaVersionValue
        != schemaVersion()) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban reader-preference sync schema is unsupported."));
    }

    QString seriesId;
    if (!TankobanReaderPreferencesCodec::
            decodeRecordKey(
                recordKey,
                &seriesId)) {
        return fail(
            error,
            QStringLiteral(
                "The Tankoban reader-preference record key is invalid."));
    }

    const QVariantMap current =
        m_owner->rawRecord(
            seriesId);

    if (operation
        == SyncWireOperation::Delete) {
        const QVariantMap cleared =
            TankobanReaderPreferencesCodec::
                clearSyncedFields(
                    current);

        if (cleared == current)
            return true;

        if (cleared.isEmpty()) {
            return m_owner
                ->removeSyncedRawRecord(
                    seriesId,
                    error);
        }

        return m_owner
            ->applySyncedRawRecord(
                seriesId,
                cleared,
                error);
    }

    if (!payload.isObject()) {
        return fail(
            error,
            QStringLiteral(
                "A Tankoban reader-preference PUT requires an object payload."));
    }

    const QJsonObject object =
        payload.toObject();

    QString codecError;
    if (!TankobanReaderPreferencesCodec::
            validatePayload(
                object,
                seriesId,
                &codecError)) {
        return fail(
            error,
            codecError);
    }

    QVariantMap next;
    if (!TankobanReaderPreferencesCodec::
            overlaySyncedPayload(
                current,
                object,
                &next,
                &codecError)) {
        return fail(
            error,
            codecError);
    }

    if (next == current)
        return true;

    return m_owner
        ->applySyncedRawRecord(
            seriesId,
            next,
            error);
}

bool TankobanReaderPreferencesSyncAdapter::
fail(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
