#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class PorticoDestinationActionRouter final : public QObject
{
    Q_OBJECT

public:
    explicit PorticoDestinationActionRouter(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap actionFor(const QVariantMap &door) const;
    Q_INVOKABLE QVariantList actionsFor(const QVariantList &doors) const;

private:
    static QString modeFor(const QVariantMap &door);
    static bool isLaunchableMode(const QString &mode);
};
