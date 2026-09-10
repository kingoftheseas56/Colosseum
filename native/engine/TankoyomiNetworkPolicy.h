#pragma once

#include <QHostAddress>
#include <QStringList>
#include <QUrl>

namespace TankoyomiNetworkPolicy {
bool metadataHostAllowed(const QString &host, const QStringList &allowedHosts);
bool pageUrlAllowedBeforeDns(const QUrl &url, const QString &policy);
bool resolvedAddressAllowed(const QHostAddress &address, const QString &policy);
bool redirectAllowed(const QUrl &from, const QUrl &target,
                     const QStringList &allowedHosts);
}
