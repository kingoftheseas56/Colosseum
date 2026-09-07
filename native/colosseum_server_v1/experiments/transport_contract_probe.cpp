#include "transport_contract_probe.h"

#include <libtorrent/peer_connection.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/version.hpp>

#include <fstream>

// P08A is an accepted predecessor fixture, not a production implementation.
// It supplies the real-wire selected-peer and picker-control regression that
// this packet re-runs; this file owns the P08 capability inventory and seam.
#define main p08a_fixture_main
#include "../../../artifacts/server1/P08A/probe-src/exact_block_probe.cpp"
#undef main

namespace colosseum::server1::p08 {

capability_report inspect_frozen_libtorrent_surface()
{
    // These references deliberately compile against the locked 2.0 headers.
    // The member calls are the candidate seam; the report distinguishes API
    // availability from controls proven on the wire by the fixture run.
    using add_request_member = decltype(&libtorrent::peer_connection::add_request);
    using cancel_request_member = decltype(&libtorrent::peer_connection::cancel_request);
    using send_requests_member = decltype(&libtorrent::peer_connection::send_block_requests);
    using torrent_deadline_member = decltype(&libtorrent::torrent_handle::set_piece_deadline);
    using session_settings_member = void (libtorrent::session_handle::*)(libtorrent::settings_pack const&);
    using peer_stats_member = decltype(&libtorrent::peer_connection::statistics);
    (void)sizeof(add_request_member);
    (void)sizeof(cancel_request_member);
    (void)sizeof(send_requests_member);
    (void)sizeof(torrent_deadline_member);
    (void)sizeof(session_settings_member);
    (void)sizeof(peer_stats_member);

    return {
        "version-specific internal header access",
        lt::version(),
        "6e1587799",
        true,  // P08A real wire: exact piece/offset/length on selected peer.
        true,  // P08A real wire: unowned picker requests suppressed.
        false, // P08-A must not infer reservation transfer from cancellation.
        false, // Requires a distinct multi-block, pre-hash fixture.
        true,  // Public metadata/discovery settings compile and are exercised by setup.
        false, // Statistics API compiles; honest peer-stat wire receipt is not yet captured.
        true,  // torrent_handle deadline/priority is torrent-scoped.
        true,  // session::apply_settings is session-scoped.
        false  // Choke/interest was not isolated as a graded P08 wire case.
    };
}

int write_capability_matrix(std::string const& path, capability_report const& r)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) return 1;
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-native-capability-matrix/v1\",\n"
        << "  \"worker_id\": \"P08-A\",\n"
        << "  \"dependency\": {\"version\": \"" << r.dependency_version
        << "\", \"revision\": \"" << r.dependency_revision << "\"},\n"
        << "  \"seam\": {\"classification\": \"" << r.seam_classification
        << "\", \"exact_block\": \"peer_connection::add_request(piece_block) + send_block_requests()\", \"hotswap\": \"peer_connection::cancel_request(piece_block) + add_request(piece_block)\"},\n"
        << "  \"controls\": {\n"
        << "    \"selected_peer_exact_block_wire\": " << (r.selected_peer_exact_block_wire ? "true" : "false") << ",\n"
        << "    \"picker_suppression_wire\": " << (r.picker_suppression_wire ? "true" : "false") << ",\n"
        << "    \"reservation_hotswap_wire\": " << (r.reservation_hotswap_wire ? "true" : "false") << ",\n"
        << "    \"partial_delivery_before_piece_verification\": " << (r.partial_delivery_before_piece_verification ? "true" : "false") << ",\n"
        << "    \"metadata_discovery_control\": " << (r.metadata_discovery_control ? "true" : "false") << ",\n"
        << "    \"honest_peer_stats\": " << (r.honest_peer_stats ? "true" : "false") << ",\n"
        << "    \"per_engine_settings\": " << (r.per_engine_settings ? "true" : "false") << ",\n"
        << "    \"session_global_settings\": " << (r.session_global_settings ? "true" : "false") << ",\n"
        << "    \"choke_interest_wire\": " << (r.choke_interest_observable ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"interface_statement\": \"No production interface changed; G-NATIVE is blocked until every mandatory control has an isolated real-wire receipt.\"\n"
        << "}\n";
    return 0;
}

} // namespace colosseum::server1::p08

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--capabilities") {
        return colosseum::server1::p08::write_capability_matrix(
            argv[2], colosseum::server1::p08::inspect_frozen_libtorrent_surface());
    }
    return p08a_fixture_main(argc, argv);
}
