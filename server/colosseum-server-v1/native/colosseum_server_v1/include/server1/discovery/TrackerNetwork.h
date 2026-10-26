#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <string>
namespace server1::discovery {
// One oracle tracker client per source run. The caller owns the 30-second
// source scheduler; this client owns only tracker-specified announce intervals.
class TrackerNetwork final : public QObject {
public:
  TrackerNetwork(QString url, QString infoHash, QObject *parent = nullptr);
  ~TrackerNetwork() override;
  void start();
  void close();
  std::function<void(std::string)> onPeer;
  std::function<void(std::string)> onWarning;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace server1::discovery
