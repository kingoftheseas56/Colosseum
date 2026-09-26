#pragma once

#include "FeedValue.h"

// CONTRACT v1.1 Step 3 permits these feed owners to return loading until
// their QML logic is moved. Keeping one class per feed makes that boundary
// explicit and prevents the transport from becoming a projection owner.
class HomeFeed final {
public:
    static QVariantList initial() {
        return {WebFeedValue::section(QStringLiteral("home.main"), 0,
                   QStringLiteral("Home"), QStringLiteral("list"), {},
                   QStringLiteral("loading"))};
    }
};

class SeeAllFeed final {
public:
    static QVariantList initial() {
        return {WebFeedValue::section(QStringLiteral("seeAll.results"), 0,
                   QStringLiteral("See All"), QStringLiteral("grid"), {},
                   QStringLiteral("loading"))};
    }
};

class SearchFeed final {
public:
    static QVariantList initial() {
        return {WebFeedValue::section(QStringLiteral("search.results"), 0,
                   QStringLiteral("Search"), QStringLiteral("grid"), {},
                   QStringLiteral("loading"))};
    }
};
