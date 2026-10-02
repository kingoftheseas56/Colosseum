#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

class PorticoDiscoveryService;

struct PorticoFeedRuntimeInputs final
{
    QString spotifyChartsCsv;
    QString appleMusicDeveloperToken;
};

struct PorticoFeedDefinition final
{
    QString feedId;
    QString sourceId;
    QVariantMap config;
};

class PorticoFeedRegistry final
{
public:
    static QList<PorticoFeedDefinition> definitions(
        const PorticoFeedRuntimeInputs &runtimeInputs = {});

    static void configure(
        PorticoDiscoveryService &discovery,
        const PorticoFeedRuntimeInputs &runtimeInputs = {});
};
