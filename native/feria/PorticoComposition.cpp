#include "PorticoComposition.h"

#include <QQmlContext>

PorticoComposition::PorticoComposition(const PorticoFeedRuntimeInputs &inputs)
{
    for (const auto &feed : PorticoFeedRegistry::definitions(inputs))
        m_runtime.configureFeed(feed.feedId, feed.sourceId, feed.config);
    m_appState.setRuntime(&m_runtime);
    m_appState.setSettingsApps({
        QStringLiteral("netflix"), QStringLiteral("prime"),
        QStringLiteral("hbomax"), QStringLiteral("disney"),
        QStringLiteral("appletv"), QStringLiteral("crunchyroll"),
        QStringLiteral("youtube"), QStringLiteral("spotify"),
        QStringLiteral("ytmusic"), QStringLiteral("kindle"),
        QStringLiteral("mangaplus"), QStringLiteral("webtoon"),
        QStringLiteral("dcui")
    });
}

void PorticoComposition::exposeTo(QQmlContext *context)
{
    context->setContextProperty(QStringLiteral("porticoRuntime"), &m_runtime);
    context->setContextProperty(QStringLiteral("porticoAppState"), &m_appState);
    context->setContextProperty(QStringLiteral("porticoDestinationRouter"), &m_destinationRouter);
    context->setContextProperty(QStringLiteral("porticoIdentityState"), &m_identityState);

    // The current discovery variant expects these names. Both refer to the
    // facade's single owned backend and store, not another runtime instance.
    context->setContextProperty(QStringLiteral("porticoDiscovery"), &m_runtime);
    context->setContextProperty(QStringLiteral("porticoContent"), m_runtime.contentStore());
    context->setContextProperty(QStringLiteral("porticoWebBridge"), &m_webBridge);
}
