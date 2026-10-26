#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace server1::media {
class SegmentLayout {
public:
  static QJsonArray retrieve(const QJsonObject &options);
  static QJsonArray predict(const QJsonObject &stream,
                            const QJsonObject &format, double segmentDuration,
                            double timescale);
};
} // namespace server1::media
