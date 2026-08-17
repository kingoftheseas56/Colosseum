#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QList>
#include <QString>
#include <QStringList>

enum class SyncDisposition {
    Syncable,
    Secret,
    LocalOnly
};

enum class SyncOwnerStatus {
    Confirmed,
    Partial,
    Absent
};

struct SyncOwnershipEntry {
    QString id;
    SyncDisposition disposition = SyncDisposition::LocalOnly;
    SyncOwnerStatus ownerStatus = SyncOwnerStatus::Absent;
    bool ordinaryPayloadEligible = false;
    QStringList approvedFields;
    QString liveOwner;
    QString cumulativeReferenceOwner;
    QString readSeam;
    QString writeSeam;
    QString changeSeam;
    int futureSlice = 0;
    QString denialCode;
    QString note;
};

class SyncOwnershipInventory {
public:
    static const QList<SyncOwnershipEntry> &all();
    static const SyncOwnershipEntry *find(const QString &id);

    static QString dispositionName(
        SyncDisposition disposition);
    static QString ownerStatusName(
        SyncOwnerStatus status);

    static QString inspectionBaseCommit();
};
