#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QJsonValue>
#include <QString>

struct SyncPayloadValidation {
    bool allowed = false;
    QString code;
    QString fieldPath;
    QString detail;
};

class SyncPayloadFirewall {
public:
    static SyncPayloadValidation validate(
        const QString &categoryId,
        const QJsonValue &payload);

    static bool isForbiddenFieldName(
        const QString &fieldName);

    static bool isFilesystemPathValue(
        const QString &value);

private:
    static SyncPayloadValidation scan(
        const QJsonValue &value,
        const QString &fieldPath,
        int depth);

    static QString normalizedFieldName(
        const QString &fieldName);

    static SyncPayloadValidation allow();
    static SyncPayloadValidation reject(
        const QString &code,
        const QString &fieldPath,
        const QString &detail);
};
