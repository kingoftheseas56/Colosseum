#pragma once

#include <QString>
#include <QVariantMap>

namespace Colosseum::Platform {

inline bool jobRequiresBackgroundHost(const QVariantMap &job) {
    if (job.value(QStringLiteral("id")).toString().trimmed().isEmpty())
        return false;

    const QString state = job.value(QStringLiteral("state"))
        .toString().trimmed().toLower();
    return state != QStringLiteral("done")
        && state != QStringLiteral("complete")
        && state != QStringLiteral("completed")
        && state != QStringLiteral("failed")
        && state != QStringLiteral("cancelled")
        && state != QStringLiteral("canceled")
        && state != QStringLiteral("paused")
        && state != QStringLiteral("ready");
}

} // namespace Colosseum::Platform
