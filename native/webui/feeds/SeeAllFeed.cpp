#include "FeedRegistry.h"
#include "FeedValue.h"

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("seeAll"), {},
    [](const QVariantMap &params) {
        return params.value(QStringLiteral("route")).toMap()
            .value(QStringLiteral("v")).toInt() == 1;
    },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
            QStringLiteral("See All"), QStringLiteral("grid"), {},
            QStringLiteral("loading"))};
    }, nullptr});
} // namespace
