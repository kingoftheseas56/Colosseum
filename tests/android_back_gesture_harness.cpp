#include "platform/PlatformRuntime.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>
#include <limits>
#include <cstdio>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    using namespace Colosseum::Platform;
    Runtime runtime(nullptr, Kind::Android);
    QWindow window;
    runtime.attachWindow(&window);
    int backs = 0;
    QObject::connect(&runtime, &Runtime::backRequested, [&] { ++backs; });
    auto require = [](bool value, const char *message) {
        if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
    };
    runtime.acceptBackGesture(0, 0, 1);
    runtime.acceptBackGesture(1, 0.6, 1);
    require(runtime.backGestureActive() && runtime.backGestureProgress() == 0.6
            && runtime.backGestureEdge() == 1 && backs == 0, "progress navigated or was lost");
    runtime.acceptBackGesture(2, 0, 1);
    require(!runtime.backGestureActive() && runtime.backGestureProgress() == 0 && backs == 0,
            "cancel must restore preview without navigating");
    runtime.acceptBackGesture(1, 0.9, 0);
    require(!runtime.backGestureActive(), "late progress must not resurrect a cancelled gesture");
    runtime.acceptBackGesture(0, 0, 0);
    runtime.acceptBackGesture(1, 8, 0);
    require(runtime.backGestureProgress() == 1, "invalid progress escaped bounds");
    runtime.acceptBackGesture(1, std::numeric_limits<double>::quiet_NaN(), 0);
    require(runtime.backGestureProgress() == 1, "nonfinite progress must be ignored");
    runtime.acceptBackGesture(3, 1, 0);
    require(backs == 1 && !runtime.backGestureActive(), "commit must dispatch exactly once");
    runtime.acceptBackGesture(3, 1, 0); // API33/button Back can arrive without animation events.
    require(backs == 2, "nonanimated platform Back was lost");
    QKeyEvent legacy(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &legacy);
    require(backs == 3 && legacy.isAccepted(), "legacy Back path regressed");
    Runtime desktop(nullptr, Kind::WindowsDesktop);
    desktop.acceptBackGesture(0, 0, 0);
    require(!desktop.backGestureActive(), "Android gesture leaked into desktop");
    std::puts("ANDROID_BACK_GESTURE_PASS progress cancel commit legacy desktop");
}
