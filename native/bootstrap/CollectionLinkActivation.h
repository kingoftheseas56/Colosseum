#pragma once
#include <QObject>
#include <QUrl>
#include <QPointer>
#include <memory>
class QThread;
struct CollectionActivationMailbox;
class ColosseumInstanceLock;

// An ID-only activation channel subordinate to the existing instance lock.
class CollectionLinkActivation final : public QObject {
    Q_OBJECT
public:
    explicit CollectionLinkActivation(std::shared_ptr<ColosseumInstanceLock> lock, const QUrl &legacyOrigin, QObject *parent = nullptr);
    ~CollectionLinkActivation() override;
    bool startPrimary(const QString &appDataRoot);
    bool forwardToPrimary(const QUrl &url, int timeoutMs = 1500);
    Q_INVOKABLE bool submit(const QUrl &url);
    QString channelName() const;
    Q_INVOKABLE void setDestinationReady(bool ready);
    Q_INVOKABLE void clearPending();
    Q_INVOKABLE bool dismissPresented(const QString &ticket);
    Q_INVOKABLE bool deferPresented(const QString &ticket);
    void deferForProfile();
    void registerPlatformHandlers();
    bool shutdown(int timeoutMs = 1500);
signals:
    void openRequested(const QUrl &canonicalUrl, const QString &ticket);
public slots:
    void handleUrl(const QUrl &url);
private slots:
    void drain();
private:
    const std::shared_ptr<ColosseumInstanceLock> m_lock;
    const QString m_root;
    const QUrl m_legacyOrigin;
    const std::shared_ptr<CollectionActivationMailbox> m_mailbox;
    QPointer<QThread> m_listener;
    quint64 m_ticketSequence = 0;
    QString m_presentedTicket;
    bool m_ready = false;
    bool m_handlers = false;
};
