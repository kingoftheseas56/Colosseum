#pragma once

#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariant>

class FeedbackRequestService final : public QObject
{
    Q_OBJECT
public:
    explicit FeedbackRequestService(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);

    Q_INVOKABLE QString newReference() const;
    Q_INVOKABLE void setInlineKeyboard(bool enabled);
    Q_INVOKABLE void submit(const QString &kind, const QVariantMap &fields);
    Q_INVOKABLE void searchUniverses(const QString &query);

signals:
    void submissionFinished(bool ok, const QString &message);
    void universeResults(const QString &query, const QVariantList &rows);

private:
    QNetworkAccessManager *const m_network;
    QPointer<QNetworkReply> m_search;
    bool m_sending = false;
#if defined(Q_OS_ANDROID)
    bool m_ownsKeyboardPolicy = false;
#endif

    void finish(bool ok, const QString &message);
};
