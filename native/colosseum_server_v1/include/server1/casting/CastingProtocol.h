#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>
namespace server1::casting {
struct PlayerCall {
  QString method;
  QJsonArray arguments;
};
QJsonObject initialPlayerStatus();
PlayerCall selectPlayerCall(QJsonObject &status, const QJsonObject &query,
                            const QJsonObject &body);
QJsonObject conformDlnaStatus(QJsonObject status, const QJsonObject &update,
                              int flags = 1, qint64 seekTime = 0);
QJsonObject conformChromecastStatus(QJsonObject status, QJsonObject update,
                                    int flags = 1);
// Native protobuf CastMessage encoding + bounded incremental frame decoding.
QByteArray encodeCastFrame(const QString &sender, const QString &receiver,
                           const QString &nameSpace,
                           const QJsonObject &message);
bool decodeCastFrames(QByteArray &pending, QJsonArray &messages,
                      QString &error);
struct SoapResult {
  QJsonObject values;
  QString error;
};
// Explicit control endpoint. Does not discover, subscribe, open listeners, or
// disable TLS validation. Redirects are rejected to keep the configured target.
class DlnaSoapClient : public QObject {
public:
  using Callback = std::function<void(SoapResult)>;
  explicit DlnaSoapClient(QUrl endpoint, QObject *parent = nullptr);
  void call(const QString &service, const QString &action,
            const QJsonObject &arguments, Callback callback);
  static QByteArray envelope(const QString &service, const QString &action,
                             const QJsonObject &arguments);

private:
  QUrl endpoint_;
};
} // namespace server1::casting
