#pragma once

#include <string>

namespace colosseum::server1::p08 {

struct capability_report {
    std::string seam_classification;
    std::string dependency_version;
    std::string dependency_revision;
    bool selected_peer_exact_block_wire;
    bool picker_suppression_wire;
    bool reservation_hotswap_wire;
    bool partial_delivery_before_piece_verification;
    bool metadata_discovery_control;
    bool honest_peer_stats;
    bool per_engine_settings;
    bool session_global_settings;
    bool choke_interest_observable;
};

capability_report inspect_frozen_libtorrent_surface();
int write_capability_matrix(std::string const& path, capability_report const& report);

} // namespace colosseum::server1::p08
