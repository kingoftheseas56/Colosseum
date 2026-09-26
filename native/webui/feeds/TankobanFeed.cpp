#include "FeedRegistry.h"
#include "FeedValue.h"

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("world"), QStringLiteral("Tankoban"),
    [](const QVariantMap &params) {
        return WorldFeed::validTab(QStringLiteral("Tankoban"),
            params.value(QStringLiteral("tab")).toString());
    },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("world"), 0,
            QStringLiteral("Loading"), QStringLiteral("list"), {},
            QStringLiteral("loading"))};
    },
    [](const FeedContext &ctx) {
        return WorldFeed::build(QStringLiteral("Tankoban"),
            ctx.params.value(QStringLiteral("tab")).toString(), ctx.paths,
            ctx.collection, ctx.showExplicit);
    }, false, true});
} // namespace
