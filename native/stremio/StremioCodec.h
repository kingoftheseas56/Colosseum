#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

struct StremioLoopbackCallback {
    bool accepted = false;
    QByteArray authKey;
    QString error;
};

struct StremioAccountIdentity {
    QString accountId;
    QString displayName;
};

namespace StremioCodec {

StremioLoopbackCallback decodeLoopbackCallback(
    const QByteArray &request,
    const QString &expectedPath);

bool isProductionEndpoint(const QUrl &endpoint);
bool isTaggedLoopbackEndpoint(const QUrl &endpoint);
QUrl browserLoginUrl(const QUrl &callback);
bool decodeGetUserResult(
    const QJsonObject &response,
    StremioAccountIdentity *identity,
    QString *error = nullptr);

} // namespace StremioCodec
