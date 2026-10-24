#pragma once
#include <QtGlobal>
namespace Colosseum {
inline bool vinylLiveEnabled() {
#if defined(Q_OS_ANDROID)
    return false;
#elif defined(COLOSSEUM_ENABLE_VINYL_QUALIFICATION)
    return qEnvironmentVariableIntValue("COLOSSEUM_VINYL_LIVE") == 1;
#else
    // Local music uses the ordinary desktop library, identity and mpv owners.
    // Production entry must not depend on a developer environment variable.
    return true;
#endif
}
}
