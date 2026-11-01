#pragma once

#include <QtGlobal>

namespace Colosseum {
inline bool feriaLiveEnabled()
{
#if defined(COLOSSEUM_FERIA_IN_APP) && !COLOSSEUM_FERIA_IN_APP
    return false;
#elif defined(Q_OS_ANDROID)
    return false;
#else
    return true;
#endif
}
}
