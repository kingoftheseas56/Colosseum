#include "FeedRegistry.h"
#include "FeedValue.h"

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("home"), {},
    [](const QVariantMap &params) { return params.isEmpty(); },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("home.main"), 0,
            QStringLiteral("Home"), QStringLiteral("list"), {},
            QStringLiteral("loading"))};
    }, nullptr});
} // namespace
