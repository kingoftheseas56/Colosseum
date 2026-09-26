#pragma once

#include "PorticoAppStateBridge.h"
#include "PorticoDestinationActionRouter.h"
#include "PorticoFeedRegistry.h"
#include "PorticoIdentityStateBridge.h"
#include "PorticoRuntimeFacade.h"
#include "PorticoWebBridge.h"

class QQmlContext;

// Owns one backend through the facade. The other lanes observe or adapt it.
class PorticoComposition final
{
public:
    explicit PorticoComposition(const PorticoFeedRuntimeInputs &inputs = {});

    void exposeTo(QQmlContext *context);
    void refresh() { m_runtime.refresh(); }

    PorticoRuntimeFacade *runtime() { return &m_runtime; }
    PorticoAppStateBridge *appState() { return &m_appState; }
    PorticoDestinationActionRouter *destinationRouter() { return &m_destinationRouter; }
    PorticoIdentityStateBridge *identityState() { return &m_identityState; }

private:
    PorticoRuntimeFacade m_runtime;
    PorticoAppStateBridge m_appState;
    PorticoDestinationActionRouter m_destinationRouter;
    PorticoIdentityStateBridge m_identityState;
    PorticoWebBridge m_webBridge{&m_runtime, &m_destinationRouter};
};
