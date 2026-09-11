#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <optional>

enum class SyncWireOperation {
    Put,
    Delete
};

struct SyncWireHlc {
    qint64 physicalMs = 0;
    quint64 counter = 0;
    QString deviceId;
};

struct SyncWireMutation {
    QString mutationId;
    QString deviceId;
    QString category;
    QString recordKey;
    int schemaVersion = 0;
    SyncWireHlc hlc;
    // Pull entries may carry canonical materialization ordering separately
    // from the original request HLC/device identity.
    std::optional<SyncWireHlc> materializedHlc;
    SyncWireOperation operation = SyncWireOperation::Put;
    QJsonValue payload;
};

struct SyncWireAttachmentManifestItem {
    SyncWireMutation mutation;
    QByteArray canonicalPayloadHash;
};

struct SyncWireCurrentMetadata {
    QString mutationId;
    QString deviceId;
    int schemaVersion = 0;
    SyncWireHlc hlc;
    SyncWireOperation operation = SyncWireOperation::Put;
    quint64 serverSeq = 0;
};

struct SyncWirePushResult {
    QString mutationId;
    bool accepted = false;
    quint64 serverSeq = 0;
    bool won = false;
    QString code;
    QString message;
    std::optional<SyncWireCurrentMetadata> current;
};

struct SyncWirePullEntry {
    quint64 serverSeq = 0;
    bool won = false;
    bool canonical = false;
    SyncWireMutation mutation;
};

struct SyncWirePushResponse {
    qint64 serverTimeMs = 0;
    QList<SyncWirePushResult> results;
};

struct SyncWirePullResponse {
    qint64 serverTimeMs = 0;
    QList<SyncWirePullEntry> entries;
    bool hasMore = false;
};

enum class SyncWireAttachmentState {
    Open,
    Uploaded,
    Committed,
    Aborted
};

struct SyncWireAttachmentDisposition {
    QString mutationId;
    QString category;
    QString recordKey;
    SyncWireOperation operation = SyncWireOperation::Put;
    // Server-certified outcome for the exact manifest identity.  A
    // materialized item is present in canonical export with the source
    // contribution; a superseded item is represented by a newer canonical
    // winner and may differ from the source payload.
    QString disposition;
    QByteArray materializedPayloadHash;
};

struct SyncWireAttachmentResponse {
    QString attachmentId;
    QString deviceId;
    quint64 baselineServerSeq = 0;
    SyncWireAttachmentState state = SyncWireAttachmentState::Open;
    QString freshExportSnapshotId;
    QString freshExportCursor;
    quint64 freshExportHighWaterSeq = 0;
    QList<SyncWireAttachmentDisposition> dispositions;
};

struct SyncWireExportPage {
    QString format;
    int schemaVersion = 0;
    QString snapshotId;
    QString cursor;
    QString nextCursor;
    quint64 highWaterServerSeq = 0;
    QJsonArray items;
    bool hasMore = false;
};

struct SyncWireSnapshotResponse {
    qint64 serverTimeMs = 0;
    quint64 cursor = 0;
    QList<SyncWirePullEntry> entries;
    QString nextPageToken;
    bool hasMore = false;
};

int compareSyncWireHlc(
    const SyncWireHlc &left,
    const SyncWireHlc &right);

bool syncWireHlcGreater(
    const SyncWireHlc &left,
    const SyncWireHlc &right);

QString syncWireOperationName(
    SyncWireOperation operation);

std::optional<SyncWireOperation>
syncWireOperationFromName(
    const QString &name);

bool isValidSyncWireRecordKey(
    const QString &recordKey);

// Returns the exact compact UTF-8 request body used by AccountHttpTransport
// for a sync push envelope. SyncEngine uses this seam to enforce the server's
// 64 KiB decoder bound before dispatching a batch.
QByteArray syncWirePushRequestBytes(
    const QJsonArray &mutations);

QJsonObject syncWireMutationToJson(
    const SyncWireMutation &mutation);

QByteArray syncWireCanonicalPayloadHash(
    const SyncWireMutation &mutation);

QJsonObject syncWireAttachmentManifestItemToJson(
    const SyncWireAttachmentManifestItem &item);

std::optional<SyncWireAttachmentManifestItem>
syncWireAttachmentManifestItemFromJson(
    const QJsonObject &object);

std::optional<SyncWireMutation>
syncWireMutationFromJson(
    const QJsonObject &object);

std::optional<SyncWirePushResult>
syncWirePushResultFromJson(
    const QJsonObject &object);

std::optional<SyncWirePullEntry>
syncWirePullEntryFromJson(
    const QJsonObject &object);

std::optional<SyncWirePushResponse>
syncWirePushResponseFromJson(
    const QJsonObject &object);

std::optional<SyncWirePullResponse>
syncWirePullResponseFromJson(
    const QJsonObject &object);

QString syncWireAttachmentStateName(
    SyncWireAttachmentState state);

std::optional<SyncWireAttachmentState>
syncWireAttachmentStateFromName(
    const QString &name);

std::optional<SyncWireAttachmentResponse>
syncWireAttachmentResponseFromJson(
    const QJsonObject &object);

std::optional<SyncWireExportPage>
syncWireExportPageFromJson(
    const QJsonObject &object);

std::optional<SyncWireSnapshotResponse>
syncWireSnapshotResponseFromJson(
    const QJsonObject &object);
