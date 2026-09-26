#pragma once

#include "FeedRegistry.h"

class TheatreFeed final {
public:
    static QVariantList build(const FeedContext &context);
    static QVariantList enrich(const FeedContext &context);
};
