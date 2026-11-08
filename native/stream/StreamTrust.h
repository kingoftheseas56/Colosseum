#pragma once
#include "StreamTypes.h"
namespace colosseum::stream {
class StreamTrust {
  public:
    static TrustResult apply(const QList<ParsedStream> &streams, const TrustOptions &options);
};
} // namespace colosseum::stream
