#pragma once
#include "SubtitleFetch.h"
namespace server1::media {
struct TrackResult {
  QJsonArray tracks;
  QString error;
  bool incomplete = false;
  qint64 nextOffset = -1;
  qint64 nextSize = 15728640;
};
TrackResult extractTrackBytes(const QByteArray &data,
                              bool mp4Continuation = false);
QObject *extractMediaTracks(const QString &path,
                            std::function<void(TrackResult)> callback,
                            QObject *parent = nullptr,
                            qint64 maxBytesLimit = 26214400);
} // namespace server1::media
