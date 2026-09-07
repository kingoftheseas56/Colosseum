#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace lt = libtorrent;
using namespace std::chrono_literals;

namespace {

struct SharedState
{
    explicit SharedState(int target_port_, bool control_)
        : target_port(target_port_), control(control_) {}

    int target_port = 0;
    bool control = false;
    std::atomic<int> peers{0};
    std::atomic<int> peers_with_piece{0};
    std::atomic<int> sent_requests{0};
    std::atomic<int> target_sent{0};
    std::atomic<int> target_received{0};
    std::atomic<int> injection_failures{0};
    std::atomic<bool> allow_disconnect{false};
    std::mutex output_mutex;
};

void put_u32(std::array<char, 17>& msg, int pos, std::uint32_t value)
{
    msg[std::size_t(pos + 0)] = char((value >> 24) & 0xff);
    msg[std::size_t(pos + 1)] = char((value >> 16) & 0xff);
    msg[std::size_t(pos + 2)] = char((value >> 8) & 0xff);
    msg[std::size_t(pos + 3)] = char(value & 0xff);
}

void send_interested(lt::peer_connection_handle& pc)
{
    std::array<char, 5> msg{{0, 0, 0, 1, 2}};
    pc.send_buffer(msg.data(), int(msg.size()));
}

void send_exact_request(lt::peer_connection_handle& pc)
{
    std::array<char, 17> msg{};
    put_u32(msg, 0, 13);
    msg[4] = 6;
    put_u32(msg, 5, 0);
    put_u32(msg, 9, 0);
    put_u32(msg, 13, 16384);
    pc.send_buffer(msg.data(), int(msg.size()));
}

class PeerProbe final : public lt::peer_plugin
{
public:
    PeerProbe(lt::peer_connection_handle pc, std::shared_ptr<SharedState> state)
        : pc_(std::move(pc))
        , state_(std::move(state))
        , target_(int(pc_.remote().port()) == state_->target_port)
    {
        state_->peers.fetch_add(1, std::memory_order_relaxed);
        suppress_picker();

        std::lock_guard<std::mutex> lock(state_->output_mutex);
        std::cout << "P08A_PEER_CONNECTED port=" << pc_.remote().port()
                  << " target=" << (target_ ? 1 : 0)
                  << " control=" << (state_->control ? 1 : 0) << '\n';
    }

    lt::string_view type() const override { return "server1-p08a"; }

    bool on_handshake(lt::span<char const>) override
    {
        suppress_picker();
        return true;
    }

    bool on_bitfield(lt::bitfield const&) override
    {
        note_piece_available();
        return false;
    }

    bool on_have(lt::piece_index_t piece) override
    {
        if (piece == lt::piece_index_t(0)) note_piece_available();
        return false;
    }

    bool on_have_all() override
    {
        note_piece_available();
        return false;
    }

    bool on_unchoke() override
    {
        unchoked_ = true;
        if (!state_->control) send_interested(pc_);
        maybe_inject_owned_request();
        return false;
    }

    void tick() override
    {
        maybe_inject_owned_request();
    }

    void sent_request(lt::peer_request const& r) override
    {
        state_->sent_requests.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(state_->output_mutex);
        std::cout << "P08A_LIBTORRENT_SENT_REQUEST peer_port=" << pc_.remote().port()
                  << " piece=" << static_cast<int>(r.piece)
                  << " offset=" << r.start
                  << " length=" << r.length << '\n';
    }

    bool on_piece(lt::peer_request const& r, lt::span<char const> buf) override
    {
        if (!state_->control && target_
            && r.piece == lt::piece_index_t(0)
            && r.start == 0 && r.length == 16384
            && int(buf.size()) == 16384)
        {
            state_->target_received.store(1, std::memory_order_release);
            std::lock_guard<std::mutex> lock(state_->output_mutex);
            std::cout << "P08A_OWNED_RESPONSE_RECEIVED peer_port=" << pc_.remote().port()
                      << " piece=0 offset=0 length=16384\n";
            return true;
        }
        return false;
    }

    bool can_disconnect(lt::error_code const&) override
    {
        return state_->allow_disconnect.load(std::memory_order_acquire);
    }

private:
    void suppress_picker()
    {
        if (state_->control) return;
        auto native = pc_.native_handle();
        if (!native)
        {
            state_->injection_failures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        native->no_download(true);
    }

    void note_piece_available()
    {
        if (!piece_available_)
        {
            piece_available_ = true;
            state_->peers_with_piece.fetch_add(1, std::memory_order_relaxed);
        }
        if (!state_->control) send_interested(pc_);
        maybe_inject_owned_request();
    }

    void maybe_inject_owned_request()
    {
        if (state_->control || injected_ || !target_ || !piece_available_ || !unchoked_)
            return;

        suppress_picker();
        if (state_->injection_failures.load(std::memory_order_relaxed) != 0) return;

        int expected = 0;
        if (!state_->target_sent.compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            injected_ = true;
            return;
        }

        send_interested(pc_);
        send_exact_request(pc_);
        injected_ = true;

        std::lock_guard<std::mutex> lock(state_->output_mutex);
        std::cout << "P08A_OWNED_REQUEST_QUEUED peer_port=" << pc_.remote().port()
                  << " piece=0 offset=0 length=16384\n";
    }

    lt::peer_connection_handle pc_;
    std::shared_ptr<SharedState> state_;
    bool target_ = false;
    bool injected_ = false;
    bool piece_available_ = false;
    bool unchoked_ = false;
};

class TorrentProbe final : public lt::torrent_plugin
{
public:
    explicit TorrentProbe(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}

    std::shared_ptr<lt::peer_plugin>
    new_connection(lt::peer_connection_handle const& pc) override
    {
        return std::make_shared<PeerProbe>(pc, state_);
    }

private:
    std::shared_ptr<SharedState> state_;
};

int parse_port(char const* value)
{
    int port = std::stoi(value);
    if (port < 1 || port > 65535) throw std::runtime_error("invalid port");
    return port;
}

template <typename Pred>
bool wait_for(Pred&& pred, std::chrono::milliseconds timeout)
{
    auto const end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        if (pred()) return true;
        std::this_thread::sleep_for(25ms);
    }
    return pred();
}

void print_identity()
{
    std::cout << "libtorrent.compile=" << LIBTORRENT_VERSION << '\n';
    std::cout << "libtorrent.runtime=" << lt::version() << '\n';
    std::cout << "libtorrent.revision=" << LIBTORRENT_REVISION << '\n';
    std::cout << "libtorrent.version_num=" << LIBTORRENT_VERSION_NUM << '\n';
    std::cout << "libtorrent.abi=" << TORRENT_ABI_VERSION << '\n';
#ifdef TORRENT_LINKING_SHARED
    std::cout << "libtorrent.linking_shared=1\n";
#else
    std::cout << "libtorrent.linking_shared=0\n";
#endif
#ifdef TORRENT_EXPORT_EXTRA
    std::cout << "libtorrent.export_extra=1\n";
#else
    std::cout << "libtorrent.export_extra=0\n";
#endif
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        print_identity();
        if (argc == 2 && std::string(argv[1]) == "--identity") return 0;
        if (argc != 7)
        {
            std::cerr << "usage: probe <case> <torrent> <save-dir> <peer-a-port> <peer-b-port> <timeout-ms>\n";
            return 64;
        }

        std::string const case_name = argv[1];
        std::string const torrent_path = argv[2];
        std::string const save_path = argv[3];
        int const peer_a = parse_port(argv[4]);
        int const peer_b = parse_port(argv[5]);
        int const timeout_ms = std::stoi(argv[6]);
        bool const control = case_name == "P08A-CONTROL";

        std::filesystem::create_directories(save_path);

        lt::settings_pack settings;
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        settings.set_int(lt::settings_pack::connections_limit, 16);

        auto session = std::make_unique<lt::session>(settings);

        lt::add_torrent_params atp;
        atp.ti = std::make_shared<lt::torrent_info>(torrent_path);
        atp.save_path = save_path;
        atp.flags &= ~lt::torrent_flags::paused;
        atp.flags &= ~lt::torrent_flags::auto_managed;

        lt::torrent_handle handle = session->add_torrent(atp);
        auto state = std::make_shared<SharedState>(peer_b, control);
        handle.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<TorrentProbe>(state);
        });

        lt::error_code ec;
        auto const loopback = lt::make_address("127.0.0.1", ec);
        if (ec)
        {
            std::cerr << "P08A_FAIL make_address: " << ec.message() << '\n';
            return 65;
        }

        handle.connect_peer(lt::tcp::endpoint(loopback, std::uint16_t(peer_a)), {}, {});
        handle.connect_peer(lt::tcp::endpoint(loopback, std::uint16_t(peer_b)), {}, {});

        bool ok = false;
        if (control)
        {
            ok = wait_for([&] {
                return state->sent_requests.load(std::memory_order_acquire) > 0;
            }, std::chrono::milliseconds(timeout_ms));
            if (ok) std::this_thread::sleep_for(600ms);
        }
        else if (case_name == "P08A-03")
        {
            ok = wait_for([&] {
                return state->target_sent.load(std::memory_order_acquire) == 1;
            }, std::chrono::milliseconds(timeout_ms));
            if (ok) std::this_thread::sleep_for(700ms);
        }
        else
        {
            ok = wait_for([&] {
                return state->target_received.load(std::memory_order_acquire) == 1;
            }, std::chrono::milliseconds(timeout_ms));
            if (ok) std::this_thread::sleep_for(1200ms);
        }

        std::cout << "P08A_STATE peers=" << state->peers.load()
                  << " peers_with_piece=" << state->peers_with_piece.load()
                  << " sent_requests=" << state->sent_requests.load()
                  << " target_sent=" << state->target_sent.load()
                  << " target_received=" << state->target_received.load()
                  << " injection_failures=" << state->injection_failures.load()
                  << '\n';

        state->allow_disconnect.store(true, std::memory_order_release);
        session->remove_torrent(handle);
        std::this_thread::sleep_for(250ms);
        session.reset();

        if (!ok)
        {
            std::cerr << "P08A_FAIL timeout case=" << case_name << '\n';
            return 70;
        }
        if (!control && state->injection_failures.load() != 0)
        {
            std::cerr << "P08A_FAIL native_handle unavailable\n";
            return 71;
        }
        if (!control && state->peers.load() < 2)
        {
            std::cerr << "P08A_FAIL expected two peer plugins\n";
            return 72;
        }

        std::cout << "P08A_PROBE_PASS case=" << case_name << '\n';
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "P08A_EXCEPTION " << e.what() << '\n';
        return 90;
    }
}
