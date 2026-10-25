#pragma once

#include <QtQml/qqml.h>

namespace Colosseum::Bootstrap {

// Call after the shell has imported QtQuick, before a lazy player can import it.
// A strong module lock skips further qmldir/plugin discovery for versioned imports.
// The implicit plugin lock is weaker: a later resolution can re-enter registration
// and fail with "Cannot install element ... into protected module 'QtQuick'".
inline bool sealQtQuickModule()
{
    return qmlProtectModule("QtQuick", 6);
}

} // namespace Colosseum::Bootstrap
