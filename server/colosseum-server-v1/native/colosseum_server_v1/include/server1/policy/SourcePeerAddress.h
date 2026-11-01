#pragma once
#include <optional>
#include <string>
namespace server1::policy {
// M818 add validates only Number(addr.split(':')[1]); net.connect receives the
// original first two components. Raw text remains the retained-peer key.
struct SourcePeerAddress final {
    std::string raw,host,portText;
    double portNumber=0;
};
[[nodiscard]] std::optional<SourcePeerAddress> parseSourcePeerAddress(const std::string &raw);
}
