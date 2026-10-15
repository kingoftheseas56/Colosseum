#include "windowstatepolicy.h"

#include <algorithm>

namespace {
constexpr int kMeaningfulPixels = 96;

QRect centeredSized(const QRect &available, const QSize &wanted) {
    const QSize bounded(std::min(wanted.width(), available.width()),
                        std::min(wanted.height(), available.height()));
    return QRect(available.x() + (available.width() - bounded.width()) / 2,
                 available.y() + (available.height() - bounded.height()) / 2,
                 bounded.width(), bounded.height());
}
}

namespace WindowStatePolicy {
QSize defaultSize() { return QSize(1280, 720); }
QSize minimumSize() { return QSize(1024, 640); }

QRect centeredDefault(const QRect &available) {
    const QRect safe = available.isValid() ? available : QRect(0, 0, 1920, 1040);
    return centeredSized(safe, defaultSize());
}

QRect fullscreenGeometry(const QRect &screenGeometry, const QRect &fallback) {
    if (screenGeometry.isValid())
        return screenGeometry;
    return fallback.isValid() ? fallback : QRect(0, 0, 1920, 1080);
}

bool isMeaningfullyVisible(const QRect &geometry,
                           const QList<QRect> &availableScreens) {
    for (const QRect &screen : availableScreens) {
        const QRect overlap = geometry.intersected(screen);
        if (overlap.width() >= kMeaningfulPixels
            && overlap.height() >= kMeaningfulPixels)
            return true;
    }
    return false;
}

QRect validatedNormalGeometry(const QRect &saved,
                              const QList<QRect> &availableScreens,
                              const QRect &primaryAvailable) {
    if (!saved.isValid()
        || saved.width() < minimumSize().width()
        || saved.height() < minimumSize().height()
        || !isMeaningfullyVisible(saved, availableScreens))
        return centeredDefault(primaryAvailable);

    QRect targetScreen;
    int largestOverlap = -1;
    for (const QRect &screen : availableScreens) {
        const QRect overlap = saved.intersected(screen);
        const int area = overlap.width() * overlap.height();
        if (area > largestOverlap) {
            largestOverlap = area;
            targetScreen = screen;
        }
    }
    if (!targetScreen.isValid())
        targetScreen = primaryAvailable;

    const QSize bounded(std::min(saved.width(), targetScreen.width()),
                        std::min(saved.height(), targetScreen.height()));
    const int x = std::clamp(saved.x(), targetScreen.left(),
                             targetScreen.right() - bounded.width() + 1);
    const int y = std::clamp(saved.y(), targetScreen.top(),
                             targetScreen.bottom() - bounded.height() + 1);
    return QRect(x, y, bounded.width(), bounded.height());
}
}
