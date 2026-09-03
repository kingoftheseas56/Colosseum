#pragma once

#include "SchedulerTransportBridge.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace colosseum::server::integration {

class IPieceSource
{
public:
    using Subscription = std::uint64_t;
    using Ready = std::function<void(std::error_code)>;

    virtual ~IPieceSource() = default;
    virtual bool hasPiece(std::size_t piece) const = 0;
    virtual void makeUrgent(std::size_t piece) = 0;
    virtual Subscription waitForPiece(std::size_t piece, Ready ready) = 0;
    virtual void cancelWait(Subscription token) = 0;
    virtual std::vector<std::byte> readBlock(
        const scheduler::WireBlock &block,
        std::error_code &error) const = 0;
};

class PieceSourceBlockTransport final : public IBlockTransport
{
public:
    explicit PieceSourceBlockTransport(IPieceSource &source);
    ~PieceSourceBlockTransport();

    void requestBlock(const std::string &peerHint,
                      const scheduler::WireBlock &block,
                      Completion completion) override;
    void cancelBlock(const std::string &peerHint,
                     const scheduler::WireBlock &block) override;

private:
    struct Pending {
        std::string peerHint;
        scheduler::WireBlock block;
        IPieceSource::Subscription subscription = 0;
        Completion completion;
        bool cancelled = false;
        bool completed = false;
        bool registered = false;
    };

    struct State {
        explicit State(IPieceSource &source)
            : source(source)
        {
        }

        IPieceSource &source;
        std::mutex mutex;
        bool shuttingDown = false;
        std::map<IPieceSource::Subscription, std::shared_ptr<Pending>> pending;
    };

    static void finishRead(IPieceSource &source,
                           const scheduler::WireBlock &block,
                           Completion completion);
    static void onReady(const std::shared_ptr<State> &state,
                        const std::shared_ptr<Pending> &pending,
                        std::error_code error);

    std::shared_ptr<State> state_;
};

} // namespace colosseum::server::integration
