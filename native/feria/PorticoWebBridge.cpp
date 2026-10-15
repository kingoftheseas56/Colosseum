#include "PorticoWebBridge.h"

#include "PorticoDestinationActionRouter.h"
#include "PorticoRuntimeFacade.h"

#include <QDesktopServices>
#include <QUrl>

PorticoWebBridge::PorticoWebBridge(PorticoRuntimeFacade *runtime,
                                   PorticoDestinationActionRouter *destinations,
                                   QObject *parent)
    : QObject(parent), m_runtime(runtime), m_destinations(destinations)
{
    connect(m_runtime, &PorticoRuntimeFacade::revisionChanged,
            this, &PorticoWebBridge::changed);
}

QVariantList PorticoWebBridge::shelves() const
{
    return m_runtime->shelves();
}

QVariantMap PorticoWebBridge::title(const QString &canonicalKey) const
{
    return m_runtime->title(canonicalKey);
}

QVariantList PorticoWebBridge::search(const QString &query) const
{
    QVariantList results;
    for (const QString &key : m_runtime->search(query, 40))
        results.append(m_runtime->title(key));
    return results;
}

QVariantList PorticoWebBridge::destinations(const QString &canonicalKey) const
{
    const QVariantMap item = m_runtime->title(canonicalKey);
    if (item.isEmpty())
        return {};
    const QVariantList raw = m_runtime->destinationsFor(item);
    QVariantList result;
    for (const QVariant &value : raw) {
        QVariantMap door = value.toMap();
        const QVariantMap action = m_destinations->actionFor(door);
        door.insert(QStringLiteral("actionable"), action.value(QStringLiteral("actionable")));
        door.insert(QStringLiteral("mode"), action.value(QStringLiteral("mode")));
        result.append(door);
    }
    return result;
}

bool PorticoWebBridge::openDestination(const QString &canonicalKey,
                                       const QString &providerId) const
{
    if (canonicalKey.isEmpty() || providerId.isEmpty())
        return false;
    for (const QVariant &value : destinations(canonicalKey)) {
        const QVariantMap door = value.toMap();
        if (door.value(QStringLiteral("providerId")).toString() != providerId
                || !door.value(QStringLiteral("actionable")).toBool())
            continue;
        const QUrl url(door.value(QStringLiteral("url")).toString());
        if (url.scheme() != QStringLiteral("https") || url.host().isEmpty())
            return false;
        return QDesktopServices::openUrl(url);
    }
    return false;
}

void PorticoWebBridge::setLens(const QString &lens)
{
    if (lens == QStringLiteral("all") || lens == QStringLiteral("watch")
            || lens == QStringLiteral("listen") || lens == QStringLiteral("read"))
        m_runtime->setLens(lens);
}

void PorticoWebBridge::refresh()
{
    m_runtime->refresh();
}

void PorticoWebBridge::close()
{
    emit closeRequested();
}
