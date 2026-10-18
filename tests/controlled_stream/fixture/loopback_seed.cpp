// Owned-fixture seeder. No tracker list; DHT/LSD/NAT and public addresses disabled.
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc, char** argv) {
  if(argc != 3) return 2; // torrent path, payload directory
  namespace lt = libtorrent;
  lt::settings_pack s;
  s.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:49021");
  s.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  s.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
  s.set_bool(lt::settings_pack::enable_dht, false);
  s.set_bool(lt::settings_pack::enable_lsd, false);
  s.set_bool(lt::settings_pack::enable_upnp, false);
  s.set_bool(lt::settings_pack::enable_natpmp, false);
  lt::session_params sp(s);
  lt::ip_filter f;
  f.add_rule(lt::make_address("0.0.0.0"),lt::make_address("255.255.255.255"),lt::ip_filter::blocked);
  f.add_rule(lt::make_address("::"),lt::make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),lt::ip_filter::blocked);
  f.add_rule(lt::make_address("127.0.0.1"),lt::make_address("127.0.0.1"),0);
  sp.ip_filter=f;
  lt::session session(sp);
  lt::add_torrent_params p;
  p.ti=std::make_shared<lt::torrent_info>(argv[1]);
  if (!p.ti->trackers().empty()) return 3;
  p.save_path=argv[2];
  p.flags &= ~lt::torrent_flags::paused;
  p.flags &= ~lt::torrent_flags::auto_managed;
  p.flags |= lt::torrent_flags::apply_ip_filter;
  auto handle = session.add_torrent(std::move(p)); // verify real piece hashes, never seed_mode
  auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!handle.status().is_seeding) {
    auto status = handle.status();
    if (status.errc) { std::cerr << "SEEDER_ERROR disk verification failed code=" << status.errc.value() << std::endl; return 4; }
    if (std::chrono::steady_clock::now() >= ready_deadline) {
      std::cerr << "SEEDER_ERROR readiness deadline; verified pieces incomplete" << std::endl; return 5;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "SEEDER_READY verified owned payload" << std::endl;
  std::cout << "SEEDER_STARTED loopback-only port=49021 duration=180s" << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(180));
}
