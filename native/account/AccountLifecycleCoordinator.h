#pragma once

#include "AccountClient.h"
#include "AccountCredentialStore.h"

#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QSaveFile>
#include <QString>
#include <QUrl>

#include <memory>

class AccountController;
class AccountLifecycleCoordinator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
    Q_PROPERTY(bool deletionBusy READ deletionBusy NOTIFY deletionBusyChanged)
    Q_PROPERTY(int exportItemCount READ exportItemCount NOTIFY exportProgressChanged)

public:
    explicit AccountLifecycleCoordinator(
        AccountClient *client,
        AccountCredentialStore *credentialStore,
        AccountController *controller,
        QObject *parent = nullptr);

    bool exportBusy() const;
    bool deletionBusy() const;
    int exportItemCount() const;
    bool hasPendingDeletion() const;

    Q_INVOKABLE void exportAccountData(const QUrl &destination);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void deleteAccount(const QString &currentPassword);
    Q_INVOKABLE void resumePendingDeletion();

signals:
    void exportBusyChanged();
    void deletionBusyChanged();
    void exportProgressChanged();
    void exportSucceeded(const QString &path, int itemCount);
    void exportFailed(const QString &message);
    void deletionSucceeded();
    void deletionFailed(const QString &message, bool retryAvailable);

private:
    void handleCompleted(
        quint64 requestId,
        AccountOperation operation,
        quint64 accessTokenGeneration,
        const AccountTransportReply &reply);
    void requestExportPage(const QString &cursor);
    void failExport(const QString &message);
    void finishExport();
    bool acceptExportPage(const SyncWireExportPage &page, QString *error);
    void sendDeletion(const StoredAccountDeletion &deletion,
                      const QString &password,
                      bool retry);
    void finishDeletion(const AccountTransportReply &reply);
    static bool success(const AccountTransportReply &reply);
    static QByteArray randomCapability();

    AccountClient *m_client = nullptr;
    AccountCredentialStore *m_credentialStore = nullptr;
    AccountController *m_controller = nullptr;

    std::unique_ptr<QSaveFile> m_exportFile;
    QString m_exportPath;
    QString m_exportAccountId;
    QString m_snapshotId;
    QString m_expectedCursor;
    QString m_format;
    int m_schemaVersion = 0;
    quint64 m_highWater = 0;
    quint64 m_exportRequestId = 0;
    quint64 m_exportTokenGeneration = 0;
    int m_exportPages = 0;
    QJsonArray m_exportItems;
    QSet<QString> m_exportKeys;

    StoredAccountDeletion m_deletion;
    quint64 m_deletionRequestId = 0;
    bool m_deletionRetry = false;
};
