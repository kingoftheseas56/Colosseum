#pragma once
#include "HomeServerClient.h"
#include <QJsonObject>
#include <QPointer>
#include <QVariantMap>

struct HomeServerPlaybackOptions {
  bool android = false;
  QVariantMap videoDecodeCapabilities;
  bool forceTranscode = false;
};

// Native-only resolver result: never expose URL/header credentials to QML or
// sessions.
class HomeServerPlaybackJob final : public QObject {
public:
  struct Result {
    QString url;
    QVariantMap headers;
    QString mediaSourceId;
    QString playSessionId;
    QString playMethod;
    bool transcoded = false;
    QString error;
    bool expired = false;
    bool ok() const { return error.isEmpty() && !url.isEmpty(); }
  };
  using Callback = std::function<void(Result)>;
  explicit HomeServerPlaybackJob(HomeServerClient *client,
                                 QObject *parent = nullptr);
  ~HomeServerPlaybackJob() override;
  void start(QJsonObject connection, QJsonObject item, QString versionId,
             QString token, HomeServerPlaybackOptions options,
             Callback callback);
  void cancel();
  static QUrl admittedUrl(QUrl origin, QString path);
  static QJsonObject deviceProfile(HomeServerPlaybackOptions options);

private:
  void request(QString path, QByteArray method, QJsonObject body,
               std::function<void(QJsonObject)> callback);
  void jellyfin();
  void plex();
  void finish(Result result);
  void fail(QString error, bool expired = false);
  QVariantMap playbackHeaders() const;
  QPointer<HomeServerClient> m_client;
  Callback m_callback;
  QJsonObject m_connection, m_item, m_version;
  HomeServerPlaybackOptions m_options;
  QUrl m_origin;
  QString m_token, m_session;
  bool m_started = false, m_finished = false;
};
