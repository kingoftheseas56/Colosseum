#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QObject>
#include <QThread>

#include <optional>

struct StremioPendingIntent {
    QString operationId;
    QString kind;
    QJsonObject desired;
    bool remoteAcknowledged = false;
    bool localReceiptDurable = false;
    int attempts = 0;
    qint64 retryAtMs = 0;
};

struct StremioPersistentState {
    QString profileId;
    quint64 bindingGeneration = 0;
    QString accountId;
    QString displayName;
    QJsonObject acknowledgedBaselines;
    QJsonArray importRedoReceipts;
    QJsonArray intentionalMembershipDifferences;
    QList<StremioPendingIntent> pendingIntents;
    qint64 lastSuccessAtMs = 0;
    bool firstMergeComplete = false;
    bool reconnectRequired = false;
};

class StremioState final : public QObject {
    Q_OBJECT

public:
    explicit StremioState(QObject *parent = nullptr);
    ~StremioState() override;

    std::optional<StremioPersistentState> load(
        const QString &path,
        QString *error = nullptr) const;
    quint64 saveAsync(
        const QString &path,
        const StremioPersistentState &state);
    bool flush(QString *error = nullptr);

    static QJsonObject encode(const StremioPersistentState &state);
    static std::optional<StremioPersistentState> decode(
        const QJsonObject &object,
        QString *error = nullptr);

signals:
    void persistenceCommitted(quint64 generation);
    void persistenceFailed(quint64 generation, const QString &message);

private:
    QThread m_writerThread;
    QObject *m_writerObject = nullptr;
    mutable QMutex m_writerErrorMutex;
    QString m_lastWriterError;
    quint64 m_nextGeneration = 1;
};
