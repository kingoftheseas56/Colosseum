#pragma once
#include "server1/ports/ProcessPort.h"
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>
namespace server1::media {
struct TrackConversionResult {
  QByteArray data;
  QString errorName;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
// Injectable process boundary. Callbacks may arrive after cancel; generation
// checks are the converter's responsibility. Process start callbacks must be
// asynchronous.
struct TrackConversionBackend {
  std::function<quint64(const QJsonObject &, std::function<void(QByteArray)>,
                        std::function<void(int, QString)>)>
      start;
  std::function<void(quint64)> cancel;
  std::function<void(quint64)> pause;
  std::function<void(quint64)> resume;
  std::function<void(QJsonObject &)> prepareOptions;
};
class TrackConverter : public QObject {
public:
  using Callback = std::function<void(const TrackConversionResult &)>;
  TrackConverter(QJsonObject input, TrackConversionBackend backend,
                 QObject *parent = nullptr);
  TrackConverter(QJsonObject input, ProcessDriver &driver, QJsonObject settings,
                 int cpuCores, QObject *parent = nullptr);
  ~TrackConverter() override;
  void playlist(Callback callback);
  void initSegment(Callback callback);
  void mediaSegment(int sequence, Callback callback);
  void burnSubtitles(QJsonObject subtitles, Callback callback);
  QJsonObject status() const;
  void status(std::function<void(QJsonObject, QString)> callback);
  void destroy();

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace server1::media
