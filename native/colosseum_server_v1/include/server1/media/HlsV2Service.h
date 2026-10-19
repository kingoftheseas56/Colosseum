#pragma once
#include "server1/media/TrackConverter.h"
#include <QJsonArray>
#include <QString>
#include <functional>
#include <memory>
namespace server1::media {
class HlsV2Converter {
public:
  using Callback = TrackConverter::Callback;
  ~HlsV2Converter();
  void playlist(QString track, Callback callback);
  void initSegment(QString track, Callback callback);
  void mediaSegment(QString track, int sequence, Callback callback);
  void burnSubtitles(QJsonObject subtitles, Callback callback);
  void status(std::function<void(QJsonObject, QString)> callback);
  void destroy();

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
  explicit HlsV2Converter(std::shared_ptr<Impl> impl);
  friend class HlsV2Service;
};
class HlsV2Service : public QObject {
public:
  HlsV2Service(QString ffmpeg, QString ffprobe, QObject *parent = nullptr);
  ~HlsV2Service() override;
  static QString queryString(const QJsonObject &options,
                             QString *error = nullptr);
  std::shared_ptr<HlsV2Converter> acquire(QString id, QJsonObject options,
                                          QString *error = nullptr);
  void destroy(
      const QString &id); // Deliberately leaves the destroyed registry entry.
  bool contains(const QString &id) const;
  void status(std::function<void(QJsonObject, QString)> callback);
  void probe(QString mediaURL, bool samples,
             std::function<void(QJsonObject, QString)> callback);
  void cleanup();
  void setConcurrency(int sources, int tracks);
  void setClock(std::function<qint64()> clock);
  void setTranscodeSettings(QJsonObject settings, int cpuCores);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace server1::media
