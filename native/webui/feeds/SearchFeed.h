#pragma once

#include "WorldFeed.h"

#include <QVariantMap>

class SearchFeed final {
public:
    // Run on a feed worker. The bridge can issue native `open` with this Item
    // after `search.surprise` has selected a real title.
    static QVariantMap surpriseItem(const QString &scope,
                                    const WorldFeed::Paths &paths,
                                    bool showExplicit);
};
