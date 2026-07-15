#pragma once

#include <QList>
#include <QRect>
#include <QSize>

namespace WindowStatePolicy {
QSize defaultSize();
QSize minimumSize();
QRect centeredDefault(const QRect &available);
bool isMeaningfullyVisible(const QRect &geometry,
                           const QList<QRect> &availableScreens);
QRect validatedNormalGeometry(const QRect &saved,
                              const QList<QRect> &availableScreens,
                              const QRect &primaryAvailable);
}
