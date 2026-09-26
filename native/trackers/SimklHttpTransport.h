#pragma once

#include "SimklAuth.h"

#include <QNetworkAccessManager>
#include <QObject>

class QNetworkReply;

// Production AUTH V2 transport for SIMKL. Provider payloads and tokens stay
// native; callers receive only the narrow typed results declared by SimklAuth.
class SimklHttpTransport final : public QObject, public SimklAuthTransport
{
    Q_OBJECT

public:
    explicit SimklHttpTransport(QObject *parent = nullptr);

    void exchangeAuthorizationCode(const SimklAuthorizationCodeRequest &request,
                                   SimklTokenCompletion completion) override;
    void requestDevicePin(const SimklDevicePinRequest &request,
                          SimklDevicePinCompletion completion) override;
    void pollDevicePin(const SimklDevicePinPollRequest &request,
                       SimklTokenCompletion completion) override;
    void fetchStableAccountId(const SimklIdentityRequest &request,
                              SimklIdentityCompletion completion) override;

private:
    using HttpCompletion = std::function<void(int, const QByteArray &, qint64,
                                               bool, bool)>;

    void postForm(const QUrl &url,
                  const QList<QPair<QString, QString>> &fields,
                  const QString &userAgent,
                  HttpCompletion completion);
    void getJson(const QUrl &url,
                 const QByteArray &accessToken,
                 const QString &userAgent,
                 HttpCompletion completion);
    void finishReply(QNetworkReply *reply,
                     const std::shared_ptr<QByteArray> &buffer,
                     const std::shared_ptr<bool> &tooLarge,
                     HttpCompletion completion);

    QNetworkAccessManager m_network;
};
