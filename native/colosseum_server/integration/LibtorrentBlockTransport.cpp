#include "LibtorrentBlockTransport.h"

#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_request.hpp>
#include <libtorrent/torrent.hpp>

namespace colosseum::server::integration {

namespace {

constexpr std::size_t BlockSize = 16 * 1024;

std::error_code transportError(const std::errc error)
{
    return std::make_error_code(error);
}

bool matchesPeer(const std::string &hint,
                 const lt::peer_connection &peer)
{
    return hint == LibtorrentBlockTransport::peerIdentity(peer.remote())
        || hint == peer.pid().to_string();
}

} // namespace

struct LibtorrentBlockTransport::State
{
    struct Pending {
        std::uint64_t id = 0;
        std::string peerHint;
        scheduler::WireBlock block;
        Completion completion;
    };

    struct Command {
        enum class Kind { Request, Cancel };

        Kind kind = Kind::Request;
        std::uint64_t id = 0;
        int attempts = 0;
        std::chrono::steady_clock::time_point retryAfter{};
        std::string peerHint;
        scheduler::WireBlock block;
    };

    struct Result {
        std::uint64_t id = 0;
        Completion completion;
        std::error_code error;
        std::vector<std::byte> bytes;
        std::string peerHint;
        scheduler::WireBlock block;
    };

    std::mutex mutex;
    bool shuttingDown = false;
    std::uint64_t nextId = 1;
    std::map<std::uint64_t, Pending> pending;
    std::deque<Command> commands;
    std::deque<Result> results;
};

struct LibtorrentBlockTransport::TorrentPlugin final : lt::torrent_plugin
{
    explicit TorrentPlugin(std::shared_ptr<State> state, lt::torrent *torrent)
        : state_(std::move(state)), torrent_(torrent)
    {
    }

    std::shared_ptr<lt::peer_plugin> new_connection(
        lt::peer_connection_handle const &peer) override;

    void tick() override
    {
        if (torrent_)
            processNextCommand(state_, *torrent_);
    }

    static void processNextCommand(const std::shared_ptr<State> &state,
                                   lt::torrent &torrent);
    static void failPending(const std::shared_ptr<State> &state,
                            std::uint64_t id,
                            std::error_code error);

    std::shared_ptr<State> state_;
    lt::torrent *torrent_ = nullptr;
};

struct LibtorrentBlockTransport::PeerPlugin final : lt::peer_plugin
{
    PeerPlugin(std::shared_ptr<State> state,
               lt::peer_connection_handle peer)
        : state_(std::move(state)), peer_(std::move(peer))
    {
    }

    bool on_piece(lt::peer_request const &request,
                  lt::span<char const> bytes) override
    {
        const auto peerIdentity = LibtorrentBlockTransport::peerIdentity(peer_.remote());
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown)
            return false;

        for (auto it = state_->pending.begin(); it != state_->pending.end(); ++it) {
            const auto &pending = it->second;
            if (pending.peerHint != peerIdentity
                && pending.peerHint != peer_.pid().to_string()) {
                continue;
            }
            if (pending.block.piece != static_cast<std::size_t>(request.piece)
                || pending.block.offset != static_cast<std::size_t>(request.start)
                || pending.block.length != static_cast<std::size_t>(request.length)) {
                continue;
            }

            std::vector<std::byte> copied;
            copied.reserve(static_cast<std::size_t>(bytes.size()));
            for (const char byte : bytes)
                copied.push_back(std::byte{static_cast<unsigned char>(byte)});

            auto completion = std::move(pending.completion);
            const auto id = pending.id;
            const auto peerHint = pending.peerHint;
            const auto block = pending.block;
            state_->pending.erase(it);
            state_->results.push_back(State::Result{
                id, std::move(completion), {}, std::move(copied),
                peerHint, block});
            return false;
        }
        return false;
    }

    void on_disconnect(lt::error_code const &) override
    {
        const auto peerIdentity = LibtorrentBlockTransport::peerIdentity(peer_.remote());
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown)
            return;

        for (auto it = state_->pending.begin(); it != state_->pending.end();) {
            if (it->second.peerHint != peerIdentity
                && it->second.peerHint != peer_.pid().to_string()) {
                ++it;
                continue;
            }
            auto pending = std::move(it->second);
            it = state_->pending.erase(it);
            state_->results.push_back(State::Result{
                pending.id, std::move(pending.completion),
                transportError(std::errc::connection_aborted), {},
                pending.peerHint, pending.block});
        }
    }

    std::shared_ptr<State> state_;
    lt::peer_connection_handle peer_;
};

std::shared_ptr<lt::peer_plugin> LibtorrentBlockTransport::TorrentPlugin::new_connection(
    lt::peer_connection_handle const &peer)
{
    return std::make_shared<PeerPlugin>(state_, peer);
}

void LibtorrentBlockTransport::TorrentPlugin::failPending(
    const std::shared_ptr<State> &state,
    const std::uint64_t id,
    std::error_code error)
{
    std::lock_guard lock(state->mutex);
    const auto it = state->pending.find(id);
    if (it == state->pending.end())
        return;
    auto pending = std::move(it->second);
    state->pending.erase(it);
    state->results.push_back(State::Result{
        pending.id, std::move(pending.completion), error, {},
        pending.peerHint, pending.block});
}

void LibtorrentBlockTransport::TorrentPlugin::processNextCommand(
    const std::shared_ptr<State> &state,
    lt::torrent &torrent)
{
    State::Command command;
    {
        std::lock_guard lock(state->mutex);
        if (state->shuttingDown || state->commands.empty())
            return;
        if (state->commands.front().retryAfter > std::chrono::steady_clock::now())
            return;
        command = std::move(state->commands.front());
        state->commands.pop_front();
    }

    lt::peer_connection *connection = nullptr;
    for (auto *candidate : torrent) {
        if (candidate && !candidate->is_disconnecting()
            && matchesPeer(command.peerHint, *candidate)) {
            connection = candidate;
            break;
        }
    }

    if (command.kind == State::Command::Kind::Cancel) {
        if (!connection || command.block.offset % BlockSize != 0)
            return;
        connection->cancel_request(
            lt::piece_block{lt::piece_index_t{
                static_cast<std::int32_t>(command.block.piece)},
                static_cast<int>(command.block.offset / BlockSize)},
            true);
        connection->send_block_requests();
        return;
    }

    if (command.block.length == 0
        || command.block.length > BlockSize
        || command.block.offset % BlockSize != 0
        || command.block.piece > static_cast<std::size_t>(
            (std::numeric_limits<std::int32_t>::max)())) {
        failPending(state, command.id,
                    transportError(std::errc::invalid_argument));
        return;
    }
    {
        std::lock_guard lock(state->mutex);
        if (state->shuttingDown || state->pending.find(command.id) == state->pending.end())
            return;
    }
    if (!connection) {
        if (command.peerHint.find(':') != std::string::npos
            && command.attempts < 20) {
            ++command.attempts;
            command.retryAfter = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(100);
            std::lock_guard lock(state->mutex);
            if (!state->shuttingDown
                && state->pending.find(command.id) != state->pending.end())
                state->commands.push_back(std::move(command));
            return;
        }
        failPending(state, command.id,
                    transportError(std::errc::connection_refused));
        return;
    }

    const lt::piece_block block{
        lt::piece_index_t{static_cast<std::int32_t>(command.block.piece)},
        static_cast<int>(command.block.offset / BlockSize)};
    connection->send_interested();
    if (connection->has_peer_choked()) {
        if (command.attempts < 100) {
            ++command.attempts;
            command.retryAfter = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(100);
            std::lock_guard lock(state->mutex);
            if (!state->shuttingDown
                && state->pending.find(command.id) != state->pending.end())
                state->commands.push_back(std::move(command));
            return;
        }
        failPending(state, command.id,
                    transportError(std::errc::operation_would_block));
        return;
    }
    if (!connection->add_request(block, lt::peer_connection::time_critical)) {
        if (command.attempts < 10) {
            ++command.attempts;
            command.retryAfter = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(100);
            std::lock_guard lock(state->mutex);
            if (!state->shuttingDown
                && state->pending.find(command.id) != state->pending.end())
                state->commands.push_back(std::move(command));
            return;
        }
        failPending(state, command.id,
                    transportError(std::errc::operation_would_block));
        return;
    }
    connection->send_block_requests();
}

std::string LibtorrentBlockTransport::peerIdentity(const lt::tcp::endpoint &endpoint)
{
    const auto address = endpoint.address().to_string();
    if (endpoint.address().is_v6())
        return "[" + address + "]:" + std::to_string(endpoint.port());
    return address + ":" + std::to_string(endpoint.port());
}

LibtorrentBlockTransport::LibtorrentBlockTransport(lt::torrent_handle torrent)
    : torrent_(std::move(torrent)), state_(std::make_shared<State>())
{
    if (torrent_.is_valid()) {
        const auto state = state_;
        torrent_.add_extension(
            [state](lt::torrent_handle const &handle, lt::client_data_t)
                -> std::shared_ptr<lt::torrent_plugin> {
                const auto native = handle.native_handle();
                if (!native)
                    return std::shared_ptr<lt::torrent_plugin>{};
                return std::shared_ptr<lt::torrent_plugin>(
                    std::make_shared<TorrentPlugin>(state, native.get()));
            });
    }
}

LibtorrentBlockTransport::~LibtorrentBlockTransport()
{
    std::lock_guard lock(state_->mutex);
    state_->shuttingDown = true;
    state_->pending.clear();
    state_->commands.clear();
    state_->results.clear();
}

void LibtorrentBlockTransport::enqueueCommand()
{
    const auto state = state_;
    torrent_.add_extension(
        [state](lt::torrent_handle const &handle, lt::client_data_t) {
            if (const auto torrent = handle.native_handle())
                TorrentPlugin::processNextCommand(state, *torrent);
            return std::shared_ptr<lt::torrent_plugin>{};
        });
}

void LibtorrentBlockTransport::requestBlock(
    const std::string &peerHint,
    const scheduler::WireBlock &block,
    Completion completion)
{
    if (!torrent_.is_valid()) {
        std::lock_guard lock(state_->mutex);
        state_->results.push_back(State::Result{
            0, std::move(completion),
            transportError(std::errc::not_connected), {}, peerHint, block});
        return;
    }

    {
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown)
            return;
        const auto id = state_->nextId++;
        state_->pending.emplace(id,
            State::Pending{id, peerHint, block, std::move(completion)});
        state_->commands.push_back(
            State::Command{State::Command::Kind::Request, id, 0, {}, peerHint, block});
    }
    enqueueCommand();
}

void LibtorrentBlockTransport::cancelBlock(
    const std::string &peerHint,
    const scheduler::WireBlock &block)
{
    std::vector<std::uint64_t> cancelled;
    {
        std::lock_guard lock(state_->mutex);
        for (auto it = state_->pending.begin(); it != state_->pending.end();) {
            if (it->second.peerHint != peerHint
                || it->second.block.piece != block.piece
                || it->second.block.offset != block.offset
                || it->second.block.length != block.length) {
                ++it;
                continue;
            }
            cancelled.push_back(it->first);
            it = state_->pending.erase(it);
        }
        for (auto it = state_->results.begin(); it != state_->results.end();) {
            if (it->peerHint == peerHint
                && it->block.piece == block.piece
                && it->block.offset == block.offset
                && it->block.length == block.length) {
                it = state_->results.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto id : cancelled) {
            state_->commands.push_back(
                State::Command{State::Command::Kind::Cancel, id, 0, {}, peerHint, block});
        }
    }
    if (!cancelled.empty() && torrent_.is_valid())
        enqueueCommand();
}

void LibtorrentBlockTransport::pumpResults()
{
    std::deque<State::Result> results;
    bool commandReady = false;
    {
        std::lock_guard lock(state_->mutex);
        results.swap(state_->results);
        commandReady = !state_->shuttingDown && !state_->commands.empty()
            && state_->commands.front().retryAfter <= std::chrono::steady_clock::now();
    }
    for (auto &result : results)
        if (result.completion)
            result.completion(result.error, std::move(result.bytes));
    if (commandReady && torrent_.is_valid())
        enqueueCommand();
}

} // namespace colosseum::server::integration
