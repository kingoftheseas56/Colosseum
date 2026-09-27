#pragma once
#include "FeedRegistry.h"

namespace TheatreDetailProjection {
QVariantList build(const FeedContext &ctx, const QVariantMap &meta,
                   const QVariantList &related, const QVariantList &sourceRows);
} // namespace TheatreDetailProjection
