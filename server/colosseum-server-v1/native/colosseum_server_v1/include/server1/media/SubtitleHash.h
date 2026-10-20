#pragma once
#include "SubtitleFetch.h"
namespace server1::media {
QString subtitleHashBytes(const QByteArray &bytes);
struct HashResult {
  QJsonObject value;
  QString error;
};
QObject *calculateSubtitleHash(const QString &url,
                               std::function<void(HashResult)> callback,
                               QObject *parent = nullptr);
} // namespace server1::media
