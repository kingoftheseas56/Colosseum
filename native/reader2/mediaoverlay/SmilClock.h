#pragma once
#include <QString>
#include <optional>
namespace MediaOverlay {
struct SmilClock {
    // Non-negative milliseconds; invalid and overflow are nullopt, distinct from zero.
    static std::optional<qint64> parse(const QString &clock);
};
}
