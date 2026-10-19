#pragma once
#include "server1/policy/EngineRegistry.h"
#include <memory>
#include <string>
namespace server1 {
// Shared production composition for the standalone host and native embedding.
// Construct, call and destroy on the Qt application lane.
struct ServerMediaConfig final {
    std::string ffmpeg;
    std::string ffprobe;
    bool hlsV2Enabled=true;
};
class ServerComposition final {
public:
    explicit ServerComposition(policy::EngineRegistryConfig config = {}, ServerMediaConfig mediaConfig = {});
    ~ServerComposition();
    ServerComposition(const ServerComposition &) = delete;
    ServerComposition &operator=(const ServerComposition &) = delete;
    bool listen(unsigned short port);
    void stop();
    unsigned short port() const;
    std::string error() const;
    policy::EngineRegistry &engines();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
