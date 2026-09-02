#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

struct CoreStateSyncProjection {
    enum class Disposition {
        Portable,
        LocalOnly,
        Invalid
    };

    Disposition disposition =
        Disposition::Invalid;
    QString recordKey;
    QJsonObject payload;
    qint64 localOrderMs = -1;
    QString error;

    static CoreStateSyncProjection collection(
        const QVariantMap &entry);

    static CoreStateSyncProjection progress(
        const QVariantMap &entry);

    static CoreStateSyncProjection history(
        const QVariantMap &entry);

    static bool decodeCollectionKey(
        const QString &recordKey,
        QString *world,
        QString *id);

    static bool decodeProgressKey(
        const QString &recordKey,
        QString *kind,
        QString *id);

    static bool decodeHistoryKey(
        const QString &recordKey,
        QString *kind,
        QString *id);

    // Applies a portable remote payload over an existing local owner record
    // while preserving fields/values that the 5A ordinary-sync firewall
    // classifies as machine-local. Safe portable fields missing from the
    // remote payload are removed normally.
    static QVariantMap mergePortableIntoLocal(
        const QVariantMap &existingLocal,
        const QJsonObject &portableRemote);
};
