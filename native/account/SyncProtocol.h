#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

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
    SyncWireOperation operation = SyncWireOperation::Put;
    QJsonValue payload;
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

QJsonObject syncWireMutationToJson(
    const SyncWireMutation &mutation);

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
