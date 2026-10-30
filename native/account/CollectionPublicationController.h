#pragma once
#include "AccountClient.h"
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
class NamedCollectionsStore;

// The network boundary for explicitly shared snapshots, not a second collection
// or account owner. No session material crosses its QML interface.
class CollectionPublicationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool retryResetAvailable READ retryResetAvailable NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList publications READ publications NOTIFY changed)
    Q_PROPERTY(QVariantMap received READ received NOTIFY changed)
public:
    explicit CollectionPublicationController(AccountClient *client, const QUrl &serviceUrl, QObject *parent = nullptr);
    void activate(NamedCollectionsStore *store, bool signedIn);
    void deactivate();
    bool busy() const { return m_request != 0; }
    bool retryResetAvailable() const { return !busy() && !m_retryOperationKey.isEmpty(); }
    bool signedIn() const { return m_signedIn; }
    QString error() const { return m_error; }
    QString message() const { return m_message; }
    QVariantList publications() const { return m_publications; }
    QVariantMap received() const { return m_received; }
    Q_INVOKABLE bool publish(const QString &collectionId, bool gift, const QString &passphrase = {});
    Q_INVOKABLE bool publishChanges(const QString &shareId, int expectedRevision, const QString &collectionId, const QString &passphrase = {});
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool startNewPublicationRequest();
    Q_INVOKABLE bool revoke(const QString &shareId, int expectedRevision);
    Q_INVOKABLE bool openLink(const QString &url, const QString &passphrase = {});
    Q_INVOKABLE bool openEditionFile(const QUrl &url);
    Q_INVOKABLE bool keepReceivedGift();
    Q_INVOKABLE bool makeOwnVersion(const QString &keptId, const QString &name);
    Q_INVOKABLE bool savePresentation(const QString &id, const QVariantMap &presentation, int expectedRevision);
    Q_INVOKABLE QString loadCover(const QUrl &localUrl);
    Q_INVOKABLE QString linkFor(const QString &shareId) const;
    Q_INVOKABLE bool copyLink(const QString &shareId);
    Q_INVOKABLE void clearReceived();
signals:
    void changed();
    void kept(const QString &collectionId);
    void copied(const QString &collectionId);
private:
    bool ready(bool accountRequired);
    bool fail(const QString &message);
    QString operationId(const QString &action, const QJsonObject &body);
    bool finishOperation();
    QString sourceFor(const QString &shareId) const;
    bool saveSource(const QString &shareId, const QString &collectionId);
    bool acceptReceived(const QJsonObject &body);
    void complete(quint64 id, AccountOperation operation, quint64 tokenGeneration, const AccountTransportReply &reply);
    static bool validShareId(const QString &id);
    QPointer<AccountClient> m_client;
    QPointer<NamedCollectionsStore> m_store;
    QUrl m_serviceUrl;
    quint64 m_request = 0;
    quint64 m_generation = 0;
    quint64 m_requestGeneration = 0;
    bool m_signedIn = false;
    QString m_error, m_message;
    QString m_requestCollectionId;
    QString m_requestShareId;
    QString m_operationKey;
    QString m_retryOperationKey;
    QVariantList m_publications;
    QVariantMap m_received;
};
