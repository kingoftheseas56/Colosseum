#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class RatingsReviewsStore final : public QObject
{
    Q_OBJECT

public:
    struct Identity {
        QString world;
        QString kind;
        QString mediaId;
    };

    struct Record {
        Identity identity;
        std::optional<double> rating;
        std::optional<QString> review;
        bool spoiler = false;
        qint64 createdAtMs = 0;
        qint64 updatedAtMs = 0;
    };
    struct Tombstone {
        qint64 deletedAtMs = 0;
    };

    struct CommitResult {
        bool changed = false;
        quint64 revision = 0;
        QString recordKey;
    };

    using Clock = std::function<qint64()>;

    explicit RatingsReviewsStore(
        const QString &path,
        Clock clock = {},
        QObject *parent = nullptr);

    bool healthy(QString *error = nullptr) const;
    QString persistenceError() const;
    QString storagePath() const;
    quint64 revision() const;

    static QString recordKeyForIdentity(
        const Identity &identity,
        QString *error = nullptr);
    std::optional<Record> record(const Identity &identity) const;
    std::optional<Record> recordByKey(const QString &recordKey) const;
    QJsonObject recordsJson() const;
    QJsonObject tombstonesJson() const;

    bool saveLocal(
        const Identity &identity,
        const std::optional<double> &rating,
        const std::optional<QString> &review,
        bool spoiler,
        CommitResult *result = nullptr,
        QString *error = nullptr);

    bool setRating(
        const Identity &identity,
        double rating,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool clearRating(
        const Identity &identity,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool setReview(
        const Identity &identity,
        const QString &review,
        bool spoiler,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool deleteReview(
        const Identity &identity,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool setSpoiler(
        const Identity &identity,
        bool spoiler,
        CommitResult *result = nullptr,
        QString *error = nullptr);

    bool applySyncedPut(
        const QString &recordKey,
        const Record &record,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool applySyncedDelete(
        const QString &recordKey,
        qint64 deletedAtMs,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool restoreCanonicalState(
        const QJsonObject &records,
        const QJsonObject &tombstones,
        CommitResult *result = nullptr,
        QString *error = nullptr);
    bool clearForProfileRetirement(QString *error = nullptr);
    bool flush(QString *error = nullptr) const;

    static bool mergeCanonicalState(
        const QJsonObject &accountRecords,
        const QJsonObject &accountTombstones,
        const QJsonObject &localRecords,
        const QJsonObject &localTombstones,
        QJsonObject *mergedRecords,
        QJsonObject *mergedTombstones,
        QString *error = nullptr);

signals:
    void changed();
    void syncDirty(quint64 revision);
    void remoteApplied(const QString &recordKey, quint64 revision);

private:
    enum class MutationOrigin {
        Local,
        Remote,
        Restore,
        Retirement,
    };

    bool load();
    bool persistCandidate(
        const QJsonObject &records,
        const QJsonObject &tombstones,
        quint64 revision,
        QString *error);
    bool commitCandidate(
        const QJsonObject &records,
        const QJsonObject &tombstones,
        const QString &recordKey,
        MutationOrigin origin,
        CommitResult *result,
        QString *error);

    bool requireHealthy(QString *error) const;
    qint64 now(QString *error) const;

    QString m_path;
    Clock m_clock;
    QJsonObject m_records;
    QJsonObject m_tombstones;
    quint64 m_revision = 0;
    bool m_healthy = true;
    QString m_persistenceError;
};
