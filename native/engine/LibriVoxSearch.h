#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;

class LibriVoxSearch : public QObject
{
    Q_OBJECT
public:
    explicit LibriVoxSearch(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~LibriVoxSearch() override;
    void setEnabledCheck(std::function<bool()> check) { m_enabledCheck = std::move(check); }

    Q_INVOKABLE void search(const QString& token, const QString& title, const QString& author);
    Q_INVOKABLE void cancel(const QString& token);

signals:
    void searchFinished(const QString& token, const QVariantList& rows, const QString& error);

private:
    void schedule();
    void request();
    void finishRequest();
    void complete(const QVariantList& rows, const QString& error);

    QPointer<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_reply;
    std::function<bool()> m_enabledCheck;
    QElapsedTimer m_sinceRequest;
    QTimer m_timer;
    QString m_token, m_title, m_author;
    QByteArray m_body;
    int m_attempts = 0;
};
