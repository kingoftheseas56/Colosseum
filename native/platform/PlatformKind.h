#pragma once

#include <QtGlobal>

namespace Colosseum::Platform {

enum class Kind {
    WindowsDesktop,
    LinuxDesktop,
    Android,
    Other,
};

struct Capabilities {
    bool secureCredentialStore = false;
    bool systemBack = false;
    bool safeAreaInsets = false;
    bool softwareKeyboard = false;
    bool storageAccessFramework = false;
    bool backgroundDownloadNotifications = false;
    bool playbackScreenInhibit = false;
    bool desktopUpdater = false;
    bool desktopWindowChrome = false;
};

constexpr Kind selectKind(bool android, bool windows, bool isLinux) {
    if (android)
        return Kind::Android;
    if (windows)
        return Kind::WindowsDesktop;
    if (isLinux)
        return Kind::LinuxDesktop;
    return Kind::Other;
}

constexpr Kind currentKind() {
#if defined(Q_OS_ANDROID)
    return Kind::Android;
#elif defined(Q_OS_WIN)
    return Kind::WindowsDesktop;
#elif defined(Q_OS_LINUX)
    return Kind::LinuxDesktop;
#else
    return Kind::Other;
#endif
}

constexpr Capabilities capabilitiesFor(Kind kind) {
    switch (kind) {
    case Kind::Android:
        return {true, true, true, true, true, true, true, false, false};
    case Kind::WindowsDesktop:
        return {true, false, true, false, false, false, true, true, true};
    case Kind::LinuxDesktop:
        return {false, false, true, false, false, false, true, false, true};
    case Kind::Other:
        return {};
    }
    return {};
}

const char *kindName(Kind kind);

} // namespace Colosseum::Platform
