#include "FeedRegistry.h"
#include "FeedValue.h"

#include <QStringList>

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("search"), {},
    [](const QVariantMap &params) {
        return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                           QStringLiteral("Biblio"), QStringLiteral("Theatre")}
            .contains(params.value(QStringLiteral("scope")).toString());
    },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("search.results"), 0,
            QStringLiteral("Search"), QStringLiteral("grid"), {},
            QStringLiteral("loading"))};
    }, nullptr});
} // namespace
