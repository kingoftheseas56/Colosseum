#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QUrl>
#include <functional>
namespace server1::media {
struct ByteResult {
  QByteArray data;
  QString error;
  int status = 0;
  QString statusText;
  QUrl url;
  QMap<QByteArray, QByteArray> headers;
};
using ByteCallback = std::function<void(ByteResult)>;
// Requests are children of lifetime. Destroying lifetime cancels pending I/O
// and callbacks.
void mediaRequest(QObject *lifetime, const QString &path,
                  const QByteArray &method,
                  const QMap<QByteArray, QByteArray> &headers,
                  ByteCallback callback, int redirectLimit = 12);
struct SubtitleOptions {
  QString charset = "auto";
  bool hasAgent = false;
  std::function<void(QObject *, const QString &, ByteCallback)> agentRequest;
};
struct SubtitleResult {
  QString text;
  QJsonArray tracks;
  QString error;
};
QJsonArray parseSrt(const QString &text);
QString decodeSubtitle(const QByteArray &data, const QString &charset,
                       QString *error);
QByteArray unwrapSubtitle(const QString &path, const QByteArray &data,
                          QString *error);
QObject *retrieveSubtitle(const QString &path, SubtitleOptions options,
                          std::function<void(SubtitleResult)> callback,
                          QObject *parent = nullptr);
QByteArray renderSubtitles(const QJsonArray &tracks, bool vtt,
                           qint64 offset = 0);
} // namespace server1::media
