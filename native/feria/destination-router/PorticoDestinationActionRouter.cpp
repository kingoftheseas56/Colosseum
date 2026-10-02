#include "PorticoDestinationActionRouter.h"

#include <QString>

PorticoDestinationActionRouter::PorticoDestinationActionRouter(QObject *parent)
    : QObject(parent)
{
}

QString PorticoDestinationActionRouter::modeFor(const QVariantMap &door)
{
    const bool appOnly = door.value(QStringLiteral("appOnly")).toBool();
    if (appOnly)
        return QStringLiteral("app-only");

    const QString url = door.value(QStringLiteral("url")).toString();
    const bool actionable = door.value(QStringLiteral("actionable")).toBool();
    if (!actionable || url.isEmpty())
        return QStringLiteral("unavailable");

    if (door.value(QStringLiteral("exact")).toBool())
        return QStringLiteral("exact");

    if (door.value(QStringLiteral("reason")).toString() == QStringLiteral("search"))
        return QStringLiteral("search");

    return QStringLiteral("unavailable");
}

bool PorticoDestinationActionRouter::isLaunchableMode(const QString &mode)
{
    return mode == QStringLiteral("exact") || mode == QStringLiteral("search");
}

QVariantMap PorticoDestinationActionRouter::actionFor(const QVariantMap &door) const
{
    const QString mode = modeFor(door);
    const bool sourceActionable = door.value(QStringLiteral("actionable")).toBool();

    return {
        {QStringLiteral("providerId"), door.value(QStringLiteral("providerId")).toString()},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("url"), door.value(QStringLiteral("url")).toString()},
        {QStringLiteral("exact"), door.value(QStringLiteral("exact")).toBool()},
        {QStringLiteral("actionable"), sourceActionable && isLaunchableMode(mode)},
        {QStringLiteral("appOnly"), door.value(QStringLiteral("appOnly")).toBool()},
    };
}

QVariantList PorticoDestinationActionRouter::actionsFor(const QVariantList &doors) const
{
    QVariantList actions;
    actions.reserve(doors.size());
    for (const QVariant &value : doors)
        actions.push_back(actionFor(value.toMap()));
    return actions;
}
