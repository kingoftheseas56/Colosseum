#pragma once
#include <QJsonArray>
#include <QString>
#include <QVector>
namespace server1::media {
class VttTimeline {
public:
  // Decoder-facing fields used by M916/924, with source parser recovery
  // semantics.
  static QJsonArray parse(QString text);
  static QString format(const QJsonArray &cues);
  // Inclusive end bucket, unchanged cue timestamps (source M916).
  static QVector<QJsonArray> partition(const QJsonArray &cues,
                                       int segmentCount);
};
} // namespace server1::media
