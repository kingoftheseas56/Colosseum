#include "FeriaBrowserPolicy.h"
#ifdef FERIA_HAS_WEBVIEW2
#include "provider-host/FeriaHostItem.h"
#include <qqml.h>
#endif

bool FeriaBrowserPolicy::webView2Available() const
{
#ifdef FERIA_HAS_WEBVIEW2
    return true;
#else
    return false;
#endif
}

void FeriaBrowserPolicy::registerTypes()
{
#ifdef FERIA_HAS_WEBVIEW2
    qmlRegisterType<FeriaHostItem>("Colosseum.FeriaHost", 1, 0, "FeriaHostItem");
#endif
}
