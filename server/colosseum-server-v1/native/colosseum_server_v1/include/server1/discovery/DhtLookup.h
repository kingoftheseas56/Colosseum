#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace server1::discovery {

// Persistent IPv4 DHT service. PeerSearch owns user lookup start/pause cadence.
// The service owns bootstrap, routing, inbound RPC and network timeouts.
// All calls and callbacks must run on the object's Qt thread.
class DhtLookup final : public QObject {
public:
    struct Endpoint {
        QString host;
        quint16 port = 6881;
    };

    explicit DhtLookup(QString infoHash, QObject *parent = nullptr);
    DhtLookup(QString infoHash, std::vector<Endpoint> bootstrapNodes,
              QObject *parent = nullptr);
    ~DhtLookup() override;

    using LookupId = quint64;
    [[nodiscard]] LookupId startLookup();
    void abortLookup(LookupId id);
    void close();

    // Compatibility helpers: one lookup and immediate cancellation of all
    // user lookups. Neither helper destroys the persistent routing service.
    void start();
    void cancel();
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] static std::vector<Endpoint> defaultBootstrapNodes();

    // Duplicates are intentional: the coordinator owns unique-peer accounting.
    std::function<void(std::string)> onPeer;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace server1::discovery
