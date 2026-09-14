#pragma once

#include <QCoreApplication>
#include <QByteArray>

namespace Colosseum::Update {

inline QByteArray updateUserAgent()
{
    const QString version = QCoreApplication::applicationVersion().trimmed();
    return QByteArrayLiteral("Colosseum/")
        + (version.isEmpty() ? QByteArrayLiteral("unknown") : version.toUtf8());
}

} // namespace Colosseum::Update
