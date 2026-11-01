#pragma once
#include <QJsonValue>
#include <QStringList>
#include <QtGlobal>
#include <cmath>
#include <optional>

namespace vinyl::boundary {
inline constexpr qsizetype MaxSnapshotBytes = 8 * 1024 * 1024;
inline constexpr qsizetype MaxQueueItems = 10000;
inline constexpr qsizetype MaxRows = 100000;
inline constexpr qsizetype MaxDisplayCharacters = 4096;
inline constexpr qsizetype MaxArtistCredits = 256;

inline std::optional<qint64> milliseconds(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return std::nullopt;
    const double scaled = seconds * 1000.0;
    // 2^63 is exactly representable; qint64::max converted to double is not.
    if (!std::isfinite(scaled) || scaled >= 9223372036854775808.0)
        return std::nullopt;
    return static_cast<qint64>(std::llround(scaled));
}
inline bool text(const QString &value) noexcept
{
    return value.size() <= MaxDisplayCharacters;
}
inline bool display(const QString &primary, const QStringList &credits) noexcept
{
    if (!text(primary) || credits.size() > MaxArtistCredits)
        return false;
    for (const auto &credit : credits)
        if (!text(credit)) return false;
    return true;
}
inline bool display(const QString &title, const QString &artistCredit) noexcept
{
    return text(title) && text(artistCredit);
}
} // namespace vinyl::boundary
