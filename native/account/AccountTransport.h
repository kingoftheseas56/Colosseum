#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>

struct AccountTransportRequest {
    QByteArray method;
    QString path;
    QJsonObject body;
    QByteArray bearerToken;
};

struct AccountTransportReply {
    int statusCode = 0;
    QJsonObject body;
    QString errorCode;
    QString errorMessage;
    bool networkError = false;
};

Q_DECLARE_METATYPE(AccountTransportRequest)
Q_DECLARE_METATYPE(AccountTransportReply)

class AccountTransport : public QObject {
    Q_OBJECT

public:
    explicit AccountTransport(QObject *parent = nullptr)
        : QObject(parent) {}

    ~AccountTransport() override = default;

    virtual void send(quint64 requestId, const AccountTransportRequest &request) = 0;

signals:
    void finished(quint64 requestId, const AccountTransportReply &reply);
};
