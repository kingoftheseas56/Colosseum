#pragma once
#include "StreamTypes.h"
namespace colosseum::stream {
class StreamParser {
  public:
    static ParsedStream parse(const Stream &stream);
    static bool isTrustedGroup(const QString &normalized);
    static bool isBatchRelease(const QString &text);
    static bool hasDualAudio(const QString &text);
};
} // namespace colosseum::stream
