#include "torrent/protocol_backend.hpp"
#include "torrent/libtorrent_stream_bridge.hpp"
#include "cache/disk_cache_manager.hpp"
#include "storage/atomic_file.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/client_data.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>

#include <sstream>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

namespace nuvio::torrent {
namespace {

constexpr std::size_t maximum_session_state_size = 4 * 1024 * 1024;
constexpr std::size_t maximum_resume_state_size = 16 * 1024 * 1024;
constexpr std::size_t maximum_pending_disk_reclaims = 256;
constexpr int peer_connect_timeout_seconds = 3;
constexpr int peer_handshake_timeout_seconds = 20;
constexpr int peer_reconnect_floor_seconds = 4;
constexpr int piece_request_timeout_seconds = 4;
constexpr int request_queue_seconds = 1;
constexpr int maximum_outstanding_requests_per_peer = 64;
constexpr int idle_session_tick_milliseconds = 500;
constexpr int streaming_session_tick_milliseconds = 100;

struct TorrentProfileSettings {
    int connection_limit;
    int connection_attempts_per_second;
    int connect_boost;
};

TorrentProfileSettings torrent_profile_settings(
    const nuvio_engine_torrent_profile profile
) {
    switch (profile) {
    case NUVIO_ENGINE_TORRENT_PROFILE_SOFT:
        return {35, 35, 35};
    case NUVIO_ENGINE_TORRENT_PROFILE_BALANCED:
        return {120, 120, 120};
    case NUVIO_ENGINE_TORRENT_PROFILE_FAST:
        return {200, 200, 200};
    }
    return {120, 120, 120};
}

enum class MetadataSource {
    live,
    restored,
    cold,
    provided,
};

constexpr std::string_view metadata_source_name(const MetadataSource source) {
    switch (source) {
    case MetadataSource::live:
        return "live";
    case MetadataSource::restored:
        return "restored";
    case MetadataSource::cold:
        return "cold";
    case MetadataSource::provided:
        return "provided";
    }
    return "cold";
}

static_assert(metadata_source_name(MetadataSource::cold) == "cold");

int bounded_rate(const std::uint64_t value) {
    return static_cast<int>(std::min(
        value,
        static_cast<std::uint64_t>(std::numeric_limits<int>::max())
    ));
}

// Synthetic fixture tests set NUVIO_ENGINE_DISABLE_DISCOVERY so their torrent
// sessions can never bootstrap public DHT/router, LSD, UPnP or NAT-PMP
// traffic; production builds leave it unset and keep internet peer transport.
bool discovery_disabled_by_environment() {
    const char* flag = std::getenv("NUVIO_ENGINE_DISABLE_DISCOVERY");
    return flag != nullptr && *flag != '\0';
}

lt::settings_pack make_settings(const ProtocolBackendConfig& config) {
    lt::settings_pack settings;
    const auto profile = torrent_profile_settings(config.torrent_profile);
    settings.set_str(lt::settings_pack::user_agent, "Nuvio Engine/0.1.5");
    const bool discovery_disabled = discovery_disabled_by_environment();
    // The synthetic fixture must not create an all-interface listener:
    // otherwise Windows Firewall prompts even though DHT/LSD are disabled.
    // Production peer transport retains its existing listen interfaces.
    settings.set_str(
        lt::settings_pack::listen_interfaces,
        discovery_disabled ? "127.0.0.1:0" : "0.0.0.0:0,[::]:0"
    );
    settings.set_bool(lt::settings_pack::enable_dht, !discovery_disabled);
#if defined(__ANDROID__)
    // Android profile: local peer discovery and NAT port mapping stay off so
    // the engine never announces itself beyond explicitly configured trackers
    // and DHT. Internet peer transport itself is unchanged.
    constexpr bool local_mapping_enabled = false;
#else
    const bool local_mapping_enabled = !discovery_disabled;
#endif
    settings.set_bool(lt::settings_pack::enable_lsd, local_mapping_enabled);
    settings.set_bool(lt::settings_pack::enable_upnp, local_mapping_enabled);
    settings.set_bool(lt::settings_pack::enable_natpmp, local_mapping_enabled);
    if (discovery_disabled) {
        // Task-only isolation hardening: with discovery disabled the session
        // must also carry no DHT bootstrap targets and accept no PEX peers,
        // so even a hypothetical later enable_dht flip could not reach the
        // public network through this session.
        settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
        settings.set_int(lt::settings_pack::max_pex_peers, 0);
    }
    settings.set_bool(lt::settings_pack::announce_to_all_tiers, true);
    settings.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    settings.set_bool(lt::settings_pack::close_redundant_connections, false);
    settings.set_int(lt::settings_pack::connections_limit, profile.connection_limit);
    settings.set_int(lt::settings_pack::peer_connect_timeout, peer_connect_timeout_seconds);
    settings.set_int(lt::settings_pack::handshake_timeout, peer_handshake_timeout_seconds);
    settings.set_int(lt::settings_pack::min_reconnect_time, peer_reconnect_floor_seconds);
    settings.set_int(
        lt::settings_pack::connection_speed,
        profile.connection_attempts_per_second
    );
    settings.set_int(lt::settings_pack::torrent_connect_boost, profile.connect_boost);
    settings.set_int(lt::settings_pack::request_timeout, piece_request_timeout_seconds);
    settings.set_int(lt::settings_pack::request_queue_time, request_queue_seconds);
    settings.set_int(lt::settings_pack::tick_interval, idle_session_tick_milliseconds);
    settings.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    settings.set_int(
        lt::settings_pack::max_out_request_queue,
        maximum_outstanding_requests_per_peer
    );
    settings.set_int(lt::settings_pack::whole_pieces_threshold, 0);
    if (!config.tls_ca_bundle_path.empty()) {
        settings.set_str(
            lt::settings_pack::nuvio_ssl_ca_bundle,
            config.tls_ca_bundle_path
        );
    }
    settings.set_int(
        lt::settings_pack::alert_mask,
        lt::alert_category::error |
            lt::alert_category::peer |
            lt::alert_category::storage |
            lt::alert_category::tracker |
            lt::alert_category::connect |
            lt::alert_category::status |
            lt::alert_category::performance_warning |
            lt::alert_category::dht |
            lt::alert_category::file_progress |
            lt::alert_category::block_progress
    );
    if (config.upload_mode == NUVIO_ENGINE_UPLOAD_LIMITED) {
        settings.set_int(
            lt::settings_pack::upload_rate_limit,
            bounded_rate(config.upload_limit_bytes_per_second)
        );
    }
    if (config.upload_mode == NUVIO_ENGINE_UPLOAD_DISABLED) {
        settings.set_int(lt::settings_pack::unchoke_slots_limit, 0);
    }
    return settings;
}

std::filesystem::path state_root(const ProtocolBackendConfig& config) {
    return std::filesystem::path(config.state_directory) / "nuvio-engine-state";
}

struct SessionBootstrap {
    lt::session_params params;
    std::string diagnostic;
};

SessionBootstrap make_session_bootstrap(const ProtocolBackendConfig& config) {
    SessionBootstrap bootstrap{lt::session_params(make_settings(config)), {}};
#if !defined(_WIN32)
    bootstrap.params.disk_io_constructor = lt::posix_disk_io_constructor;
#endif
    if (!discovery_disabled_by_environment()) {
    try {
        const auto state = storage::read_bounded_file(
            state_root(config) / "session.dht",
            maximum_session_state_size
        );
        if (state.has_value() && !state->empty()) {
            auto persisted = lt::read_session_params(
                *state,
                lt::session::save_dht_state
            );
            bootstrap.params.dht_state = std::move(persisted.dht_state);
        }
    } catch (const std::exception& error) {
        bootstrap.diagnostic = std::string("ignored persisted DHT state: ") + error.what();
    }
    }
    return bootstrap;
}

bool is_canonical_torrent_id(const std::string& value) {
    if (value.size() != 40 && value.size() != 64) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char character) {
        return std::isxdigit(character) != 0 &&
            character == static_cast<unsigned char>(std::tolower(character));
    });
}

std::uint64_t nonnegative(const std::int64_t value) {
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::uint64_t nonnegative(const int value) {
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

void saturating_add(std::uint64_t& total, const std::uint64_t value) {
    total = value > std::numeric_limits<std::uint64_t>::max() - total
        ? std::numeric_limits<std::uint64_t>::max()
        : total + value;
}

void saturating_increment(std::uint64_t& value) {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

std::uint32_t bounded_count(const std::uint64_t value) {
    return static_cast<std::uint32_t>(std::min(
        value,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
    ));
}

class LibtorrentBackend final : public ProtocolBackend {
public:
    explicit LibtorrentBackend(const ProtocolBackendConfig& config)
        : LibtorrentBackend(config, make_session_bootstrap(config)) {
    }

    // Called only on EngineRuntime's owning worker thread, never on a copied
    // construction pack. The session handle's getters are current-session reads.
    [[nodiscard]] std::string session_diagnostics() override {
        const auto effective = session_.get_settings();
        const auto count = session_.get_torrents().size();
        const bool dht = session_.is_dht_running();
        const bool listening = session_.is_listening();
        const auto listen = effective.get_str(lt::settings_pack::listen_interfaces);
        const auto bootstrap = effective.get_str(lt::settings_pack::dht_bootstrap_nodes);
        const auto pex = effective.get_int(lt::settings_pack::max_pex_peers);
        const bool safe = isolated_session_ &&
            !effective.get_bool(lt::settings_pack::enable_dht) &&
            !effective.get_bool(lt::settings_pack::enable_lsd) &&
            !effective.get_bool(lt::settings_pack::enable_upnp) &&
            !effective.get_bool(lt::settings_pack::enable_natpmp) &&
            listen == "127.0.0.1:0" &&
            bootstrap.empty() && pex == 0 && !dht && listening;
        std::ostringstream out;
        out << "isolation_pass=" << (safe ? "true" : "false")
            << "; session_instance=" << static_cast<const void*>(&session_)
            << "; torrent_count=" << count
            << "; enable_dht=" << (effective.get_bool(lt::settings_pack::enable_dht) ? "true" : "false")
            << "; enable_lsd=" << (effective.get_bool(lt::settings_pack::enable_lsd) ? "true" : "false")
            << "; enable_upnp=" << (effective.get_bool(lt::settings_pack::enable_upnp) ? "true" : "false")
            << "; enable_natpmp=" << (effective.get_bool(lt::settings_pack::enable_natpmp) ? "true" : "false")
            << "; listen_interfaces=\"" << listen
            << "\"; dht_bootstrap_nodes=\"" << bootstrap
            << "\"; max_pex_peers=" << pex
            << "; live_dht_running=" << (dht ? "true" : "false")
            << "; live_listening=" << (listening ? "true" : "false")
            << "; live_listen_port=" << session_.listen_port()
            << "; libtorrent=" << LIBTORRENT_VERSION;
        return out.str();
    }

    void add_torrent(
        const std::uint64_t request_id,
        TorrentInput input,
        const std::string&
    ) override {
        if (isolated_session_) {
            if (session_diagnostics().find("isolation_pass=true") == std::string::npos) {
                throw std::runtime_error("fixture admission denied: current session is not isolated");
            }
            if (input.type != TorrentInputType::torrent_data) {
                throw std::runtime_error("fixture admission denied: metadata file required");
            }
        }
        const auto metadata_started_at = std::chrono::steady_clock::now();
        const auto source_type = input.type;
        auto source_params = input.type == TorrentInputType::magnet
            ? lt::parse_magnet_uri(input.magnet_uri)
            : lt::load_torrent_buffer(input.torrent_data);
        const auto id = torrent_id(source_params.info_hashes);
        if (!is_canonical_torrent_id(id)) {
            throw std::runtime_error("torrent source has no canonical info hash");
        }
        if (isolated_session_) {
            if (!source_params.ti || !source_params.ti->priv() ||
                source_params.trackers.size() != 1 ||
                source_params.trackers.front() != "http://127.0.0.1:51413/announce" ||
                !source_params.http_seeds.empty() || !source_params.url_seeds.empty() ||
                !source_params.dht_nodes.empty() || !source_params.peers.empty() ||
                std::filesystem::exists(resume_path(id))) {
                throw std::runtime_error("fixture admission denied: unsafe source endpoints or saved state");
            }
        }
        focus_torrent(id);

        const auto existing = handles_.find(id);
        if (existing != handles_.end() && existing->second.is_valid()) {
            activate_torrent(id, existing->second);
            auto* context = existing->second.userdata().get<RequestContext>();
            if (context != nullptr) {
                context->request_id = request_id;
                context->metadata_started_at = metadata_started_at;
            }
            queued_events_.push_back({
                BackendEventType::torrent_added,
                request_id,
                id,
                {},
                {},
            });
            if (existing->second.status().has_metadata) {
                RequestContext request_context{};
                request_context.request_id = request_id;
                request_context.torrent_id = id;
                request_context.metadata_source = MetadataSource::live;
                request_context.metadata_started_at = metadata_started_at;
                queued_events_.push_back(metadata_event(
                    existing->second,
                    &request_context,
                    MetadataSource::live
                ));
            }
            return;
        }

        std::vector<std::string> source_trackers(
            source_params.trackers.begin(),
            source_params.trackers.end()
        );
        const auto provided_metadata = source_params.ti;
        auto params = std::move(source_params);
        auto metadata_source = source_type == TorrentInputType::torrent_data
            ? MetadataSource::provided
            : MetadataSource::cold;
        if (auto resume = load_resume_state(id); resume.has_value()) {
            params = std::move(*resume);
            if (params.ti) {
                metadata_source = MetadataSource::restored;
            } else if (provided_metadata) {
                params.ti = provided_metadata;
                metadata_source = MetadataSource::provided;
            } else {
                metadata_source = MetadataSource::cold;
            }
            for (const auto& tracker : source_trackers) {
                if (std::ranges::find(params.trackers, tracker) == params.trackers.end()) {
                    params.trackers.push_back(tracker);
                }
            }
        }
        params.save_path = prepare_payload_path(id).string();
        params.flags |= lt::torrent_flags::upload_mode;
        params.flags &= ~lt::torrent_flags::auto_managed;
        params.flags &= ~lt::torrent_flags::paused;
        auto context = std::make_unique<RequestContext>();
        context->request_id = request_id;
        context->torrent_id = id;
        context->metadata_source = metadata_source;
        context->metadata_started_at = metadata_started_at;
        touch_disk_cache(id);
        params.userdata = context.get();
        auto* context_key = context.get();
        pending_.emplace(context_key, std::move(context));
        try {
            session_.async_add_torrent(std::move(params));
        } catch (...) {
            pending_.erase(context_key);
            throw;
        }
    }

    void prepare_file(
        const std::uint64_t request_id,
        const std::string& id,
        const std::uint32_t file_index,
        TorrentFileInfo file
    ) override {
        const auto torrent = handles_.find(id);
        if (torrent == handles_.end() || !torrent->second.is_valid()) {
            throw std::runtime_error("torrent not found");
        }
        focus_torrent(id);
        activate_torrent(id, torrent->second);
        touch_disk_cache(id);
        if (pending_prepares_.contains(id)) {
            throw std::runtime_error("stream preparation already pending");
        }
        const auto metadata = torrent->second.torrent_file();
        if (!metadata || file_index >= static_cast<std::uint32_t>(metadata->num_files())) {
            throw std::runtime_error("selected torrent file is unavailable");
        }
        PendingPrepare prepare{request_id, file_index, std::move(file)};
        if (torrent->second.status().is_seeding) {
            queued_events_.push_back(prepared_event(torrent->second, id, prepare));
            return;
        }
        pending_prepares_.emplace(id, std::move(prepare));
        try {
            std::vector<lt::download_priority_t> priorities(
                static_cast<std::size_t>(metadata->num_files()),
                lt::dont_download
            );
            priorities[static_cast<std::size_t>(file_index)] = lt::default_priority;
            torrent->second.prioritize_files(priorities);
        } catch (...) {
            pending_prepares_.erase(id);
            throw;
        }
    }

    void remove_torrent(
        const std::uint64_t request_id,
        const std::string& id
    ) override {
        const auto torrent = handles_.find(id);
        if (torrent == handles_.end() || !torrent->second.is_valid()) {
            throw std::runtime_error("torrent not found");
        }
        if (pending_removals_.contains(id)) {
            throw std::runtime_error("torrent removal already pending");
        }
        auto* context = torrent->second.userdata().get<RequestContext>();
        if (context != nullptr) {
            context->explicitly_removed = true;
        }
        pending_cache_unloads_.erase(id);
        const auto cache_removal_pending = pending_cache_removals_.erase(id) > 0;
        pending_removals_.emplace(id, request_id);
        try {
            if (!cache_removal_pending) {
                session_.remove_torrent(torrent->second);
            }
        } catch (...) {
            pending_removals_.erase(id);
            if (context != nullptr) {
                context->explicitly_removed = false;
            }
            throw;
        }
        pending_resume_saves_.erase(context);
        touch_disk_cache(id);
        warm_since_.erase(id);
        quiesced_torrents_.erase(id);
        stream_bridge_->remove_torrent(id);
        try {
            storage::remove_file_if_present(resume_path(id));
        } catch (const std::exception& error) {
            queued_events_.push_back({
                BackendEventType::torrent_error,
                request_id,
                id,
                std::string("failed to remove persisted torrent state: ") + error.what(),
                {},
            });
        }
        const auto prepare = pending_prepares_.find(id);
        if (prepare != pending_prepares_.end()) {
            queued_events_.push_back({
                BackendEventType::torrent_error,
                prepare->second.request_id,
                id,
                "stream preparation cancelled by torrent removal",
                {},
            });
            pending_prepares_.erase(prepare);
        }
    }

    void stop_stream(
        const std::uint64_t request_id,
        const std::string& stream_id
    ) override {
        auto torrent_id = stream_bridge_->stop_stream(stream_id);
        if (!torrent_id.empty()) {
            mark_warm(torrent_id);
            const auto torrent = handles_.find(torrent_id);
            if (torrent != handles_.end() && torrent->second.is_valid() &&
                !stream_bridge_->has_stream_for_torrent(torrent_id)) {
                try {
                    torrent->second.set_flags(lt::torrent_flags::upload_mode);
                } catch (...) {
                }
            }
        }
        BackendEvent event{
            BackendEventType::stream_stopped,
            request_id,
            std::move(torrent_id),
            {},
            {},
        };
        event.stream_id = stream_id;
        queued_events_.push_back(std::move(event));
    }

    void reclaim_disk_cache(
        const std::uint64_t request_id,
        const std::uint64_t target_bytes
    ) override {
        if (pending_disk_reclaims_.size() >= maximum_pending_disk_reclaims) {
            throw std::runtime_error("too many pending disk cache reclaim requests");
        }
        pending_disk_reclaims_.push_back({request_id, target_bytes});
    }

    void set_wakeup(std::function<void()> wake) override {
        session_.set_alert_notify(wake);
        stream_bridge_->set_wakeup(std::move(wake));
    }

    void shutdown() override {
        if (shutting_down_) {
            return;
        }
        shutting_down_ = true;
        session_.set_alert_notify({});
        stream_bridge_->shutdown();
        for (const auto& [id, handle] : handles_) {
            if (!pending_removals_.contains(id)) {
                request_resume_save(handle, lt::torrent_handle::save_info_dict);
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!pending_resume_saves_.empty() &&
               std::chrono::steady_clock::now() < deadline) {
            session_.wait_for_alert(std::chrono::milliseconds(100));
            static_cast<void>(pop_events());
        }
        save_session_state();
    }

    std::vector<BackendEvent> pop_events() override {
        stream_bridge_->poll();
        for (auto& expired : stream_bridge_->pop_expired_streams()) {
            mark_warm(expired.torrent_id);
            const auto torrent = handles_.find(expired.torrent_id);
            if (torrent != handles_.end() && torrent->second.is_valid() &&
                !stream_bridge_->has_stream_for_torrent(expired.torrent_id)) {
                try {
                    torrent->second.set_flags(lt::torrent_flags::upload_mode);
                } catch (...) {
                }
            }
            BackendEvent event{
                BackendEventType::stream_stopped,
                0,
                std::move(expired.torrent_id),
                "stream expired after inactivity",
                {},
            };
            event.stream_id = std::move(expired.stream_id);
            queued_events_.push_back(std::move(event));
        }
        std::vector<lt::alert*> alerts;
        session_.pop_alerts(&alerts);
        std::vector<BackendEvent> events;
        events.swap(queued_events_);
        for (const auto* alert : alerts) {
            if (const auto* added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
                handle_added(*added, events);
            } else if (const auto* block = lt::alert_cast<lt::block_finished_alert>(alert)) {
                stream_bridge_->handle_block_finished(*block);
            } else if (const auto* piece = lt::alert_cast<lt::read_piece_alert>(alert)) {
                stream_bridge_->handle_read_piece(*piece);
            } else if (const auto* failed_hash = lt::alert_cast<lt::hash_failed_alert>(alert)) {
                stream_bridge_->handle_hash_failed(*failed_hash);
            } else if (const auto* metadata = lt::alert_cast<lt::metadata_received_alert>(alert)) {
                handle_metadata(*metadata, events);
            } else if (const auto* error = lt::alert_cast<lt::torrent_error_alert>(alert)) {
                handle_error(*error, events);
            } else if (const auto* file_error = lt::alert_cast<lt::file_error_alert>(alert)) {
                handle_file_error(*file_error, events);
            } else if (const auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
                handle_resume_saved(*saved, events);
            } else if (const auto* failed =
                           lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
                handle_resume_save_failed(*failed, events);
            } else if (const auto* finished = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
                handle_torrent_finished(*finished);
                request_resume_save(
                    finished->handle,
                    lt::torrent_handle::only_if_modified |
                        lt::torrent_handle::save_info_dict
                );
            } else if (const auto* priority = lt::alert_cast<lt::file_prio_alert>(alert)) {
                handle_file_priority(*priority, events);
            } else if (const auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert)) {
                handle_removed(*removed, events);
            } else if (const auto* update = lt::alert_cast<lt::state_update_alert>(alert)) {
                handle_state_update(*update);
            } else if (const auto* peers = lt::alert_cast<lt::peer_info_alert>(alert)) {
                handle_peer_info(*peers);
            } else if (const auto* connected = lt::alert_cast<lt::peer_connect_alert>(alert)) {
                handle_peer_connect(*connected);
            } else if (const auto* disconnected =
                           lt::alert_cast<lt::peer_disconnected_alert>(alert)) {
                handle_peer_disconnected(*disconnected);
            } else if (const auto* tracker_reply =
                           lt::alert_cast<lt::tracker_reply_alert>(alert)) {
                handle_tracker_reply(*tracker_reply);
            } else if (const auto* tracker_error =
                           lt::alert_cast<lt::tracker_error_alert>(alert)) {
                handle_tracker_error(*tracker_error);
            } else if (const auto* dht = lt::alert_cast<lt::dht_reply_alert>(alert)) {
                handle_dht_reply(*dht);
            } else if (lt::alert_cast<lt::dht_bootstrap_alert>(alert)) {
                dht_ready_ = true;
                announce_to_dht();
            }
        }
        run_periodic_checkpoints(events);
        run_warm_quiescence();
        process_disk_reclaims(events);
        run_disk_cache_enforcement(events);
        request_telemetry_update();
        update_session_tick();
        return events;
    }

    BackendStats statistics() override {
        BackendStats result{};
        result.active_torrents = bounded_count(handles_.size());
        std::uint64_t peers = 0;
        std::uint64_t seeds = 0;
        std::uint64_t known_peers = 0;
        std::uint64_t connect_candidates = 0;
        std::uint64_t interested_peers = 0;
        std::uint64_t unchoked_peers = 0;
        std::uint64_t downloading_peers = 0;
        std::uint64_t snubbed_peers = 0;
        std::uint64_t pending_block_requests = 0;
        std::uint64_t target_block_requests = 0;
        std::uint64_t timed_out_block_requests = 0;
        std::uint64_t connecting_peers = 0;
        std::uint64_t handshaking_peers = 0;
        std::uint64_t target_piece_peers = 0;
        std::uint64_t target_piece_unchoked_peers = 0;
        std::uint64_t target_piece_downloading_peers = 0;
        std::uint64_t off_target_downloading_peers = 0;
        std::uint64_t tracker_reply_events = 0;
        std::uint64_t tracker_error_events = 0;
        std::uint64_t dht_reply_events = 0;
        for (const auto& [id, telemetry] : telemetry_) {
            static_cast<void>(id);
            peers = std::min(
                peers + static_cast<std::uint64_t>(telemetry.connected_peers),
                static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
            );
            seeds = std::min(
                seeds + static_cast<std::uint64_t>(telemetry.connected_seeds),
                static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
            );
            saturating_add(known_peers, telemetry.known_peers);
            saturating_add(connect_candidates, telemetry.connect_candidates);
            saturating_add(interested_peers, telemetry.interested_peers);
            saturating_add(unchoked_peers, telemetry.unchoked_peers);
            saturating_add(downloading_peers, telemetry.downloading_peers);
            saturating_add(snubbed_peers, telemetry.snubbed_peers);
            saturating_add(pending_block_requests, telemetry.pending_block_requests);
            saturating_add(target_block_requests, telemetry.target_block_requests);
            saturating_add(timed_out_block_requests, telemetry.timed_out_block_requests);
            saturating_add(connecting_peers, telemetry.connecting_peers);
            saturating_add(handshaking_peers, telemetry.handshaking_peers);
            saturating_add(target_piece_peers, telemetry.target_piece_peers);
            saturating_add(
                target_piece_unchoked_peers,
                telemetry.target_piece_unchoked_peers
            );
            saturating_add(
                target_piece_downloading_peers,
                telemetry.target_piece_downloading_peers
            );
            saturating_add(
                off_target_downloading_peers,
                telemetry.off_target_downloading_peers
            );
            saturating_add(tracker_reply_events, telemetry.tracker_reply_events);
            saturating_add(tracker_error_events, telemetry.tracker_error_events);
            saturating_add(dht_reply_events, telemetry.dht_reply_events);
            saturating_add(result.download_rate_bytes_per_second, telemetry.download_rate);
            saturating_add(result.upload_rate_bytes_per_second, telemetry.upload_rate);
            saturating_add(result.total_payload_download_bytes, telemetry.total_download);
            saturating_add(result.total_payload_upload_bytes, telemetry.total_upload);
            saturating_add(result.tracker_peers_returned, telemetry.tracker_peers_returned);
            saturating_add(result.dht_peers_returned, telemetry.dht_peers_returned);
            saturating_add(result.peer_connect_events, telemetry.peer_connect_events);
            saturating_add(result.peer_disconnect_events, telemetry.peer_disconnect_events);
            saturating_add(
                result.peer_disconnect_timeouts,
                telemetry.peer_disconnect_timeouts
            );
            saturating_add(
                result.peer_disconnect_connect_failures,
                telemetry.peer_disconnect_connect_failures
            );
            saturating_add(
                result.peer_disconnect_redundant,
                telemetry.peer_disconnect_redundant
            );
            saturating_add(
                result.peer_disconnect_turnover,
                telemetry.peer_disconnect_turnover
            );
            saturating_add(
                result.peer_disconnect_other,
                telemetry.peer_disconnect_other
            );
            saturating_add(result.torrent_finished_events, telemetry.torrent_finished_events);
        }
        result.connected_peers = bounded_count(peers);
        result.connected_seeds = bounded_count(seeds);
        result.known_peers = bounded_count(known_peers);
        result.connect_candidates = bounded_count(connect_candidates);
        result.interested_peers = bounded_count(interested_peers);
        result.unchoked_peers = bounded_count(unchoked_peers);
        result.downloading_peers = bounded_count(downloading_peers);
        result.snubbed_peers = bounded_count(snubbed_peers);
        result.pending_block_requests = bounded_count(pending_block_requests);
        result.target_block_requests = bounded_count(target_block_requests);
        result.timed_out_block_requests = bounded_count(timed_out_block_requests);
        result.connecting_peers = bounded_count(connecting_peers);
        result.handshaking_peers = bounded_count(handshaking_peers);
        result.target_piece_peers = bounded_count(target_piece_peers);
        result.target_piece_unchoked_peers = bounded_count(target_piece_unchoked_peers);
        result.target_piece_downloading_peers = bounded_count(target_piece_downloading_peers);
        result.off_target_downloading_peers = bounded_count(off_target_downloading_peers);
        result.tracker_reply_events = bounded_count(tracker_reply_events);
        result.tracker_error_events = bounded_count(tracker_error_events);
        result.dht_reply_events = bounded_count(dht_reply_events);
        const auto stream = stream_bridge_->statistics();
        result.active_streams = stream.active_streams;
        result.active_http_requests = stream.active_http_requests;
        result.pending_piece_reads = stream.pending_piece_reads;
        result.memory_cache_capacity_bytes = stream.cache.capacity_bytes;
        result.memory_cache_used_bytes = stream.cache.used_bytes;
        result.memory_cache_hits = stream.cache.hits;
        result.memory_cache_misses = stream.cache.misses;
        result.memory_cache_evictions = stream.cache.evictions;
        result.memory_cache_entries = stream.cache.entries;
        result.streams = stream.streams;
        result.warm_torrents = bounded_count(warm_since_.size());
        result.quiesced_torrents = bounded_count(quiesced_torrents_.size());
        const auto& disk = disk_cache_.stats();
        result.disk_cache_capacity_bytes = disk.capacity_bytes;
        result.disk_cache_used_bytes = disk.used_bytes;
        result.disk_cache_protected_bytes = disk.protected_bytes;
        result.disk_cache_evictions = disk.evictions;
        result.disk_cache_reclaimed_bytes = disk.reclaimed_bytes;
        result.disk_cache_over_budget = disk.over_budget;
        return result;
    }

private:
    struct RequestContext {
        std::uint64_t request_id = 0;
        std::string torrent_id;
        bool metadata_emitted = false;
        bool explicitly_removed = false;
        MetadataSource metadata_source = MetadataSource::cold;
        std::chrono::steady_clock::time_point metadata_started_at{};
    };

    struct PendingPrepare {
        std::uint64_t request_id;
        std::uint32_t file_index;
        TorrentFileInfo file;
    };

    struct TorrentTelemetry {
        std::uint32_t connected_peers = 0;
        std::uint32_t connected_seeds = 0;
        std::uint32_t known_peers = 0;
        std::uint32_t connect_candidates = 0;
        std::uint32_t interested_peers = 0;
        std::uint32_t unchoked_peers = 0;
        std::uint32_t downloading_peers = 0;
        std::uint32_t snubbed_peers = 0;
        std::uint32_t pending_block_requests = 0;
        std::uint32_t target_block_requests = 0;
        std::uint32_t timed_out_block_requests = 0;
        std::uint32_t connecting_peers = 0;
        std::uint32_t handshaking_peers = 0;
        std::uint32_t target_piece_peers = 0;
        std::uint32_t target_piece_unchoked_peers = 0;
        std::uint32_t target_piece_downloading_peers = 0;
        std::uint32_t off_target_downloading_peers = 0;
        std::uint32_t tracker_reply_events = 0;
        std::uint32_t tracker_error_events = 0;
        std::uint32_t dht_reply_events = 0;
        std::uint64_t download_rate = 0;
        std::uint64_t upload_rate = 0;
        std::uint64_t total_download = 0;
        std::uint64_t total_upload = 0;
        std::uint64_t tracker_peers_returned = 0;
        std::uint64_t dht_peers_returned = 0;
        std::uint64_t peer_connect_events = 0;
        std::uint64_t peer_disconnect_events = 0;
        std::uint64_t peer_disconnect_timeouts = 0;
        std::uint64_t peer_disconnect_connect_failures = 0;
        std::uint64_t peer_disconnect_redundant = 0;
        std::uint64_t peer_disconnect_turnover = 0;
        std::uint64_t peer_disconnect_other = 0;
        std::uint64_t torrent_finished_events = 0;
    };

    struct PendingDiskReclaim {
        std::uint64_t request_id = 0;
        std::uint64_t target_bytes = 0;
    };

    LibtorrentBackend(
        const ProtocolBackendConfig& config,
        SessionBootstrap bootstrap
    )
        : state_root_(state_root(config)),
          resume_directory_(state_root_ / "resume"),
          session_state_path_(state_root_ / "session.dht"),
          payload_directory_(
              std::filesystem::path(config.download_directory) / "payload"
          ),
          warm_torrent_timeout_(config.warm_torrent_timeout_milliseconds),
          disk_cache_(payload_directory_, config.disk_cache_capacity_bytes),
          isolated_session_(discovery_disabled_by_environment()),
          session_(std::move(bootstrap.params)) {
        if (!bootstrap.diagnostic.empty()) {
            queued_events_.push_back({
                BackendEventType::torrent_error,
                0,
                {},
                std::move(bootstrap.diagnostic),
                {},
            });
        }
        stream_bridge_ = std::make_unique<LibtorrentStreamBridge>(
            config.listen_port,
            config.memory_cache_capacity_bytes,
            config.disk_cache_capacity_bytes,
            std::chrono::milliseconds(config.stream_inactivity_timeout_milliseconds)
        );
    }

    static std::string torrent_id(const lt::info_hash_t& hashes) {
        if (hashes.has_v1()) {
            return lt::aux::to_hex(hashes.v1);
        }
        if (hashes.has_v2()) {
            return lt::aux::to_hex(hashes.v2);
        }
        return {};
    }

    static std::string torrent_id(const lt::torrent_handle& handle) {
        return torrent_id(handle.info_hashes());
    }

    [[nodiscard]] std::filesystem::path resume_path(const std::string& id) const {
        return resume_directory_ / (id + ".resume");
    }

    [[nodiscard]] std::filesystem::path prepare_payload_path(
        const std::string& id
    ) const {
        if (!is_canonical_torrent_id(id)) {
            throw std::runtime_error("payload path requires a canonical info hash");
        }
        std::error_code error;
        std::filesystem::create_directories(payload_directory_, error);
        if (error) {
            throw std::system_error(error, "create torrent payload root");
        }
        const auto root_status = std::filesystem::symlink_status(
            payload_directory_,
            error
        );
        if (error) {
            throw std::system_error(error, "inspect torrent payload root");
        }
        if (root_status.type() != std::filesystem::file_type::directory) {
            throw std::runtime_error("torrent payload root is not a real directory");
        }
        const auto target = payload_directory_ / id;
        const auto target_status = std::filesystem::symlink_status(target, error);
        const auto missing = error == std::errc::no_such_file_or_directory ||
            target_status.type() == std::filesystem::file_type::not_found;
        if (error && !missing) {
            throw std::system_error(error, "inspect torrent payload directory");
        }
        if (!missing &&
            target_status.type() != std::filesystem::file_type::directory) {
            throw std::runtime_error("torrent payload path is not a real directory");
        }
        if (missing) {
            error.clear();
            const auto created = std::filesystem::create_directory(target, error);
            if (error) {
                throw std::system_error(error, "create torrent payload directory");
            }
            if (!created) {
                const auto raced_status = std::filesystem::symlink_status(
                    target,
                    error
                );
                if (error ||
                    raced_status.type() != std::filesystem::file_type::directory) {
                    throw std::runtime_error(
                        "torrent payload directory creation was replaced"
                    );
                }
            }
        }
        return target;
    }

    std::optional<lt::add_torrent_params> load_resume_state(const std::string& id) {
        try {
            const auto path = resume_path(id);
            const auto contents = storage::read_bounded_file(
                path,
                maximum_resume_state_size
            );
            if (!contents.has_value()) {
                return std::nullopt;
            }
            if (contents->empty()) {
                throw std::runtime_error("resume file is empty");
            }
            auto params = lt::read_resume_data(*contents);
            if (torrent_id(params.info_hashes) != id) {
                throw std::runtime_error("resume file hash does not match its name");
            }
            return params;
        } catch (const std::exception& error) {
            queued_events_.push_back({
                BackendEventType::torrent_error,
                0,
                id,
                std::string("ignored persisted torrent state: ") + error.what(),
                {},
            });
            return std::nullopt;
        }
    }

    void focus_torrent(const std::string& requested_id) {
        for (const auto& [id, handle] : handles_) {
            if (id == requested_id || !handle.is_valid() ||
                stream_bridge_->has_stream_for_torrent(id)) {
                continue;
            }
            try {
                handle.set_flags(lt::torrent_flags::upload_mode);
                handle.set_flags(
                    lt::torrent_flags::paused,
                    lt::torrent_flags::paused | lt::torrent_flags::auto_managed
                );
                mark_warm(id);
                quiesced_torrents_.insert(id);
            } catch (...) {
            }
        }
    }

    void request_resume_save(
        const lt::torrent_handle& handle,
        const lt::resume_data_flags_t flags
    ) {
        if (!handle.is_valid()) {
            return;
        }
        auto* context = handle.userdata().get<RequestContext>();
        if (context == nullptr || context->explicitly_removed ||
            pending_resume_saves_.contains(context)) {
            return;
        }
        pending_resume_saves_.insert(context);
        try {
            handle.save_resume_data(flags);
        } catch (...) {
            pending_resume_saves_.erase(context);
        }
    }

    void begin_cache_unload(
        const std::string& id,
        const lt::torrent_handle& handle,
        std::vector<BackendEvent>& events
    ) {
        if (pending_cache_unloads_.erase(id) == 0 || !handle.is_valid()) {
            return;
        }
        pending_cache_removals_.insert(id);
        try {
            session_.remove_torrent(handle);
        } catch (const std::exception& error) {
            pending_cache_removals_.erase(id);
            report_disk_cache_error(
                std::string("failed to unload inactive torrent: ") + error.what(),
                events
            );
        }
    }

    bool request_cache_unloads(
        std::vector<BackendEvent>& events,
        const bool include_warm
    ) {
        for (const auto& [id, warm_since] : warm_since_) {
            static_cast<void>(warm_since);
            if ((!include_warm && !quiesced_torrents_.contains(id)) ||
                pending_removals_.contains(id) ||
                pending_cache_unloads_.contains(id) ||
                pending_cache_removals_.contains(id) ||
                stream_bridge_->has_stream_for_torrent(id)) {
                continue;
            }
            const auto torrent = handles_.find(id);
            if (torrent == handles_.end() || !torrent->second.is_valid()) {
                continue;
            }
            pending_cache_unloads_.insert(id);
            auto* context = torrent->second.userdata().get<RequestContext>();
            request_resume_save(
                torrent->second,
                lt::torrent_handle::save_info_dict
            );
            if (context == nullptr || !pending_resume_saves_.contains(context)) {
                begin_cache_unload(id, torrent->second, events);
            }
        }
        return !pending_cache_unloads_.empty() || !pending_cache_removals_.empty();
    }

    void handle_resume_saved(
        const lt::save_resume_data_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        auto* context = alert.handle.userdata().get<RequestContext>();
        pending_resume_saves_.erase(context);
        if (context != nullptr && context->explicitly_removed) {
            return;
        }
        const auto id = context == nullptr || context->torrent_id.empty()
            ? torrent_id(alert.handle)
            : context->torrent_id;
        if (!is_canonical_torrent_id(id)) {
            return;
        }
        try {
            const auto contents = lt::write_resume_data_buf(alert.params);
            if (contents.size() > maximum_resume_state_size) {
                throw std::runtime_error("torrent state exceeds configured size limit");
            }
            storage::write_file_atomically(resume_path(id), contents);
        } catch (const std::exception& error) {
            events.push_back({
                BackendEventType::torrent_error,
                0,
                id,
                std::string("failed to persist torrent state: ") + error.what(),
                {},
            });
        }
        if (pending_cache_unloads_.contains(id)) {
            begin_cache_unload(id, alert.handle, events);
        }
    }

    void handle_resume_save_failed(
        const lt::save_resume_data_failed_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        auto* context = alert.handle.userdata().get<RequestContext>();
        pending_resume_saves_.erase(context);
        if (context != nullptr && context->explicitly_removed) {
            return;
        }
        const auto id = context == nullptr || context->torrent_id.empty()
            ? torrent_id(alert.handle)
            : context->torrent_id;
        if (pending_cache_unloads_.contains(id)) {
            begin_cache_unload(id, alert.handle, events);
            return;
        }
        if (alert.error == lt::errors::resume_data_not_modified) {
            return;
        }
        events.push_back({
            BackendEventType::torrent_error,
            0,
            id,
            std::string("failed to create torrent state: ") + alert.error.message(),
            {},
        });
    }

    void save_session_state() {
        const auto state = session_.session_state(lt::session::save_dht_state);
        const auto contents = lt::write_session_params_buf(
            state,
            lt::session::save_dht_state
        );
        if (contents.size() > maximum_session_state_size) {
            throw std::runtime_error("DHT state exceeds configured size limit");
        }
        storage::write_file_atomically(session_state_path_, contents);
    }

    void run_periodic_checkpoints(std::vector<BackendEvent>& events) {
        if (shutting_down_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_resume_checkpoint_ >= std::chrono::seconds(30)) {
            for (const auto& [id, handle] : handles_) {
                if (!pending_removals_.contains(id)) {
                    request_resume_save(
                        handle,
                        lt::torrent_handle::only_if_modified |
                            lt::torrent_handle::save_info_dict
                    );
                }
            }
            last_resume_checkpoint_ = now;
        }
        if (now - last_session_checkpoint_ >= std::chrono::minutes(5)) {
            try {
                save_session_state();
            } catch (const std::exception& error) {
                events.push_back({
                    BackendEventType::torrent_error,
                    0,
                    {},
                    std::string("failed to persist DHT state: ") + error.what(),
                    {},
                });
            }
            last_session_checkpoint_ = now;
        }
    }

    void request_telemetry_update() {
        if (shutting_down_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_telemetry_request_ < std::chrono::seconds(1)) {
            return;
        }
        session_.post_torrent_updates(lt::status_flags_t{});
        for (const auto& [id, handle] : handles_) {
            static_cast<void>(id);
            if (handle.is_valid()) {
                handle.post_peer_info();
            }
        }
        last_telemetry_request_ = now;
    }

    void mark_warm(const std::string& id) {
        warm_since_.try_emplace(id, std::chrono::steady_clock::now());
    }

    void touch_disk_cache(const std::string& id) {
        try {
            disk_cache_.touch(id);
            last_disk_cache_error_.clear();
        } catch (const std::exception& error) {
            report_disk_cache_error(error.what(), queued_events_);
        }
    }

    [[nodiscard]] std::unordered_set<std::string> protected_torrents() const {
        std::unordered_set<std::string> protected_ids;
        protected_ids.reserve(handles_.size() + pending_.size());
        for (const auto& [id, handle] : handles_) {
            if (handle.is_valid()) {
                protected_ids.insert(id);
            }
        }
        for (const auto& [context, request] : pending_) {
            static_cast<void>(context);
            if (!request->torrent_id.empty()) {
                protected_ids.insert(request->torrent_id);
            }
        }
        return protected_ids;
    }

    void report_disk_cache_error(
        const std::string& message,
        std::vector<BackendEvent>& events
    ) {
        if (message == last_disk_cache_error_) {
            return;
        }
        last_disk_cache_error_ = message;
        events.push_back({
            BackendEventType::torrent_error,
            0,
            {},
            std::string("disk cache policy error: ") + message,
            {},
        });
    }

    void enforce_disk_cache(std::vector<BackendEvent>& events) {
        try {
            const auto stats = disk_cache_.enforce(protected_torrents());
            last_disk_cache_error_.clear();
            if (stats.over_budget) {
                request_cache_unloads(events, false);
            }
        } catch (const std::exception& error) {
            report_disk_cache_error(error.what(), events);
        }
    }

    void process_disk_reclaims(std::vector<BackendEvent>& events) {
        if (pending_disk_reclaims_.empty() || shutting_down_) {
            return;
        }
        const auto stream = stream_bridge_->statistics();
        if (stream.active_http_requests > 0 || stream.active_demands > 0) {
            return;
        }
        if (request_cache_unloads(events, true)) {
            return;
        }
        auto requests = std::move(pending_disk_reclaims_);
        pending_disk_reclaims_.clear();
        next_disk_cache_check_ = std::chrono::steady_clock::now() +
            std::chrono::seconds(30);
        auto target_bytes = disk_cache_.stats().capacity_bytes;
        for (const auto& request : requests) {
            target_bytes = std::min(target_bytes, request.target_bytes);
        }
        try {
            const auto stats = disk_cache_.enforce(
                protected_torrents(),
                target_bytes
            );
            last_disk_cache_error_.clear();
            for (const auto& request : requests) {
                const auto effective_target = std::min(
                    request.target_bytes,
                    stats.capacity_bytes
                );
                events.push_back({
                    BackendEventType::disk_cache_reclaimed,
                    request.request_id,
                    {},
                    stats.used_bytes <= effective_target
                        ? "disk cache reclaim target reached"
                        : "disk cache reclaim limited by protected torrent data",
                    {},
                });
            }
        } catch (const std::exception& error) {
            for (const auto& request : requests) {
                events.push_back({
                    BackendEventType::torrent_error,
                    request.request_id,
                    {},
                    std::string("disk cache reclaim failed: ") + error.what(),
                    {},
                });
            }
        }
    }

    void measure_disk_cache(
        const std::chrono::steady_clock::time_point now,
        std::vector<BackendEvent>& events
    ) {
        if (now < next_disk_cache_measurement_) {
            return;
        }
        next_disk_cache_measurement_ = now + std::chrono::seconds(5);
        try {
            static_cast<void>(disk_cache_.measure(protected_torrents()));
            last_disk_cache_error_.clear();
        } catch (const std::exception& error) {
            report_disk_cache_error(error.what(), events);
        }
    }

    void run_disk_cache_enforcement(std::vector<BackendEvent>& events) {
        if (shutting_down_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < next_disk_cache_check_) {
            return;
        }
        const auto stream = stream_bridge_->statistics();
        if (stream.active_http_requests > 0 || stream.active_demands > 0) {
            next_disk_cache_check_ = now + std::chrono::seconds(1);
            measure_disk_cache(now, events);
            return;
        }
        next_disk_cache_check_ = now + std::chrono::seconds(30);
        enforce_disk_cache(events);
    }

    void activate_torrent(const std::string& id, const lt::torrent_handle& handle) {
        pending_cache_unloads_.erase(id);
        handle.set_flags(lt::torrent_flags::upload_mode);
        handle.set_flags(
            lt::torrent_flags_t{},
            lt::torrent_flags::paused | lt::torrent_flags::auto_managed
        );
        quiesced_torrents_.erase(id);
        warm_since_.erase(id);
    }

    void run_warm_quiescence() {
        if (shutting_down_ || warm_torrent_timeout_.count() == 0 ||
            warm_since_.empty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < next_warm_check_) {
            return;
        }
        next_warm_check_ = now + std::clamp(
            warm_torrent_timeout_ / 2,
            std::chrono::milliseconds(25),
            std::chrono::milliseconds(1000)
        );
        for (const auto& [id, warm_since] : warm_since_) {
            if (quiesced_torrents_.contains(id) ||
                now - warm_since < warm_torrent_timeout_) {
                continue;
            }
            const auto torrent = handles_.find(id);
            if (torrent == handles_.end() || !torrent->second.is_valid()) {
                continue;
            }
            request_resume_save(
                torrent->second,
                lt::torrent_handle::only_if_modified |
                    lt::torrent_handle::save_info_dict
            );
            torrent->second.set_flags(
                lt::torrent_flags::paused,
                lt::torrent_flags::paused | lt::torrent_flags::auto_managed
            );
            torrent->second.set_flags(lt::torrent_flags::upload_mode);
            quiesced_torrents_.insert(id);
        }
    }

    void handle_state_update(const lt::state_update_alert& alert) {
        for (const auto& status : alert.status) {
            const auto id = torrent_id(status.info_hashes);
            if (!handles_.contains(id)) {
                continue;
            }
            auto& telemetry = telemetry_[id];
            telemetry.connected_peers = bounded_count(nonnegative(status.num_peers));
            telemetry.connected_seeds = bounded_count(nonnegative(status.num_seeds));
            telemetry.known_peers = bounded_count(nonnegative(status.list_peers));
            telemetry.connect_candidates = bounded_count(
                nonnegative(status.connect_candidates)
            );
            telemetry.download_rate = nonnegative(status.download_payload_rate);
            telemetry.upload_rate = nonnegative(status.upload_payload_rate);
            telemetry.total_download = nonnegative(status.total_payload_download);
            telemetry.total_upload = nonnegative(status.total_payload_upload);
        }
    }

    void handle_peer_info(const lt::peer_info_alert& alert) {
        const auto id = torrent_id(alert.handle);
        if (!handles_.contains(id)) {
            return;
        }
        std::uint64_t interested = 0;
        std::uint64_t unchoked = 0;
        std::uint64_t downloading = 0;
        std::uint64_t snubbed = 0;
        std::uint64_t pending = 0;
        std::uint64_t target = 0;
        std::uint64_t timed_out = 0;
        std::uint64_t connecting = 0;
        std::uint64_t handshaking = 0;
        std::uint64_t target_piece_peers = 0;
        std::uint64_t target_piece_unchoked = 0;
        std::uint64_t target_piece_downloading = 0;
        std::uint64_t off_target_downloading = 0;
        const auto blocking = stream_bridge_->blocking_pieces(id);
        const auto is_blocking_piece = [&](const lt::piece_index_t piece) {
            if (piece < lt::piece_index_t{0}) {
                return false;
            }
            const auto value = static_cast<std::uint32_t>(static_cast<int>(piece));
            return std::ranges::find(blocking, value) != blocking.end();
        };
        for (const auto& peer : alert.peer_info) {
            if (peer.flags & lt::peer_info::connecting) {
                ++connecting;
            }
            if (peer.flags & lt::peer_info::handshake) {
                ++handshaking;
            }
            if (peer.flags & lt::peer_info::interesting) {
                ++interested;
            }
            if (!(peer.flags & lt::peer_info::remote_choked)) {
                ++unchoked;
            }
            if (peer.payload_down_speed > 0) {
                ++downloading;
            }
            if (peer.flags & lt::peer_info::snubbed) {
                ++snubbed;
            }
            saturating_add(pending, nonnegative(peer.download_queue_length));
            saturating_add(pending, nonnegative(peer.requests_in_buffer));
            saturating_add(target, nonnegative(peer.target_dl_queue_length));
            saturating_add(timed_out, nonnegative(peer.timed_out_requests));
            if (!blocking.empty()) {
                const auto has_target = (peer.flags & lt::peer_info::seed) ||
                    std::ranges::any_of(blocking, [&](const std::uint32_t piece) {
                        const auto index = lt::piece_index_t(static_cast<int>(piece));
                        return piece < static_cast<std::uint32_t>(peer.pieces.size()) &&
                            peer.pieces[index];
                    });
                if (has_target) {
                    ++target_piece_peers;
                    if (!(peer.flags & lt::peer_info::remote_choked)) {
                        ++target_piece_unchoked;
                    }
                }
                if (is_blocking_piece(peer.downloading_piece_index)) {
                    ++target_piece_downloading;
                } else if (peer.downloading_piece_index >= lt::piece_index_t{0}) {
                    ++off_target_downloading;
                }
            }
        }
        auto& telemetry = telemetry_[id];
        telemetry.interested_peers = bounded_count(interested);
        telemetry.unchoked_peers = bounded_count(unchoked);
        telemetry.downloading_peers = bounded_count(downloading);
        telemetry.snubbed_peers = bounded_count(snubbed);
        telemetry.pending_block_requests = bounded_count(pending);
        telemetry.target_block_requests = bounded_count(target);
        telemetry.timed_out_block_requests = bounded_count(timed_out);
        telemetry.connecting_peers = bounded_count(connecting);
        telemetry.handshaking_peers = bounded_count(handshaking);
        telemetry.target_piece_peers = bounded_count(target_piece_peers);
        telemetry.target_piece_unchoked_peers = bounded_count(target_piece_unchoked);
        telemetry.target_piece_downloading_peers = bounded_count(target_piece_downloading);
        telemetry.off_target_downloading_peers = bounded_count(off_target_downloading);
    }

    void handle_peer_connect(const lt::peer_connect_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found != telemetry_.end()) {
            saturating_increment(found->second.peer_connect_events);
        }
    }

    void handle_peer_disconnected(const lt::peer_disconnected_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found == telemetry_.end()) {
            return;
        }
        auto& telemetry = found->second;
        saturating_increment(telemetry.peer_disconnect_events);
        switch (alert.reason) {
        case lt::close_reason_t::timeout:
        case lt::close_reason_t::timed_out_interest:
        case lt::close_reason_t::timed_out_activity:
        case lt::close_reason_t::timed_out_handshake:
        case lt::close_reason_t::timed_out_request:
            saturating_increment(telemetry.peer_disconnect_timeouts);
            break;
        case lt::close_reason_t::upload_to_upload:
        case lt::close_reason_t::not_interested_upload_only:
            saturating_increment(telemetry.peer_disconnect_redundant);
            break;
        case lt::close_reason_t::peer_churn:
            saturating_increment(telemetry.peer_disconnect_turnover);
            break;
        default:
            if (alert.op == lt::operation_t::connect) {
                saturating_increment(telemetry.peer_disconnect_connect_failures);
            } else {
                saturating_increment(telemetry.peer_disconnect_other);
            }
            break;
        }
    }

    void handle_tracker_reply(const lt::tracker_reply_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found == telemetry_.end()) {
            return;
        }
        if (found->second.tracker_reply_events != std::numeric_limits<std::uint32_t>::max()) {
            ++found->second.tracker_reply_events;
        }
        saturating_add(
            found->second.tracker_peers_returned,
            nonnegative(alert.num_peers)
        );
    }

    void handle_tracker_error(const lt::tracker_error_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found != telemetry_.end() &&
            found->second.tracker_error_events != std::numeric_limits<std::uint32_t>::max()) {
            ++found->second.tracker_error_events;
        }
    }

    void update_session_tick() {
        if (shutting_down_) {
            return;
        }
        const auto streaming = std::ranges::any_of(handles_, [&](const auto& entry) {
            return stream_bridge_->has_stream_for_torrent(entry.first);
        });
        if (streaming == fast_session_tick_) {
            return;
        }
        lt::settings_pack settings;
        settings.set_int(
            lt::settings_pack::tick_interval,
            streaming ? streaming_session_tick_milliseconds : idle_session_tick_milliseconds
        );
        session_.apply_settings(std::move(settings));
        fast_session_tick_ = streaming;
    }

    void announce_to_dht() {
        for (const auto& [id, handle] : handles_) {
            static_cast<void>(id);
            if (handle.is_valid()) {
                handle.force_dht_announce();
            }
        }
    }

    void handle_dht_reply(const lt::dht_reply_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found == telemetry_.end()) {
            return;
        }
        if (found->second.dht_reply_events != std::numeric_limits<std::uint32_t>::max()) {
            ++found->second.dht_reply_events;
        }
        saturating_add(found->second.dht_peers_returned, nonnegative(alert.num_peers));
    }

    void handle_torrent_finished(const lt::torrent_finished_alert& alert) {
        const auto id = torrent_id(alert.handle);
        const auto found = telemetry_.find(id);
        if (found != telemetry_.end()) {
            saturating_increment(found->second.torrent_finished_events);
        }
    }

    BackendEvent prepared_event(
        const lt::torrent_handle& handle,
        const std::string& id,
        const PendingPrepare& prepare
    ) {
        BackendEvent event{
            BackendEventType::stream_prepared,
            prepare.request_id,
            id,
            {},
            {},
        };
        event.file_index = prepare.file_index;
        event.file_size = prepare.file.size;
        const auto stream = stream_bridge_->register_stream(
            handle,
            id,
            prepare.file_index,
            prepare.file
        );
        event.stream_id = stream.stream_id;
        event.stream_url = stream.url;
        return event;
    }

    static BackendEvent metadata_event(
        const lt::torrent_handle& handle,
        const RequestContext* const context,
        const MetadataSource source
    ) {
        BackendEvent event{
            BackendEventType::metadata_ready,
            context == nullptr ? 0 : context->request_id,
            torrent_id(handle),
            {},
            {},
        };
        const auto elapsed = context == nullptr ||
                context->metadata_started_at == std::chrono::steady_clock::time_point{}
            ? std::int64_t{0}
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - context->metadata_started_at
              ).count();
        event.message = "metadata_source=" + std::string(metadata_source_name(source)) +
            " metadata_elapsed_ms=" + std::to_string(std::max<std::int64_t>(elapsed, 0));
        const auto metadata = handle.torrent_file();
        if (!metadata) {
            return event;
        }
        const auto& storage = metadata->files();
        event.files.reserve(static_cast<std::size_t>(metadata->num_files()));
        for (const auto index : storage.file_range()) {
            event.files.push_back({
                storage.file_path(index),
                static_cast<std::uint64_t>(storage.file_offset(index)),
                static_cast<std::uint64_t>(storage.file_size(index)),
            });
        }
        return event;
    }

    void handle_added(const lt::add_torrent_alert& alert, std::vector<BackendEvent>& events) {
        auto* context = alert.params.userdata.get<RequestContext>();
        const auto request_id = context == nullptr ? 0 : context->request_id;
        if (alert.error) {
            events.push_back({
                BackendEventType::torrent_error,
                request_id,
                {},
                alert.error.message(),
                {},
            });
            pending_.erase(context);
            return;
        }
        const auto id = torrent_id(alert.handle);
        handles_.insert_or_assign(id, alert.handle);
        telemetry_.try_emplace(id);
        if (dht_ready_) {
            alert.handle.force_dht_announce();
        }
        bool discard_pending_context = false;
        if (context != nullptr) {
            context->torrent_id = id;
            const auto pending = pending_.find(context);
            if (pending != pending_.end()) {
                if (alert.handle.userdata().get<RequestContext>() == context) {
                    const auto active = active_.find(id);
                    if (active != active_.end()) {
                        retired_.push_back(std::move(active->second));
                        active_.erase(active);
                    }
                    active_.emplace(id, std::move(pending->second));
                    pending_.erase(pending);
                } else {
                    discard_pending_context = true;
                }
            }
        }
        events.push_back({BackendEventType::torrent_added, request_id, id, {}, {}});
        if (alert.handle.status().has_metadata && context != nullptr && !context->metadata_emitted) {
            context->metadata_emitted = true;
            events.push_back(metadata_event(
                alert.handle,
                context,
                context->metadata_source
            ));
            if (context->metadata_source != MetadataSource::restored) {
                request_resume_save(alert.handle, lt::torrent_handle::save_info_dict);
            }
        }
        if (discard_pending_context) {
            pending_.erase(context);
        }
    }

    void handle_metadata(
        const lt::metadata_received_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        auto* context = alert.handle.userdata().get<RequestContext>();
        if (context != nullptr && context->metadata_emitted) {
            return;
        }
        if (context != nullptr) {
            context->metadata_emitted = true;
        }
        events.push_back(metadata_event(alert.handle, context, MetadataSource::cold));
        request_resume_save(alert.handle, lt::torrent_handle::save_info_dict);
    }

    void handle_error(const lt::torrent_error_alert& alert, std::vector<BackendEvent>& events) {
        auto* context = alert.handle.userdata().get<RequestContext>();
        events.push_back({
            BackendEventType::torrent_error,
            context == nullptr ? 0 : context->request_id,
            torrent_id(alert.handle),
            alert.error.message(),
            {},
        });
    }

    void handle_file_priority(
        const lt::file_prio_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        const auto id = torrent_id(alert.handle);
        const auto pending = pending_prepares_.find(id);
        if (pending == pending_prepares_.end()) {
            return;
        }
        if (alert.error) {
            events.push_back({
                BackendEventType::torrent_error,
                pending->second.request_id,
                id,
                alert.error.message(),
                {},
            });
        } else {
            try {
                alert.handle.unset_flags(lt::torrent_flags::upload_mode);
                events.push_back(prepared_event(alert.handle, id, pending->second));
                request_resume_save(
                    alert.handle,
                    lt::torrent_handle::only_if_modified |
                        lt::torrent_handle::save_info_dict
                );
            } catch (const std::exception& error) {
                events.push_back({
                    BackendEventType::torrent_error,
                    pending->second.request_id,
                    id,
                    error.what(),
                    {},
                });
            }
        }
        pending_prepares_.erase(pending);
    }

    void handle_file_error(
        const lt::file_error_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        const auto id = torrent_id(alert.handle);
        const auto pending = pending_prepares_.find(id);
        if (pending == pending_prepares_.end()) {
            return;
        }
        events.push_back({
            BackendEventType::torrent_error,
            pending->second.request_id,
            id,
            alert.error.message(),
            {},
        });
        pending_prepares_.erase(pending);
    }

    void handle_removed(
        const lt::torrent_removed_alert& alert,
        std::vector<BackendEvent>& events
    ) {
        auto* context = alert.userdata.get<RequestContext>();
        const auto id = context == nullptr || context->torrent_id.empty()
            ? torrent_id(alert.info_hashes)
            : context->torrent_id;
        const auto pending = pending_removals_.find(id);
        const auto request_id = pending == pending_removals_.end() ? 0 : pending->second;
        const auto cache_removal = pending_cache_removals_.erase(id) > 0;
        pending_cache_unloads_.erase(id);
        events.push_back({BackendEventType::torrent_removed, request_id, id, {}, {}});
        if (pending != pending_removals_.end()) {
            pending_removals_.erase(pending);
        }
        handles_.erase(id);
        telemetry_.erase(id);
        warm_since_.erase(id);
        quiesced_torrents_.erase(id);
        stream_bridge_->remove_torrent(id);
        pending_prepares_.erase(id);
        pending_resume_saves_.erase(context);
        if (!cache_removal) {
            try {
                storage::remove_file_if_present(resume_path(id));
            } catch (const std::exception& error) {
                events.push_back({
                    BackendEventType::torrent_error,
                    request_id,
                    id,
                    std::string("failed to remove persisted torrent state: ") + error.what(),
                    {},
                });
            }
        }
        const auto active = active_.find(id);
        if (active != active_.end()) {
            retired_.push_back(std::move(active->second));
            active_.erase(active);
        }
        const auto stream = stream_bridge_->statistics();
        if (stream.active_http_requests == 0 && stream.active_demands == 0) {
            enforce_disk_cache(events);
        } else {
            next_disk_cache_check_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(1);
        }
    }

    std::filesystem::path state_root_;
    std::filesystem::path resume_directory_;
    std::filesystem::path session_state_path_;
    std::filesystem::path payload_directory_;
    const std::chrono::milliseconds warm_torrent_timeout_;
    cache::DiskCacheManager disk_cache_;
    std::unordered_map<RequestContext*, std::unique_ptr<RequestContext>> pending_;
    std::unordered_map<std::string, std::unique_ptr<RequestContext>> active_;
    std::unordered_map<std::string, lt::torrent_handle> handles_;
    std::unordered_map<std::string, TorrentTelemetry> telemetry_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> warm_since_;
    std::unordered_set<std::string> quiesced_torrents_;
    std::vector<PendingDiskReclaim> pending_disk_reclaims_;
    std::unordered_map<std::string, PendingPrepare> pending_prepares_;
    std::unordered_map<std::string, std::uint64_t> pending_removals_;
    std::unordered_set<RequestContext*> pending_resume_saves_;
    std::unordered_set<std::string> pending_cache_unloads_;
    std::unordered_set<std::string> pending_cache_removals_;
    std::vector<std::unique_ptr<RequestContext>> retired_;
    std::vector<BackendEvent> queued_events_;
    std::chrono::steady_clock::time_point last_resume_checkpoint_ =
        std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_session_checkpoint_ =
        std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_telemetry_request_ =
        std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::chrono::steady_clock::time_point next_warm_check_{};
    std::chrono::steady_clock::time_point next_disk_cache_check_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    std::chrono::steady_clock::time_point next_disk_cache_measurement_{};
    std::string last_disk_cache_error_;
    bool shutting_down_ = false;
    bool dht_ready_ = false;
    bool fast_session_tick_ = false;
    bool isolated_session_ = false;
    lt::session session_;
    std::unique_ptr<LibtorrentStreamBridge> stream_bridge_;
};

}

std::unique_ptr<ProtocolBackend> create_protocol_backend(const ProtocolBackendConfig& config) {
    return std::make_unique<LibtorrentBackend>(config);
}

}

const char* libtorrent_version_string() {
    return LIBTORRENT_VERSION;
}
