#pragma once
#include <QJsonArray>
#include <QString>
namespace server1::media {
class HlsPlaylist {
public:
  // Ordered segment array represents the source Map's 1-based root keys.
  static QString master(const QJsonArray &streams, const QString &query);
  static QString media(const QJsonArray &segments, double timescale,
                       const QString &track, const QString &query);
  static QString fixed(double value, int digits);
};
} // namespace server1::media
