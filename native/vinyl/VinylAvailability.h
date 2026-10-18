#pragma once
#include <QtGlobal>
namespace Colosseum {
inline bool vinylLiveEnabled() {
#if defined(COLOSSEUM_ENABLE_VINYL_QUALIFICATION)
    return qEnvironmentVariableIntValue("COLOSSEUM_VINYL_LIVE") == 1;
#else
    return false;
#endif
}
}
