#pragma once
#include <QtGlobal>
namespace Colosseum {
inline bool vinylLiveEnabled() {
#if defined(COLOSSEUM_VINYL_IN_APP) && !COLOSSEUM_VINYL_IN_APP
    // A shipping build can keep all Vinyl code and local-media owners while
    // making the world unavailable, regardless of developer opt-in variables.
    return false;
#elif defined(Q_OS_ANDROID)
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
