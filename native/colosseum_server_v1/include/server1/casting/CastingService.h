#pragma once
#include "server1/casting/CastingProtocol.h"
#include "server1/ports/ProcessPort.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <memory>
namespace server1::casting {
// Passive discovery ingestion only: no multicast, device control, app
// launching, listener rebinding, certificate exceptions or security
// configuration changes.
class CastingService : public QObject {
public:
  explicit CastingService(QString ffmpeg, QObject *parent = nullptr);
  ~CastingService() override;
  void collect(const QJsonObject &device); // M945: first observation wins
  QJsonArray devices() const;
  QJsonObject device(const QString &id) const;
  static QJsonObject ssdpDevice(const QString &location, const QByteArray &xml);
  static QJsonObject mdnsDevice(const QJsonObject &response);
  static QJsonObject videoInfo(const QByteArray &stderrData);
  static double hmsToSeconds(const QString &time);
  static QStringList arguments(const QJsonObject &query,
                               const QJsonObject &info,
                               const QString &subtitleFile = {});
  using PlayerInvoker =
      std::function<void(const QJsonObject &, const PlayerCall &,
                         const QJsonObject &, std::function<void(QJsonValue)>)>;
  void setPlayerInvoker(PlayerInvoker invoker);
  bool hasPlayerInvoker() const;
  void invokePlayer(const QString &id, const QJsonObject &query,
                    const QJsonObject &body,
                    std::function<void(QJsonValue)> callback);
  media::ProcessPort &processes();
  QString executable() const;
  qsizetype bufferedBytes() const;
  qsizetype peakBufferedBytes() const;
  // Stream ownership accounting; exposed for diagnostics and bounded-I/O tests.
  void adjustBufferedBytes(qsizetype delta);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Service must outlive installed routes; requests guard QObject lifetime.
// Active player control remains disabled until explicitly configured/qualified.
bool installCastingRoutes(void *router, CastingService &service);
} // namespace server1::casting
