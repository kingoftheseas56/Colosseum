#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class PorticoRuntimeFacade;
class PorticoDestinationActionRouter;

// Narrow bridge for the bundled Feria page. Do not register the runtime itself:
// WebChannel exposes every invokable on a registered object.
class PorticoWebBridge final : public QObject
{
    Q_OBJECT

public:
    PorticoWebBridge(PorticoRuntimeFacade *runtime,
                     PorticoDestinationActionRouter *destinations,
                     QObject *parent = nullptr);

    Q_INVOKABLE QVariantList shelves() const;
    Q_INVOKABLE QVariantMap title(const QString &canonicalKey) const;
    Q_INVOKABLE QVariantList search(const QString &query) const;
    Q_INVOKABLE QVariantList destinations(const QString &canonicalKey) const;
    Q_INVOKABLE bool openDestination(const QString &canonicalKey,
                                    const QString &providerId) const;
    Q_INVOKABLE void setLens(const QString &lens);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void close();

signals:
    void changed();
    void closeRequested();

private:
    PorticoRuntimeFacade *m_runtime;
    PorticoDestinationActionRouter *m_destinations;
};
