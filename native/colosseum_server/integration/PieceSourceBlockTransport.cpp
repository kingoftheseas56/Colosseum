#include "PieceSourceBlockTransport.h"

#include <utility>

namespace colosseum::server::integration {

PieceSourceBlockTransport::PieceSourceBlockTransport(IPieceSource &source)
    : state_(std::make_shared<State>(source))
{
}

PieceSourceBlockTransport::~PieceSourceBlockTransport()
{
    std::vector<IPieceSource::Subscription> subscriptions;
    {
        std::lock_guard lock(state_->mutex);
        state_->shuttingDown = true;
        subscriptions.reserve(state_->pending.size());
        for (auto &[token, pending] : state_->pending) {
            pending->cancelled = true;
            subscriptions.push_back(token);
        }
        state_->pending.clear();
    }
    for (const auto token : subscriptions)
        state_->source.cancelWait(token);
}

void PieceSourceBlockTransport::finishRead(
    IPieceSource &source,
    const scheduler::WireBlock &block,
    Completion completion)
{
    std::error_code error;
    auto bytes = source.readBlock(block, error);
    completion(error, std::move(bytes));
}

void PieceSourceBlockTransport::onReady(
    const std::shared_ptr<State> &state,
    const std::shared_ptr<Pending> &pending,
    std::error_code error)
{
    Completion completion;
    scheduler::WireBlock block;
    {
        std::lock_guard lock(state->mutex);
        if (state->shuttingDown || pending->cancelled || pending->completed)
            return;
        pending->completed = true;
        if (pending->registered)
            state->pending.erase(pending->subscription);
        completion = std::move(pending->completion);
        block = pending->block;
    }

    if (error) {
        completion(error, {});
        return;
    }
    finishRead(state->source, block, std::move(completion));
}

void PieceSourceBlockTransport::requestBlock(
    const std::string &peerHint,
    const scheduler::WireBlock &block,
    Completion completion)
{
    if (state_->source.hasPiece(block.piece)) {
        finishRead(state_->source, block, std::move(completion));
        return;
    }

    auto pending = std::make_shared<Pending>(Pending{
        peerHint, block, 0, std::move(completion), false, false, false});
    state_->source.makeUrgent(block.piece);
    const auto token = state_->source.waitForPiece(
        block.piece,
        [state = state_, pending](std::error_code error) mutable {
            onReady(state, pending, error);
        });

    bool cancelRegistration = false;
    {
        std::lock_guard lock(state_->mutex);
        pending->subscription = token;
        if (state_->shuttingDown) {
            pending->cancelled = true;
            cancelRegistration = !pending->completed;
        } else if (!pending->completed) {
            pending->registered = true;
            state_->pending[token] = pending;
        }
    }
    if (cancelRegistration)
        state_->source.cancelWait(token);
}

void PieceSourceBlockTransport::cancelBlock(
    const std::string &peerHint,
    const scheduler::WireBlock &block)
{
    std::vector<IPieceSource::Subscription> subscriptions;
    {
        std::lock_guard lock(state_->mutex);
        for (auto it = state_->pending.begin(); it != state_->pending.end();) {
            const auto &pending = *it->second;
            if (pending.peerHint == peerHint
                && pending.block.piece == block.piece
                && pending.block.offset == block.offset
                && pending.block.length == block.length) {
                it->second->cancelled = true;
                subscriptions.push_back(it->first);
                it = state_->pending.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto token : subscriptions)
        state_->source.cancelWait(token);
}

} // namespace colosseum::server::integration
