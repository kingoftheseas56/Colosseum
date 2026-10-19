#pragma once

#include <QCoreApplication>
#include <QByteArray>

namespace Colosseum::Update {

inline QByteArray updateUserAgent()
{
    const QString version = QCoreApplication::applicationVersion().trimmed();
    QByteArray agent = QByteArrayLiteral("Colosseum/")
        + (version.isEmpty() ? QByteArrayLiteral("unknown") : version.toUtf8());
#ifdef COLOSSEUM_NO_ACCOUNT_EDITION
    // Edition suffix so update-feed requests are targetable per edition.
    agent += QByteArrayLiteral(" no-account");
#endif
    return agent;
}

} // namespace Colosseum::Update
