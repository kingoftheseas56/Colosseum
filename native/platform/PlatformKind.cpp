#include "PlatformKind.h"

namespace Colosseum::Platform {

const char *kindName(Kind kind) {
    switch (kind) {
    case Kind::WindowsDesktop:
        return "windows";
    case Kind::LinuxDesktop:
        return "linux";
    case Kind::Android:
        return "android";
    case Kind::Other:
        return "other";
    }
    return "other";
}

} // namespace Colosseum::Platform
