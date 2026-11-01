#include "server1/media/SegmentLayout.h"
#include "server1/media/KeyframeIndex.h"
#include <cmath>
#include <stdexcept>
namespace server1::media {
QJsonArray SegmentLayout::retrieve(const QJsonObject &options) {
  auto grouped = groupKeyframeSegments(options);
  if (grouped["predicted"].toBool())
    return predict(options["stream"].toObject(), options["format"].toObject(),
                   grouped["segmentDuration"].toDouble(),
                   options["timescale"].toDouble());
  QJsonArray result;
  for (auto value : grouped["segments"].toArray())
    result.append(value.toObject()["segment"]);
  return result;
}
QJsonArray SegmentLayout::predict(const QJsonObject &stream,
                                  const QJsonObject &format,
                                  double segmentDuration, double timescale) {
  const double coefficient = timescale / stream["timescale"].toDouble();
  const double start = stream["startTimeTs"].isDouble()
                           ? stream["startTimeTs"].toDouble() * coefficient
                           : 0;
  const double total = std::ceil(format["duration"].toDouble() * timescale),
               step = std::ceil(segmentDuration * timescale);
  if (!std::isfinite(total) || !std::isfinite(step) || !std::isfinite(start) ||
      timescale <= 0 || step <= 0 || total < 0)
    throw std::invalid_argument("Invalid segment timing");
  const double count = std::floor(total / step),
               remaining = std::fmod(total, step);
  // Explicit resource boundary: reject rather than truncate or wrap native
  // allocation.
  if (count > 1000000)
    throw std::length_error("Segment count exceeds native resource limit");
  QJsonArray out;
  for (int i = 0; i < int(count) + (remaining != 0); ++i)
    out.append(QJsonObject{{"sequenceNumber", i + 1},
                           {"seekTime", start + i * step},
                           {"duration", i < int(count) ? step : remaining},
                           {"buffer", QJsonValue::Null},
                           {"cues", QJsonArray{}},
                           {"extraSegments", QJsonArray{}}});
  return out;
}
} // namespace server1::media
