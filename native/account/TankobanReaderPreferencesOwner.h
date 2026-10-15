#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class TankobanReaderPreferencesOwner
    : public QObject {
    Q_OBJECT

public:
    explicit TankobanReaderPreferencesOwner(
        QObject *parent = nullptr)
        : QObject(parent) {}

    ~TankobanReaderPreferencesOwner() override =
        default;

    // Monotonic for the lifetime of this owner instance.
    virtual quint64 revision() const = 0;

    // MUST enumerate only logical Tankoban series ids. The shared ComicReader
    // series-record map also serves other lanes, so returning manga/comic ids
    // here would cross an approved ownership boundary.
    virtual QStringList tankobanSeriesIds() const = 0;

    // Returns the raw durable per-series record as owned by ComicReader.
    virtual QVariantMap rawRecord(
        const QString &seriesId) const = 0;

    // Remote imports must commit through the same durable owner used by the
    // reader. They MUST NOT emit localMutationAvailable(). Implementations may
    // emit syncedRecordApplied() after a successful semantic change so an open
    // reader can reload the winning owner state.
    virtual bool applySyncedRawRecord(
        const QString &seriesId,
        const QVariantMap &rawRecord,
        QString *error = nullptr) = 0;

    virtual bool removeSyncedRawRecord(
        const QString &seriesId,
        QString *error = nullptr) = 0;

signals:
    // Emit only for durable local/user-originated changes to approved Tankoban
    // reader preference fields.
    void localMutationAvailable(
        quint64 revision);

    // Remote-only observation seam for a currently open reader. This is not a
    // sync-dirty event and must not loop back into the adapter.
    void syncedRecordApplied(
        const QString &seriesId);
};
