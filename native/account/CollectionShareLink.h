#pragma once
#include <QUrl>
#include <optional>

namespace Colosseum::Collections {
// Text entry must use this overload before QUrl normalizes percent escapes.
std::optional<QString> shareIdFromUrl(const QString &text, const QUrl &webOrigin, const QUrl &legacyServiceOrigin);
// Platform events already supply a normalized QUrl; original escape spelling is unavailable.
std::optional<QString> shareIdFromUrl(const QUrl &url, const QUrl &webOrigin, const QUrl &legacyServiceOrigin);
QUrl publicShareLink(const QString &shareId, const QUrl &webOrigin);
QUrl applicationShareLink(const QString &shareId);
}
