#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountTransport.h"

#include <QNetworkAccessManager>
#include <QUrl>

class QNetworkReply;

class AccountHttpTransport final : public AccountTransport {
    Q_OBJECT

public:
    explicit AccountHttpTransport(
        const QUrl &baseUrl,
        QObject *parent = nullptr);

    void send(
        quint64 requestId,
        const AccountTransportRequest &request) override;

    QUrl baseUrl() const;
    bool isConfigurationValid() const;

    static bool isAllowedBaseUrl(const QUrl &url);

private:
    AccountTransportReply decodeReply(
        QNetworkReply *reply,
        const QByteArray &payload) const;
    void emitConfigurationError(quint64 requestId);

    QUrl m_baseUrl;
    QNetworkAccessManager m_network;
};
