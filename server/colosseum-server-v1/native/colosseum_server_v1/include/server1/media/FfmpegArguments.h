#pragma once
#include <QJsonObject>
#include <QStringList>
namespace server1::media {
// Pure translations of M916, M932, M933. No capability discovery or host
// defaults.
class FfmpegArguments {
public:
  static QJsonObject conversionOptions(const QJsonObject &input,
                                       const QJsonObject &subtitles = {});
  static double videoBitrate(const QJsonObject &stream,
                             const QString &inputCodec,
                             const QString &outputCodec, double maximum = 0);
  static double audioBitrate(const QJsonObject &stream, const QString &codec,
                             double channels);
  static QJsonObject profiles();
  static QStringList build(const QJsonObject &options,
                           const QJsonObject &settings, int cpuCores);
};
} // namespace server1::media
