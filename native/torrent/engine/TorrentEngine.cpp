#include "TorrentEngine.h"
#include "DebugLogBuffer.h"

#ifdef HAS_LIBTORRENT

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>
#include <QDateTime>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/ip_filter.hpp>

#include <QSettings>
#include <QStringList>
#include <QHostAddress>

#include <atomic>
#include <chrono>
#include <fstream>

// ── AlertWorker: runs on a dedicated QThread, drains libtorrent alerts ──────
class TorrentEngine::AlertWorker : public QObject
{
    Q_OBJECT
public:
    AlertWorker(TorrentEngine* engine) : m_engine(engine) {}

public slots:
    void run()
    {
        m_running = true;
        m_traceActive = qEnvironmentVariableIsSet("TANKOBAN_ALERT_TRACE");
        if (m_traceActive) {
            m_traceFile.open("alert_trace.log", std::ios::binary | std::ios::trunc);
            if (m_traceFile.is_open()) {
                m_traceFile << "# Mode A alert trace — hash,type,pieceIdx,blockIdx,wallClockMs\n";
                m_traceFile.flush();
            }
        }
        // STREAM_ENGINE_REBUILD M2 (integration-memo §5) — alert-pump latency
        // tightened from 250 ms → 25 ms. Stremio's libtorrent pump runs at
        // 5 ms (backend/libtorrent/mod.rs:204); 25 ms is a conservative
        // middle that preserves wake-latency headroom for StreamPieceWaiter
        // without burning CPU on idle sessions (libtorrent's internal work
        // is still alert-driven — we're just sampling more often).
        //
        // progressTick was "every 4 wait_for_alert returns" = 1 Hz at the
        // old 250 ms cadence. With the new 25 ms cadence a simple counter
        // would fire torrentProgress at 10 Hz, flooding downstream
        // StreamEngine / StreamPage consumers. Converted to wall-clock so
        // the 1 s emit contract is preserved independent of pump cadence.
        constexpr int kAlertWaitMs = 25;
        constexpr qint64 kProgressEmitIntervalMs = 1000;
        qint64 lastProgressMs = QDateTime::currentMSecsSinceEpoch();

        while (m_running) {
            auto* alert = m_engine->m_session.wait_for_alert(
                std::chrono::milliseconds(kAlertWaitMs));
            if (alert) drainAlerts();
            triggerPeriodicResumeSaves();

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs - lastProgressMs >= kProgressEmitIntervalMs) {
                lastProgressMs = nowMs;
                emitProgressEvents();
                m_engine->checkSeedingRules();
            }
        }
    }

    void requestStop() { m_running = false; }

private:
    TorrentEngine* m_engine;
    std::atomic<bool> m_running{false};
    std::chrono::steady_clock::time_point m_lastResumeSave{std::chrono::steady_clock::now()};
    static constexpr int RESUME_SAVE_INTERVAL_S = 30;

    // STREAM diagnostic (Agent 4B — Mode A alert trace; see alert_mask setup
    // comment for scope + removal condition).
    bool m_traceActive = false;
    std::ofstream m_traceFile;

    void writeAlertTrace(const char* type, const QString& hash,
                         int pieceIdx, int blockIdx)
    {
        if (!m_traceFile.is_open()) return;
        auto nowMs = QDateTime::currentMSecsSinceEpoch();
        m_traceFile << hash.left(10).toStdString()
                    << ',' << type
                    << ',' << pieceIdx
                    << ',' << blockIdx
                    << ',' << nowMs
                    << '\n';
        m_traceFile.flush();
    }

    void drainAlerts()
    {
        std::vector<lt::alert*> alerts;
        m_engine->m_session.pop_alerts(&alerts);

        for (auto* a : alerts) {
            if (auto* mra = lt::alert_cast<lt::metadata_received_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(mra->handle);
                QString name;
                qint64 totalSize = 0;
                QJsonArray files;

                auto ti = mra->handle.torrent_file();
                if (ti) {
                    name = QString::fromStdString(ti->name());
                    totalSize = ti->total_size();
                    auto& fs = ti->files();
                    for (int i = 0; i < fs.num_files(); ++i) {
                        QJsonObject f;
                        f["index"] = i;
                        f["name"]  = QString::fromStdString(fs.file_path(lt::file_index_t{i}));
                        f["size"]  = static_cast<qint64>(fs.file_size(lt::file_index_t{i}));
                        files.append(f);
                    }
                }

                // Update record
                {
                    QMutexLocker lock(&m_engine->m_mutex);
                    if (m_engine->m_records.contains(hash)) {
                        m_engine->m_records[hash].metadataReady = true;
                        m_engine->m_records[hash].name = name;
                    }
                }

                emit m_engine->metadataReady(hash, name, totalSize, files);
            }
            else if (auto* ata = lt::alert_cast<lt::add_torrent_alert>(a)) {
                QString hash;
                if (ata->handle.is_valid()) {
                    hash = m_engine->canonicalKeyForHandle(ata->handle);
                } else {
                    const auto raw = ata->params.info_hashes.get_best().to_string();
                    hash.reserve(static_cast<int>(raw.size()) * 2);
                    for (unsigned char c : raw)
                        hash += QString::asprintf("%02x", c);
                }
                if (ata->error) {
                    emit m_engine->torrentAddFailed(
                        hash, QString::fromStdString(ata->error.message()));
                } else {
                    emit m_engine->torrentAddedConfirmed(hash);
                }
            }
            else if (auto* tea = lt::alert_cast<lt::torrent_error_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(tea->handle);
                emit m_engine->torrentError(hash, QString::fromStdString(tea->message()));
            }
            else if (auto* tfa = lt::alert_cast<lt::torrent_finished_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(tfa->handle);
                emit m_engine->torrentFinished(hash);
            }
            else if (auto* srd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                // Pin-only: a resume alert delivered AFTER removeTorrent finds no
                // pin -> suppress it, so a delayed save never recreates an orphan
                // .fastresume under the raw get_best() (v2) hash.
                auto hash = m_engine->pinnedKeyForHandle(srd->handle);
                if (!hash.isEmpty()) {
                    auto path = m_engine->m_cacheDir + "/resume/" + hash + ".fastresume";
                    try {
                        auto buf = lt::write_resume_data_buf(srd->params);
                        QByteArray blob(buf.data(), static_cast<int>(buf.size()));
                        emit m_engine->resumeDataAvailable(hash, blob);
                        std::ofstream ofs(path.toStdString(), std::ios::binary | std::ios::trunc);
                        ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
                    } catch (const std::exception& e) {
                        qWarning() << "Failed to write resume data for" << hash << ":" << e.what();
                    }
                }
            }
            else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                qWarning() << "Resume data save failed:" << a->message().c_str();
            }
            else if (auto* sma = lt::alert_cast<lt::storage_moved_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(sma->handle);
                QString newPath = QString::fromUtf8(sma->storage_path());
                emit m_engine->storageMoved(hash, newPath);
            }
            else if (auto* smf = lt::alert_cast<lt::storage_moved_failed_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(smf->handle);
                emit m_engine->storageMoveFailed(hash,
                    QString::fromStdString(smf->message()));
            }
            else if (auto* fra = lt::alert_cast<lt::file_renamed_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(fra->handle);
                emit m_engine->fileRenamed(
                    hash,
                    static_cast<int>(fra->index),
                    QString::fromUtf8(fra->new_name()));
            }
            else if (auto* frf = lt::alert_cast<lt::file_rename_failed_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(frf->handle);
                emit m_engine->fileRenameFailed(
                    hash,
                    static_cast<int>(frf->index),
                    QString::fromStdString(frf->message()));
            }
            // STREAM_ENGINE_REBUILD P2 — pieceFinished signal surface. The
            // piece_progress alert category is enabled unconditionally below
            // (applySettings alert_mask); each piece_finished_alert becomes
            // one Qt signal emit on the alert worker thread, delivered via
            // QueuedConnection to StreamPieceWaiter (P2 consumer lives on
            // main/GUI thread). Zero existing Qt consumers bind to this
            // signal; purely additive. Mode A diagnostic trace reuses the
            // same alert, still env-var gated.
            else if (auto* pfa = lt::alert_cast<lt::piece_finished_alert>(a)) {
                auto hash = m_engine->canonicalKeyForHandle(pfa->handle);
                const int pieceIdx = static_cast<int>(pfa->piece_index);
                emit m_engine->pieceFinished(hash, pieceIdx);
                if (m_traceActive)
                    writeAlertTrace("piece_finished", hash, pieceIdx, -1);
            }
            // STREAM diagnostic (Agent 4B — Mode A block-level alert trace;
            // stays env-var gated because block_progress alert volume is
            // per-16KB-block vs piece_progress at per-piece).
            else if (m_traceActive) {
                if (auto* bfa = lt::alert_cast<lt::block_finished_alert>(a)) {
                    writeAlertTrace("block_finished",
                                    m_engine->canonicalKeyForHandle(bfa->handle),
                                    static_cast<int>(bfa->piece_index),
                                    static_cast<int>(bfa->block_index));
                }
            }
        }
    }

    void emitProgressEvents()
    {
        QMutexLocker lock(&m_engine->m_mutex);
        for (auto& rec : m_engine->m_records) {
            if (!rec.handle.is_valid()) continue;
            auto st = rec.handle.status();
            if (st.flags & lt::torrent_flags::paused) continue;

            emit m_engine->torrentProgress(
                rec.infoHash, st.progress,
                st.download_rate, st.upload_rate,
                st.num_peers, st.num_seeds
            );
        }
    }

    void triggerPeriodicResumeSaves()
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastResumeSave).count();
        if (elapsed < RESUME_SAVE_INTERVAL_S) return;
        m_lastResumeSave = now;

        // Drafts in metadata-resolution (savePath = resolve_tmp) aren't in
        // TorrentClient's records.json yet — if the user cancels or the app
        // crashes before startDownload(), the .fastresume lingers with no
        // owner. Skipping save_resume_data for drafts closes the leak source.
        const QString resolveTmp = m_engine->m_cacheDir + QStringLiteral("/resolve_tmp");

        QMutexLocker lock(&m_engine->m_mutex);
        for (auto& rec : m_engine->m_records) {
            if (!rec.handle.is_valid()) continue;
            if (rec.savePath == resolveTmp) continue;
            try {
                rec.handle.save_resume_data(lt::torrent_handle::save_info_dict);
            } catch (...) {}
        }
    }
};

// ── TorrentEngine implementation ────────────────────────────────────────────

// COLOSSEUM PHASE 1c ORGAN PATCH (Hemanth-sanctioned deviation from verbatim
// import, 2026-07-13) — construct the session DORMANT. A default-constructed
// lt::session goes network-live at construction: binds listen sockets on 6881
// (listen_interfaces default "0.0.0.0:6881,[::]:6881"), bootstraps DHT, starts
// UPnP/NAT-PMP. In TB2 the ctor→start() gap was momentary; in Colosseum the
// engine sits constructed-but-NOT-started for the whole app lifetime, so that
// default is live surprise network activity at every boot. Empty
// listen_interfaces disables networking entirely per libtorrent's own docs
// ("No DHT will start, no outgoing uTP or tracker connections will be made");
// the four enable_* flags are belt-and-suspenders. start() → applySettings()
// sets the real listen_interfaces and re-enables DHT/LSD/UPnP/NAT-PMP, which
// brings the session live — no other change needed.
static lt::session_params dormantSessionParams()
{
    lt::session_params params;
    params.settings.set_str (lt::settings_pack::listen_interfaces, "");
    params.settings.set_bool(lt::settings_pack::enable_dht,    false);
    params.settings.set_bool(lt::settings_pack::enable_lsd,    false);
    params.settings.set_bool(lt::settings_pack::enable_upnp,   false);
    params.settings.set_bool(lt::settings_pack::enable_natpmp, false);
    return params;
}

TorrentEngine::TorrentEngine(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_session(dormantSessionParams()), m_cacheDir(cacheDir)
{
}

TorrentEngine::~TorrentEngine()
{
    stop();
}

void TorrentEngine::ensureDirs()
{
    QDir().mkpath(m_cacheDir);
    QDir().mkpath(m_cacheDir + "/resume");
}

void TorrentEngine::applySettings()
{
    lt::settings_pack sp;

    sp.set_str(lt::settings_pack::listen_interfaces,
               "0.0.0.0:6881,[::]:6881");

    sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
               "router.bittorrent.com:6881,"
               "router.utorrent.com:6881,"
               "dht.transmissionbt.com:6881,"
               "dht.libtorrent.org:25401");

    sp.set_bool(lt::settings_pack::enable_dht, true);
    sp.set_bool(lt::settings_pack::enable_lsd, true);
    sp.set_bool(lt::settings_pack::enable_natpmp, true);
    sp.set_bool(lt::settings_pack::enable_upnp, true);

    // STREAM_ENGINE_REBUILD P2 — piece_progress is unconditional now:
    // pieceFinished signal (drainAlerts branch above) is a hard P2
    // dependency for StreamPieceWaiter. Alert volume is per-piece
    // (typically ~2-4K total over a 4GB stream), negligible next to
    // libtorrent's baseline alert stream. block_progress stays env-var
    // gated — block_finished_alert volume is per-16KB-block and only
    // Mode A diagnosis needs it.
    int alertMask = lt::alert_category::status
                  | lt::alert_category::storage
                  | lt::alert_category::error
                  | lt::alert_category::piece_progress;
    if (qEnvironmentVariableIsSet("TANKOBAN_ALERT_TRACE")) {
        alertMask |= lt::alert_category::block_progress;
    }
    sp.set_int(lt::settings_pack::alert_mask, alertMask);

    // STREAM_PLAYBACK_FIX Phase 3 Batch 3.3 — session settings tuned for
    // streaming. Pre-3.3 values admitted "less aggressive than streaming"
    // (original comment). Reference: Stremio streaming_server settings
    // (stremio-core-development/src/types/streaming_server/settings.rs)
    // + libtorrent streaming guidance at https://www.libtorrent.org/streaming.html.
    //
    // Rationale per setting:
    //   connections_limit 200→400: stream head-fetch benefits from more
    //       parallel peers — more candidates for set_piece_deadline() to
    //       pick the fastest one. Stremio defaults to 200 but runs on
    //       typically-headless server hardware; desktop Windows with
    //       libtorrent 2.x handles 400 comfortably.
    //   active_downloads = 8 (TANKORENT_QUALITY_AND_QUEUE P1 T1.6, 2026-05-27):
    //       libtorrent's global serial cap is RELEASED. Per-show sequential
    //       discipline now lives in tankoban::queue::TransferQueue (keyed by
    //       imdb:<id>) which gates addTorrent/addMagnetHeadless via
    //       TorrentClient. Inside a show: strictly one transfer at a time.
    //       Across shows: parallel up to active_downloads. Honors the
    //       "episode by episode sequential" intent from Hemanth 2026-05-21
    //       without forcing different shows to serialize against each other.
    //       Previous setting was 1 (SEQUENTIAL_DOWNLOADS_FIX T1, 2026-05-25),
    //       which collapsed cross-show parallelism as a side effect — the
    //       per-show queue restores it.
    //   active_seeds 10, active_limit 20: unchanged. Seeding is
    //       independent of download contention; the active_limit cap is
    //       the outer ring and stays loose so completed torrents can keep
    //       seeding without blocking new download dispatches.
    //   max_queued_disk_bytes 1MB (default) → 32MB: absorbs piece-write
    //       bursts during head-deadline fan-out without back-pressuring
    //       the scheduler. Streaming workloads write bigger bursts.
    //   request_queue_time 3 (default) → 10: how many seconds of
    //       outstanding requests libtorrent maintains per peer. Streaming
    //       benefits from deeper queues so a slow-to-respond peer
    //       doesn't stall the reader frontier. REVERT ATTEMPT 2026-04-19:
    //       tried dropping to default 3 based on a (mistaken) hypothesis
    //       that it conflicted with time-critical scheduler's 2s
    //       download_queue_time break at torrent.cpp:11169. Empirical:
    //       cold-open REGRESSED from 11.5 s firstPieceMs to >109 s stuck
    //       at 0% despite 14 MB/s + 177 peers. Deep queue actually HELPS
    //       dispatch throughput — reverted to 10. Root cause of the
    //       original piece-5 stall (peer_have_count=149, 31 s wait) is
    //       still unidentified; need more instrumentation before another
    //       hypothesis lands.
    //   request_timeout 10 kept: aggressive dropout for unresponsive
    //       peers is good for streaming head-fetch.
    sp.set_int(lt::settings_pack::connections_limit, 400);

    sp.set_int(lt::settings_pack::active_downloads, 8);
    sp.set_int(lt::settings_pack::active_seeds, 10);
    sp.set_int(lt::settings_pack::active_limit, 20);

    sp.set_int(lt::settings_pack::max_queued_disk_bytes, 32 * 1024 * 1024);
    sp.set_int(lt::settings_pack::request_queue_time, 10);

    // STREAM_STALL_FIX Phase 3 — session-settings bundle (tactic e-settings).
    // Agent 4B Congress 7 B3 identified `can_request_time_critical` gate-4
    // saturation at peer_connection.cpp:3543-3558 as the residual-stall
    // mechanism post-Phase-2 (Agent 4 smoke: avg_peer_q_ms=253, peers_dl=1-2,
    // tail-block saturation on pieces at 95-99% block completion). The gate
    // returns false when `download_queue + request_queue > desired_queue_size
    // * 2`; raising max_out_request_queue (libtorrent default 500) to 1500
    // widens the per-peer cap so saturated peers can hold deeper pipelines
    // without hitting that limit. Conservative bump; 2000 reserved for a
    // future iteration if 1500 is net-positive-but-insufficient.
    // whole_pieces_threshold left at libtorrent default (20 seconds) — at
    // 8-16 MB pieces on 5-10 MB/s sustained streams, whole-piece mode sits
    // well below threshold so is NOT force-triggered. Verified via readback
    // log after apply_settings below.
    sp.set_int(lt::settings_pack::max_out_request_queue, 1500);

    sp.set_int(lt::settings_pack::request_timeout, 10);
    sp.set_int(lt::settings_pack::peer_timeout, 20);
    sp.set_int(lt::settings_pack::upload_rate_limit, 0); // unlimited — user controls via setGlobalSpeedLimits()

    // Announce to ALL trackers
    sp.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    sp.set_bool(lt::settings_pack::announce_to_all_tiers, true);

    // Peer connection
    sp.set_int(lt::settings_pack::peer_connect_timeout, 7);
    sp.set_int(lt::settings_pack::min_reconnect_time, 10);
    sp.set_int(lt::settings_pack::connection_speed, 50);
    sp.set_int(lt::settings_pack::mixed_mode_algorithm,
               lt::settings_pack::prefer_tcp);

    // Encryption
    sp.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_enabled);
    sp.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);

    // EXPERIMENT 1 (2026-04-23) — Stremio session_params port gated behind
    // TANKOBAN_STREMIO_TUNE=1 env var. Default OFF preserves current Tankoban
    // behavior (Tankorent downloads continue working as-is). When ON, applies
    // the 10 streaming-critical settings Stremio's stream-server sets in
    // C:\Users\Suprabha\Downloads\Stremio Reference\stream-server-master\
    // bindings\libtorrent-sys\cpp\wrapper.cpp:186-216. See plan at
    // ~/.claude/plans/2026-04-23-stremio-tuning-ab-experiment.md.
    //
    // Falsifiability bar: >=40% stall-per-10min reduction vs flag-off baseline
    // AND cold-open must not regress >20%. If bar not met this block gets
    // git-reverted and a memory entry documents the negative result.
    //
    // NOTE: request_queue_time and peer_timeout are already set above with
    // our own values (10 and 20 respectively); Stremio uses 3 and 60. Under
    // the experiment flag we OVERWRITE to Stremio's values so the A/B tests
    // the full Stremio config coherently.
    const QByteArray stremioTune = qgetenv("TANKOBAN_STREMIO_TUNE");
    if (stremioTune == "1") {
        qInfo() << "[TorrentEngine] EXPERIMENT: TANKOBAN_STREMIO_TUNE=1 — applying Stremio session_params overrides.";
        sp.set_bool(lt::settings_pack::strict_end_game_mode,     true);   // libtorrent default false
        sp.set_bool(lt::settings_pack::prioritize_partial_pieces, true);  // libtorrent default false
        sp.set_bool(lt::settings_pack::smooth_connects,          false);  // libtorrent default true
        sp.set_int (lt::settings_pack::piece_timeout,            5);      // libtorrent default ~20
        sp.set_int (lt::settings_pack::unchoke_slots_limit,      20);     // libtorrent default 8
        sp.set_int (lt::settings_pack::min_reconnect_time,       1);      // was set 10 above -> overwrite
        sp.set_int (lt::settings_pack::connection_speed,         200);    // was set 50 above -> overwrite
        sp.set_int (lt::settings_pack::peer_connect_timeout,     3);      // was set 7 above -> overwrite
        sp.set_int (lt::settings_pack::peer_timeout,             60);     // was set 20 above -> overwrite
        sp.set_int (lt::settings_pack::request_queue_time,       3);      // was set 10 above -> overwrite
    }

    m_session.apply_settings(sp);

    // STREAM_STALL_FIX Phase 3 — session-init verification log. Readback
    // from m_session.get_settings() (not the staged `sp`) so we log the
    // actually-applied effective values. Smoke exit criterion: first line
    // of this log in main-app startup must show max_out_request_queue=1500.
    {
        const auto applied = m_session.get_settings();
        qDebug().noquote() << "[TorrentEngine] session settings applied —"
            << "max_out_request_queue=" << applied.get_int(lt::settings_pack::max_out_request_queue)
            << "whole_pieces_threshold=" << applied.get_int(lt::settings_pack::whole_pieces_threshold)
            << "request_queue_time=" << applied.get_int(lt::settings_pack::request_queue_time)
            << "strict_end_game_mode=" << applied.get_bool(lt::settings_pack::strict_end_game_mode)
            << "prioritize_partial_pieces=" << applied.get_bool(lt::settings_pack::prioritize_partial_pieces)
            << "smooth_connects=" << applied.get_bool(lt::settings_pack::smooth_connects)
            << "piece_timeout=" << applied.get_int(lt::settings_pack::piece_timeout)
            << "unchoke_slots_limit=" << applied.get_int(lt::settings_pack::unchoke_slots_limit)
            << "min_reconnect_time=" << applied.get_int(lt::settings_pack::min_reconnect_time)
            << "connection_speed=" << applied.get_int(lt::settings_pack::connection_speed)
            << "peer_connect_timeout=" << applied.get_int(lt::settings_pack::peer_connect_timeout)
            << "peer_timeout=" << applied.get_int(lt::settings_pack::peer_timeout);
    }
}

void TorrentEngine::loadDhtState()
{
    auto path = m_cacheDir + "/session.state";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QByteArray data = file.readAll();
    if (data.isEmpty()) return;

    lt::error_code ec;
    lt::bdecode_node node;
    lt::bdecode(data.data(), data.data() + data.size(), node, ec);
    if (ec) {
        qWarning() << "Failed to bdecode DHT state:" << ec.message().c_str();
        return;
    }
    m_session.load_state(node);
    qDebug() << "Loaded DHT state from" << path << "(" << data.size() << "bytes)";
}

void TorrentEngine::saveDhtState()
{
    auto path = m_cacheDir + "/session.state";
    auto tmp  = path + ".tmp";

    lt::entry state;
    m_session.save_state(state);
    std::vector<char> buf;
    lt::bencode(std::back_inserter(buf), state);

    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(buf.data(), static_cast<qint64>(buf.size()));
    file.close();

    QFile::remove(path);
    QFile::rename(tmp, path);
    qDebug() << "Saved DHT state (" << buf.size() << "bytes)";
}

void TorrentEngine::saveAllResumeData()
{
    // Skip drafts (savePath = resolve_tmp) — see triggerPeriodicResumeSaves.
    const std::string resolveTmp =
        (m_cacheDir + QStringLiteral("/resolve_tmp")).toStdString();

    auto torrents = m_session.get_torrents();
    int count = 0;
    for (auto& h : torrents) {
        if (!h.is_valid()) continue;
        if (h.status().save_path == resolveTmp) continue;
        try {
            h.save_resume_data(lt::torrent_handle::save_info_dict);
            ++count;
        } catch (...) {}
    }
    if (count == 0) return;

    int remaining = count;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (remaining > 0 && std::chrono::steady_clock::now() < deadline) {
        auto* alert = m_session.wait_for_alert(std::chrono::milliseconds(250));
        if (!alert) continue;
        std::vector<lt::alert*> alerts;
        m_session.pop_alerts(&alerts);
        for (auto* a : alerts) {
            if (auto* srd = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                // Pin-only (see drainAlerts): skip a delayed post-removal alert so
                // shutdown never re-writes an orphan .fastresume. Still count it so
                // the drain loop terminates.
                auto hash = pinnedKeyForHandle(srd->handle);
                if (!hash.isEmpty()) {
                    auto resumePath = m_cacheDir + "/resume/" + hash + ".fastresume";
                    try {
                        auto buf = lt::write_resume_data_buf(srd->params);
                        std::ofstream ofs(resumePath.toStdString(), std::ios::binary | std::ios::trunc);
                        ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
                    } catch (...) {}
                }
                --remaining;
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                --remaining;
            }
        }
    }
    qDebug() << "Saved resume data for" << (count - remaining) << "torrents";
}

QString TorrentEngine::hashToHex(const lt::torrent_handle& h)
{
    auto raw = h.info_hashes().get_best().to_string();
    QString hex;
    hex.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        hex += QString::asprintf("%02x", c);
    }
    return hex;
}

QString TorrentEngine::canonicalKeyForHandleLocked(const lt::torrent_handle& h) const
{
    if (!h.is_valid()) return {};
    const auto it = m_handleKeyById.constFind(h.id());
    if (it != m_handleKeyById.constEnd())
        return it.value();
    // Unregistered handle (never routed through addMagnet/addTorrent/
    // addFromResume — e.g. a raw session torrent during the shutdown flush). Its
    // hash has not drifted yet, so the raw best-hash is a safe fallback; every
    // handle we registered resolves to its pinned key above.
    return hashToHex(h);
}

QString TorrentEngine::canonicalKeyForHandle(const lt::torrent_handle& h) const
{
    QMutexLocker lock(&m_mutex);
    return canonicalKeyForHandleLocked(h);
}

QString TorrentEngine::pinnedKeyForHandle(const lt::torrent_handle& h) const
{
    if (!h.is_valid()) return {};
    QMutexLocker lock(&m_mutex);
    return m_handleKeyById.value(h.id());   // {} once the pin is gone (post-removal)
}

QString TorrentEngine::stateToString(lt::torrent_status::state_t s, bool paused)
{
    if (paused) return QStringLiteral("paused");
    switch (s) {
    case lt::torrent_status::checking_files:
    case lt::torrent_status::checking_resume_data:
        return QStringLiteral("checking");
    case lt::torrent_status::downloading_metadata:
        return QStringLiteral("metadata");
    case lt::torrent_status::downloading:
        return QStringLiteral("downloading");
    case lt::torrent_status::finished:
    case lt::torrent_status::seeding:
        return QStringLiteral("seeding");
    default:
        return QStringLiteral("unknown");
    }
}

void TorrentEngine::start()
{
    if (m_running) return;
    ensureDirs();
    applySettings();
    loadDhtState();

    // Re-apply persisted ban list (Phase 6.4) so banned peers stay blocked
    // across app restart. `banPeer()` already writes to QSettings on each ban.
    {
        lt::ip_filter filter = m_session.get_ip_filter();
        for (const QString& ipStr : QSettings().value(
                QStringLiteral("tankorent/bannedPeers")).toStringList()) {
            lt::error_code ec;
            auto addr = lt::make_address(ipStr.toStdString(), ec);
            if (ec) continue;
            filter.add_rule(addr, addr, lt::ip_filter::blocked);
        }
        m_session.set_ip_filter(filter);
    }

    m_alertWorker = new AlertWorker(this);
    m_alertWorker->moveToThread(&m_alertThread);
    connect(&m_alertThread, &QThread::started, m_alertWorker, &AlertWorker::run);
    connect(&m_alertThread, &QThread::finished, m_alertWorker, &QObject::deleteLater);
    m_alertThread.start();

    m_running = true;
    qDebug() << "TorrentEngine started, cache:" << m_cacheDir;
}

void TorrentEngine::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_alertWorker)
        m_alertWorker->requestStop();
    m_alertThread.quit();
    if (!m_alertThread.wait(5000)) {
        DebugLogBuffer::instance().warning("torrent-engine",
            QStringLiteral("stop(): alert worker did not finish within 5s — proceeding with teardown"));
        qWarning() << "TorrentEngine::stop() alert worker wait timed out";
    }

    saveAllResumeData();
    saveDhtState();
    m_session.pause();

    qDebug() << "TorrentEngine stopped";
}

// ── Torrent operations ──────────────────────────────────────────────────────

QString TorrentEngine::addMagnet(const QString& magnetUri, const QString& savePath, bool paused)
{
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(magnetUri.toStdString(), ec);
    if (ec) {
        DebugLogBuffer::instance().warning("torrent-engine",
            QStringLiteral("addMagnet: parse_magnet_uri rejected URI: %1 (uri prefix=%2)")
                .arg(QString::fromUtf8(ec.message().c_str())).arg(magnetUri.left(80)));
        qWarning() << "Invalid magnet URI:" << ec.message().c_str();
        return {};
    }

    atp.save_path = savePath.toStdString();
    if (paused) {
        // Don't set paused flag — torrent needs to be active to resolve metadata
        // (connect to peers, fetch torrent info via DHT/trackers).
        // Instead, we keep it active but let the caller set file priorities to 0
        // after metadata arrives to prevent downloading actual content.
        atp.flags &= ~lt::torrent_flags::paused;
        atp.flags |= lt::torrent_flags::auto_managed;

        // Upload-only mode: allow peer connections for metadata but minimize traffic
        atp.flags |= lt::torrent_flags::upload_mode;
    }

    // Capture parsed info_hashes before the std::move(atp) consumes them — we
    // need them for the idempotent-reuse fallback below.
    const auto parsedInfoHashes = atp.info_hashes;

    auto handle = m_session.add_torrent(std::move(atp), ec);
    if (ec) {
        auto& dlog = DebugLogBuffer::instance();
        const QString ecMsg = QString::fromUtf8(ec.message().c_str());
        dlog.warning("torrent-engine",
            QStringLiteral("addMagnet: lt::add_torrent failed code=%1 cat=%2 msg=%3")
                .arg(ec.value()).arg(QString::fromUtf8(ec.category().name())).arg(ecMsg));

        // Idempotent re-add: when a prior resolveMetadata() draft is still
        // pending in the libtorrent session (session::remove_torrent only
        // completes on a later torrent_removed_alert — async at the libtorrent
        // layer), a fresh add_torrent for the same hash returns
        // duplicate_torrent. Instead of reporting "Failed to Add Magnet" to
        // the user, look up the existing handle and reuse it. (Hemanth-reported
        // 2026-04-30: download icon + right-click "Download…" both surfaced
        // "Failed to Add Magnet" because every prior cancelled draft pinned
        // the magnet in the session past the next click.)
        // libtorrent 2.0.x at C:/tools/libtorrent-2.0-msvc only exposes the
        // sha1_hash overload of find_torrent; the sha256_hash overload was
        // added in a later 2.x. v1-only magnets have v1 set; hybrid (v1+v2)
        // magnets ALSO have v1 populated (truncated from v2's first 20 bytes).
        // v2-only magnets are rare and not handled here — they'd surface as
        // "Failed to Add Magnet" until libtorrent is upgraded.
        lt::torrent_handle existing;
        if (parsedInfoHashes.has_v1())
            existing = m_session.find_torrent(parsedInfoHashes.v1);

        if (existing.is_valid()) {
            QMutexLocker lock(&m_mutex);
            // Reuse this handle's PINNED identity if we already registered it.
            // Recomputing hashToHex(existing) here after metadata would return the
            // (truncated) v2 hash and insert a SECOND record beside the original
            // v1 one — the exact drift that orphans v1-keyed consumers. Never do
            // that: hand back the key the handle already lives under.
            const auto pinned = m_handleKeyById.constFind(existing.id());
            if (pinned != m_handleKeyById.constEnd()) {
                dlog.info("torrent-engine",
                    QStringLiteral("addMagnet: reusing pinned key %1 for existing handle (%2)")
                        .arg(pinned.value().left(40)).arg(ecMsg));
                return pinned.value();
            }
            // First time we see this handle — register + pin it now.
            QString hash = hashToHex(existing);
            if (!m_records.contains(hash)) {
                TorrentRecord rec;
                rec.infoHash = hash;
                rec.name     = QString::fromStdString(existing.status().name);
                rec.savePath = savePath;
                rec.handle   = existing;
                m_records.insert(hash, rec);
            }
            m_handleKeyById.insert(existing.id(), hash);
            dlog.info("torrent-engine",
                QStringLiteral("addMagnet: registered existing session handle under %1 (%2)")
                    .arg(hash.left(40)).arg(ecMsg));
            return hash;
        }

        dlog.warning("torrent-engine",
            QStringLiteral("addMagnet: find_torrent fallback also failed; returning empty hash. ecMsg=%1").arg(ecMsg));
        qWarning() << "Failed to add torrent:" << ec.message().c_str();
        return {};
    }

    auto hash = hashToHex(handle);

    QMutexLocker lock(&m_mutex);
    TorrentRecord rec;
    rec.infoHash = hash;
    rec.name     = QString::fromStdString(handle.status().name);
    rec.savePath = savePath;
    rec.handle   = handle;
    m_records.insert(hash, rec);
    if (handle.is_valid()) m_handleKeyById.insert(handle.id(), hash);   // pin identity for life

    return hash;
}

QString TorrentEngine::addFromResume(const QString& resumePath,
                                      const QString& savePath, bool paused)
{
    QFile file(resumePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read resume file:" << resumePath;
        return {};
    }
    QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return {};

    lt::error_code ec;
    lt::add_torrent_params atp = lt::read_resume_data(
        lt::span<const char>(data.data(), static_cast<int>(data.size())), ec);
    if (ec) {
        qWarning() << "Failed to parse resume data:" << ec.message().c_str();
        return {};
    }

    // Override save_path with the persisted value from TorrentClient's JSON
    atp.save_path = savePath.toStdString();

    if (paused) {
        atp.flags |= lt::torrent_flags::paused;
        atp.flags &= ~lt::torrent_flags::auto_managed;
    } else {
        atp.flags &= ~lt::torrent_flags::paused;
        atp.flags |= lt::torrent_flags::auto_managed;
        atp.flags &= ~lt::torrent_flags::upload_mode;
    }

    auto handle = m_session.add_torrent(std::move(atp), ec);
    if (ec) {
        qWarning() << "Failed to add torrent from resume:" << ec.message().c_str();
        return {};
    }

    auto hash = hashToHex(handle);

    QMutexLocker lock(&m_mutex);
    TorrentRecord rec;
    rec.infoHash      = hash;
    rec.name          = QString::fromStdString(handle.status().name);
    rec.savePath      = savePath;
    rec.metadataReady = true;
    rec.handle        = handle;
    m_records.insert(hash, rec);
    if (handle.is_valid()) m_handleKeyById.insert(handle.id(), hash);   // pin identity for life

    return hash;
}

// EXTERNAL_DELETE_RECONCILE (2026-06-12) — see header. Pure file-IO probe:
// parses the .fastresume off-session, so it is safe to call before start()
// adds any handles and needs no mutex (touches no m_records / m_session).
TorrentEngine::ResumeDiskState TorrentEngine::resumeDataDiskState(
    const QString& resumePath, const QString& savePath) const
{
    ResumeDiskState st;
    QFile file(resumePath);
    if (!file.open(QIODevice::ReadOnly)) return st;
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) return st;

    lt::error_code ec;
    lt::add_torrent_params atp = lt::read_resume_data(
        lt::span<const char>(data.data(), static_cast<int>(data.size())), ec);
    if (ec) return st;
    st.parsed = true;

    // Progress evidence: any verified piece, or recorded download volume.
    st.hadProgress = atp.have_pieces.count() > 0 || atp.total_downloaded > 0;

    if (!atp.ti) return st;  // magnet without metadata — files unknown
    st.hasFileList = true;

    // libtorrent file_path() uses '\' on Windows — normalize before joining.
    // Early-exit on the first file found: a pack with some episodes deleted
    // but others alive must NOT be treated as externally deleted.
    const lt::file_storage& fs = atp.ti->files();
    const QDir base(savePath);
    for (const lt::file_index_t i : fs.file_range()) {
        QString rel = QString::fromStdString(fs.file_path(i));
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (QFileInfo::exists(base.filePath(rel))) {
            st.anyFilePresent = true;
            break;
        }
    }
    return st;
}

void TorrentEngine::setFilePriorities(const QString& infoHash, const QVector<int>& priorities)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    std::vector<lt::download_priority_t> prio;
    prio.reserve(priorities.size());
    for (int p : priorities)
        prio.push_back(static_cast<lt::download_priority_t>(p));

    it->handle.prioritize_files(prio);
}

void TorrentEngine::setSequentialDownload(const QString& infoHash, bool sequential)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    if (sequential)
        it->handle.set_flags(lt::torrent_flags::sequential_download);
    else
        it->handle.unset_flags(lt::torrent_flags::sequential_download);
}

void TorrentEngine::flattenFiles(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    auto ti = it->handle.torrent_file();
    if (!ti) return;

    auto& fs = ti->files();
    if (fs.num_files() <= 1) return;  // single file — nothing to flatten

    // Multi-file torrents have paths like "TorrentName/file.ext"
    // Strip the root folder prefix so files land directly in save_path
    std::string root = ti->name() + "/";

    for (lt::file_index_t i(0); i < lt::file_index_t(fs.num_files()); ++i) {
        std::string path = fs.file_path(i);
        if (path.size() > root.size() && path.substr(0, root.size()) == root) {
            std::string flat = path.substr(root.size());
            it->handle.rename_file(i, flat);
        }
    }
}

void TorrentEngine::startTorrent(const QString& infoHash, const QString& newSavePath)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    // Clear upload_mode that was set during metadata resolution
    it->handle.unset_flags(lt::torrent_flags::upload_mode);

    // Move storage to the user's chosen destination
    if (!newSavePath.isEmpty() && newSavePath != it->savePath) {
        it->handle.move_storage(newSavePath.toStdString());
        it->savePath = newSavePath;
    }

    it->handle.resume();
}

void TorrentEngine::moveStorage(const QString& infoHash, const QString& newSavePath)
{
    if (newSavePath.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    if (newSavePath == it->savePath) return;

    // libtorrent does the heavy lifting — move existing files, verify, then
    // serve from the new location. dont_replace preserves any pre-existing
    // files at the destination (matches qBittorrent's default behavior).
    it->handle.move_storage(newSavePath.toStdString(),
                            lt::move_flags_t::dont_replace);
    it->savePath = newSavePath;
}

void TorrentEngine::resumeTorrent(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.unset_flags(lt::torrent_flags::upload_mode);
    it->handle.set_flags(lt::torrent_flags::auto_managed);
    it->handle.resume();
}

void TorrentEngine::pauseTorrent(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.pause();
    it->handle.save_resume_data(lt::torrent_handle::save_info_dict);
}

void TorrentEngine::removeTorrent(const QString& infoHash, bool deleteFiles)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end()) return;

    if (it->handle.is_valid()) {
        lt::remove_flags_t flags{};
        if (deleteFiles)
            flags = lt::session::delete_files;
        m_session.remove_torrent(it->handle, flags);
        m_handleKeyById.remove(it->handle.id());   // drop the pinned identity too
    }

    m_records.erase(it);

    // Remove resume data file (written under the SAME canonical key, so this
    // matches even for a v1-registered hybrid whose get_best() drifted to v2).
    QFile::remove(m_cacheDir + "/resume/" + infoHash + ".fastresume");
}

void TorrentEngine::forceStart(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.unset_flags(lt::torrent_flags::upload_mode);
    it->handle.unset_flags(lt::torrent_flags::auto_managed);
    it->handle.resume();
}

void TorrentEngine::forceRecheck(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.force_recheck();
}

void TorrentEngine::forceReannounce(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.force_reannounce(0, -1);
}

void TorrentEngine::queuePositionUp(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.queue_position_up();
}

void TorrentEngine::queuePositionDown(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.queue_position_down();
}

void TorrentEngine::setQueueLimits(int maxDownloads, int maxUploads, int maxActive)
{
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::active_downloads, maxDownloads == 0 ? -1 : maxDownloads);
    sp.set_int(lt::settings_pack::active_seeds,     maxUploads   == 0 ? -1 : maxUploads);
    sp.set_int(lt::settings_pack::active_limit,     maxActive    == 0 ? -1 : maxActive);
    m_session.apply_settings(sp);
}

void TorrentEngine::setSpeedLimits(const QString& infoHash, int dlLimitBps, int ulLimitBps)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.set_download_limit(dlLimitBps);
    it->handle.set_upload_limit(ulLimitBps);
}

void TorrentEngine::setGlobalSpeedLimits(int dlLimitBps, int ulLimitBps)
{
    lt::settings_pack sp;
    sp.set_int(lt::settings_pack::download_rate_limit, dlLimitBps);
    sp.set_int(lt::settings_pack::upload_rate_limit, ulLimitBps);
    m_session.apply_settings(sp);
}

void TorrentEngine::setSeedingRules(const QString& infoHash, float ratioLimit, int seedTimeLimitSecs)
{
    QMutexLocker lock(&m_mutex);
    if (ratioLimit <= 0.f && seedTimeLimitSecs <= 0)
        m_seedingRules.remove(infoHash);
    else
        m_seedingRules[infoHash] = {ratioLimit, seedTimeLimitSecs};
}

void TorrentEngine::setGlobalSeedingRules(float ratioLimit, int seedTimeLimitSecs)
{
    QMutexLocker lock(&m_mutex);
    m_globalSeedRule = {ratioLimit, seedTimeLimitSecs};
}

void TorrentEngine::checkSeedingRules()
{
    QMutexLocker lock(&m_mutex);
    for (auto& rec : m_records) {
        if (!rec.handle.is_valid()) continue;
        auto st = rec.handle.status();
        if (st.state != lt::torrent_status::seeding) continue;
        if (st.flags & lt::torrent_flags::paused) continue;

        SeedingRule rule = m_seedingRules.value(rec.infoHash, m_globalSeedRule);
        if (rule.ratioLimit <= 0.f && rule.seedTimeSecs <= 0) continue;

        if (rule.ratioLimit > 0.f && st.total_done > 0) {
            float ratio = static_cast<float>(st.total_upload) / static_cast<float>(st.total_done);
            if (ratio >= rule.ratioLimit) {
                qDebug() << "Seeding rule: pausing" << rec.infoHash << "ratio" << ratio;
                rec.handle.pause();
                continue;
            }
        }

        if (rule.seedTimeSecs > 0 && st.seeding_duration.count() >= rule.seedTimeSecs) {
            qDebug() << "Seeding rule: pausing" << rec.infoHash << "seed time" << st.seeding_duration.count() << "s";
            rec.handle.pause();
        }
    }
}

QList<TorrentStatus> TorrentEngine::allStatuses() const
{
    QList<TorrentStatus> result;
    QMutexLocker lock(&m_mutex);

    for (auto& rec : m_records) {
        TorrentStatus ts;
        ts.infoHash = rec.infoHash;
        ts.name     = rec.name;
        ts.savePath = rec.savePath;

        if (rec.handle.is_valid()) {
            auto st = rec.handle.status();
            bool paused = !!(st.flags & lt::torrent_flags::paused);
            ts.stateString  = stateToString(st.state, paused);
            ts.progress     = st.progress;
            ts.downloadRate = st.download_rate;
            ts.uploadRate   = st.upload_rate;
            ts.numPeers     = st.num_peers;
            ts.numSeeds     = st.num_seeds;
            ts.totalDone    = st.total_done;
            ts.totalWanted  = st.total_wanted;
            ts.forceStarted  = !(st.flags & lt::torrent_flags::auto_managed)
                               && !(st.flags & lt::torrent_flags::paused);
            ts.queuePosition = static_cast<int>(st.queue_position);
            ts.dlLimit       = rec.handle.download_limit();
            ts.ulLimit       = rec.handle.upload_limit();
        } else {
            ts.stateString = QStringLiteral("unknown");
        }

        result.append(ts);
    }
    return result;
}

// F9 fix 2026-05-19: cheap presence check, no libtorrent call required.
// Returns true even when the handle is invalid — the bug we're guarding
// against is "engine has no record at all," not "engine record exists but
// handle stale."
bool TorrentEngine::hasTorrent(const QString& infoHash) const
{
    QMutexLocker lock(&m_mutex);
    return m_records.find(infoHash.toLower()) != m_records.end();
}

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — metadata presence check.
// Reads TorrentRecord::metadataReady which is set to true by the AlertWorker
// on metadata_received_alert (line ~139 above). No libtorrent call needed —
// the flag is the canonical in-process indicator that metadata has arrived.
bool TorrentEngine::hasMetadata(const QString& infoHash) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    return it != m_records.end() && it->metadataReady;
}

QJsonArray TorrentEngine::torrentFiles(const QString& infoHash) const
{
    QJsonArray files;
    QMutexLocker lock(&m_mutex);

    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return files;

    auto ti = it->handle.torrent_file();
    if (!ti) return files;

    auto& fs = ti->files();
    auto fileProgress = it->handle.file_progress();
    auto priorities   = it->handle.get_file_priorities();

    for (int i = 0; i < fs.num_files(); ++i) {
        QJsonObject f;
        f["index"]    = i;
        f["name"]     = QString::fromStdString(fs.file_path(lt::file_index_t{i}));
        f["size"]     = static_cast<qint64>(fs.file_size(lt::file_index_t{i}));
        f["progress"] = (fs.file_size(lt::file_index_t{i}) > 0 && i < static_cast<int>(fileProgress.size()))
                        ? static_cast<double>(fileProgress[i]) / fs.file_size(lt::file_index_t{i})
                        : 0.0;
        f["priority"] = (i < static_cast<int>(priorities.size()))
                        ? static_cast<int>(static_cast<std::uint8_t>(priorities[i]))
                        : 4;   // libtorrent default when out of range
        files.append(f);
    }
    return files;
}

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — per-file byte download counts.
// Mirrors torrentFiles() session/handle access pattern. piece_granularity flag
// means libtorrent accumulates completed piece bytes per file rather than
// waiting for the whole file to finish — critical for onPieceFinished callers.
QJsonArray TorrentEngine::torrentFileProgress(const QString& infoHash) const
{
    QJsonArray result;
    QMutexLocker lock(&m_mutex);

    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid())
        return result;

    auto ti = it->handle.torrent_file();
    if (!ti)
        return result;  // metadata not yet received

    std::vector<int64_t> progress;
    it->handle.file_progress(progress, lt::torrent_handle::piece_granularity);
    for (int64_t bytes : progress)
        result.append(QJsonValue::fromVariant(static_cast<qint64>(bytes)));
    return result;
}

void TorrentEngine::renameFile(const QString& infoHash, int fileIndex, const QString& newName)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    if (fileIndex < 0) return;
    it->handle.rename_file(lt::file_index_t{fileIndex}, newName.toStdString());
}

// ── Tracker management (Phase 6.3) ─────────────────────────────────────────

QList<TrackerInfo> TorrentEngine::trackersFor(const QString& infoHash) const
{
    QList<TrackerInfo> result;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return result;

    const auto trackers = it->handle.trackers();
    const auto now = lt::clock_type::now();
    for (const auto& ae : trackers) {
        TrackerInfo ti;
        ti.url  = QString::fromStdString(ae.url);
        ti.tier = ae.tier;

        // Aggregate across endpoints — take the first populated info_hash
        // entry. libtorrent splits state per-endpoint + per-protocol-version;
        // for UI display the first is close enough.
        bool populated = false;
        for (const auto& ep : ae.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (ih.updating)           ti.status = QStringLiteral("Updating");
                else if (ih.last_error)    ti.status = QStringLiteral("Error");
                else if (ih.fails > 0)     ti.status = QStringLiteral("Error");
                else if (ih.start_sent)    ti.status = QStringLiteral("Working");
                else                       ti.status = QStringLiteral("Not contacted");

                const auto secsNext = std::chrono::duration_cast<std::chrono::seconds>(
                    ih.next_announce - now).count();
                const auto secsMin = std::chrono::duration_cast<std::chrono::seconds>(
                    ih.min_announce - now).count();
                if (ih.next_announce.time_since_epoch().count() != 0)
                    ti.nextAnnounce = QDateTime::currentDateTime().addSecs(secsNext);
                if (ih.min_announce.time_since_epoch().count() != 0)
                    ti.minAnnounce = QDateTime::currentDateTime().addSecs(secsMin);

                ti.peers      = ih.scrape_incomplete;
                ti.seeds      = ih.scrape_complete;
                ti.leeches    = ih.scrape_incomplete;
                ti.downloaded = ih.scrape_downloaded;
                ti.message    = QString::fromStdString(ih.message);
                if (ti.message.isEmpty() && ih.last_error)
                    ti.message = QString::fromStdString(ih.last_error.message());
                populated = true;
                break;
            }
            if (populated) break;
        }
        if (!populated)
            ti.status = QStringLiteral("Not contacted");

        result.append(ti);
    }
    return result;
}

void TorrentEngine::addTracker(const QString& infoHash, const QString& url, int tier)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    lt::announce_entry ae(url.toStdString());
    ae.tier = tier;
    it->handle.add_tracker(ae);
}

void TorrentEngine::removeTracker(const QString& infoHash, const QString& url)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto current = it->handle.trackers();
    std::vector<lt::announce_entry> kept;
    kept.reserve(current.size());
    const std::string target = url.toStdString();
    for (auto& ae : current) {
        if (ae.url != target) kept.push_back(std::move(ae));
    }
    it->handle.replace_trackers(kept);
}

void TorrentEngine::editTrackerUrl(const QString& infoHash,
                                   const QString& oldUrl, const QString& newUrl)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto current = it->handle.trackers();
    const std::string target = oldUrl.toStdString();
    const std::string replacement = newUrl.toStdString();
    for (auto& ae : current) {
        if (ae.url == target) ae.url = replacement;
    }
    it->handle.replace_trackers(current);
}

// ── Peer management (Phase 6.4) ────────────────────────────────────────────

QList<PeerInfo> TorrentEngine::peersFor(const QString& infoHash) const
{
    QList<PeerInfo> result;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return result;

    std::vector<lt::peer_info> peers;
    it->handle.get_peer_info(peers);
    for (const auto& p : peers) {
        PeerInfo pi;
        pi.address = QString::fromStdString(p.ip.address().to_string());
        pi.port    = p.ip.port();
        pi.client  = QString::fromStdString(p.client);
        pi.country = QStringLiteral("--");  // GeoIP out of scope per identity ceiling

        QStringList fl;
        if (p.flags & lt::peer_info::remote_interested) fl << QStringLiteral("I");
        if (p.flags & lt::peer_info::rc4_encrypted)     fl << QStringLiteral("E");
        if (p.flags & lt::peer_info::utp_socket)        fl << QStringLiteral("uTP");
        pi.flags = fl.join(' ');

        pi.connection = (p.flags & lt::peer_info::utp_socket)
            ? QStringLiteral("uTP") : QStringLiteral("TCP");
        pi.progress   = p.progress;
        pi.downSpeed  = p.down_speed;
        pi.upSpeed    = p.up_speed;
        pi.downloaded = p.total_download;
        pi.uploaded   = p.total_upload;
        pi.relevance  = p.progress;   // libtorrent 2.x dropped the dedicated relevance field
        result.append(pi);
    }
    return result;
}

void TorrentEngine::banPeer(const QString& ipAddr)
{
    // Update the persisted list; apply to session filter.
    {
        QSettings s;
        QStringList list = s.value(QStringLiteral("tankorent/bannedPeers")).toStringList();
        if (!list.contains(ipAddr)) list.append(ipAddr);
        s.setValue(QStringLiteral("tankorent/bannedPeers"), list);
    }

    QMutexLocker lock(&m_mutex);
    lt::ip_filter filter = m_session.get_ip_filter();
    lt::error_code ec;
    auto addr = lt::make_address(ipAddr.toStdString(), ec);
    if (ec) return;
    filter.add_rule(addr, addr, lt::ip_filter::blocked);
    m_session.set_ip_filter(filter);
}

void TorrentEngine::addPeer(const QString& infoHash, const QString& ipPort)
{
    const int sep = ipPort.lastIndexOf(':');
    if (sep <= 0) return;
    const QString host = ipPort.left(sep);
    const quint16 port = ipPort.mid(sep + 1).toUShort();
    if (port == 0) return;

    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    lt::error_code ec;
    auto addr = lt::make_address(host.toStdString(), ec);
    if (ec) return;
    it->handle.connect_peer({addr, port});
}

QStringList TorrentEngine::bannedPeers() const
{
    return QSettings().value(QStringLiteral("tankorent/bannedPeers")).toStringList();
}

// ── General tab convenience wrapper (Phase 6.5) ────────────────────────────

TorrentDetails TorrentEngine::torrentDetails(const QString& infoHash) const
{
    TorrentDetails d;
    d.infoHash = infoHash;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return d;

    auto st = it->handle.status();
    d.savePath  = QString::fromStdString(st.save_path);
    d.name      = QString::fromStdString(st.name);
    d.totalSize = st.total_wanted;

    const float uploaded   = static_cast<float>(st.all_time_upload);
    const float downloaded = static_cast<float>(st.all_time_download);
    d.shareRatio = (downloaded > 0.f) ? uploaded / downloaded : 0.f;

    const auto nextSecs = std::chrono::duration_cast<std::chrono::seconds>(
        st.next_announce).count();
    if (nextSecs > 0)
        d.nextReannounce = QDateTime::currentDateTime().addSecs(nextSecs);

    auto ti = it->handle.torrent_file();
    if (ti) {
        d.pieceCount = ti->num_pieces();
        d.pieceSize  = ti->piece_length();
        d.createdBy  = QString::fromStdString(ti->creator());
        d.comment    = QString::fromStdString(ti->comment());
        const auto creationUnix = ti->creation_date();
        if (creationUnix > 0)
            d.created = QDateTime::fromSecsSinceEpoch(creationUnix);
    }

    const auto trackers = it->handle.trackers();
    for (const auto& ae : trackers) {
        for (const auto& ep : ae.endpoints) {
            for (const auto& ih : ep.info_hashes) {
                if (ih.start_sent) {
                    d.currentTracker = QString::fromStdString(ae.url);
                    break;
                }
            }
            if (!d.currentTracker.isEmpty()) break;
        }
        if (!d.currentTracker.isEmpty()) break;
    }
    if (d.currentTracker.isEmpty() && !trackers.empty())
        d.currentTracker = QString::fromStdString(trackers.front().url);

    // Availability: average piece availability across the swarm, 0..N range
    // where N is "copies of the file available." Use libtorrent's
    // distributed_copies as a proxy.
    d.availability = st.distributed_copies;

    return d;
}

bool TorrentEngine::haveContiguousBytes(const QString& infoHash, int fileIndex,
                                         qint64 fileOffset, qint64 length) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return false;

    auto ti = it->handle.torrent_file();
    if (!ti) return false;

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return false;

    // Use libtorrent's map_file() to get the piece range for this byte range
    // map_file returns a peer_request with piece, start, length
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    lt::peer_request endReq = ti->map_file(fi, fileOffset + length - 1, 1);

    int firstPiece = static_cast<int>(startReq.piece);
    int lastPiece = static_cast<int>(endReq.piece);

    for (int p = firstPiece; p <= lastPiece; ++p) {
        if (!it->handle.have_piece(lt::piece_index_t(p)))
            return false;
    }
    return true;
}

void TorrentEngine::flushCache(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.flush_cache();
}

// ── STREAM_PLAYBACK_FIX Phase 2 Batch 2.1 — piece-deadline primitives ─────────

void TorrentEngine::setPieceDeadlines(const QString& infoHash,
                                       const QList<QPair<int, int>>& deadlines)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;

    auto ti = it->handle.torrent_file();
    if (!ti) return;   // metadata not ready yet; caller should retry after

    const int numPieces = ti->num_pieces();
    for (const auto& [pieceIndex, msFromNow] : deadlines) {
        if (pieceIndex < 0 || pieceIndex >= numPieces) continue;
        // set_piece_deadline(piece, ms) — libtorrent tracks the urgency
        // relative to other pieces; lower ms = more urgent. API accepts
        // repeated calls for the same piece (updates the deadline).
        it->handle.set_piece_deadline(lt::piece_index_t(pieceIndex), msFromNow);
    }
}

void TorrentEngine::clearPieceDeadlines(const QString& infoHash)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    it->handle.clear_piece_deadlines();
}

QPair<int, int> TorrentEngine::pieceRangeForFileOffset(const QString& infoHash,
                                                        int fileIndex,
                                                        qint64 fileOffset,
                                                        qint64 length) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return {-1, -1};

    auto ti = it->handle.torrent_file();
    if (!ti) return {-1, -1};

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return {-1, -1};
    if (fileOffset < 0 || length <= 0) return {-1, -1};

    const qint64 fileSize = fs.file_size(fi);
    if (fileOffset >= fileSize) return {-1, -1};
    const qint64 effectiveLen = qMin(length, fileSize - fileOffset);

    // Same shape as haveContiguousBytes at line 753 — map_file returns a
    // peer_request {piece, start, length}; we take `piece` for first/last.
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    lt::peer_request endReq   = ti->map_file(fi, fileOffset + effectiveLen - 1, 1);

    return { static_cast<int>(startReq.piece),
             static_cast<int>(endReq.piece) };
}

qint64 TorrentEngine::contiguousBytesFromOffset(const QString& infoHash,
                                                 int fileIndex,
                                                 qint64 fileOffset) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return 0;

    auto ti = it->handle.torrent_file();
    if (!ti) return 0;

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return 0;

    const qint64 fileSize = fs.file_size(fi);
    if (fileOffset < 0 || fileOffset >= fileSize) return 0;

    // Walk pieces forward from the starting piece. The starting piece may
    // span before `fileOffset` if fileOffset isn't piece-aligned — we only
    // count bytes AT OR AFTER fileOffset, bounded by file end.
    lt::peer_request startReq = ti->map_file(fi, fileOffset, 1);
    const int firstPiece = static_cast<int>(startReq.piece);
    const int numPieces  = ti->num_pieces();
    const int pieceLen   = ti->piece_length();

    qint64 counted = 0;
    for (int p = firstPiece; p < numPieces; ++p) {
        if (!it->handle.have_piece(lt::piece_index_t(p))) break;

        // First piece: count from fileOffset to piece end, clamped to file.
        // Subsequent pieces: full piece length, clamped to file tail.
        // Use map_file from the OPPOSITE direction — compute the torrent-
        // absolute byte range this piece covers within the file, then the
        // byte count contributed is min(pieceEnd, fileEnd) - max(pieceStart,
        // fileOffset). For the vast majority of cases (piece fully inside
        // file, no cross-file boundary), this simplifies to full piece.
        const qint64 pieceStartAbs = static_cast<qint64>(p) * pieceLen;
        const qint64 pieceEndAbs   = pieceStartAbs + pieceLen;   // exclusive

        // Translate piece to this file's coordinates. ti->map_file gave us
        // piece + in-piece offset for a file offset; reverse mapping here
        // is done by asking: what file-byte range does this piece cover?
        // Simpler: we know startReq.start (the in-piece byte offset of the
        // requested file byte). So the first-piece contribution is:
        //   pieceLen - startReq.start  (bytes from fileOffset to piece end)
        // but clamped to fileSize - fileOffset.
        qint64 contribution;
        if (p == firstPiece) {
            contribution = qint64(pieceLen) - qint64(startReq.start);
        } else {
            contribution = pieceLen;
        }

        // Clamp so we don't overcount past the file's end (last piece may
        // span into the next file for multi-file torrents, or past EOF).
        const qint64 maxRemaining = fileSize - (fileOffset + counted);
        if (contribution > maxRemaining) contribution = maxRemaining;
        if (contribution <= 0) break;

        counted += contribution;
        if (fileOffset + counted >= fileSize) break;
    }

    // STREAM diagnostic (Agent 4B — temporary trace for stream-head-gate
    // regression; remove after that bug closes). Only logs head-offset
    // polls to avoid spam from non-zero offset range queries.
    if (fileOffset == 0) {
        const bool havePiece0 = it->handle.have_piece(lt::piece_index_t(firstPiece));
        qDebug().nospace() << "[STREAM] contig-from-head infoHash="
            << infoHash.left(8) << " file=" << fileIndex
            << " firstPiece=" << firstPiece << " havePiece0=" << havePiece0
            << " counted=" << counted << " fileSize=" << fileSize;
    }

    return counted;
}

// PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.1 — per-file contiguous-have
// byte-range projection. Feeds StreamEngine::contiguousHaveRanges, which
// feeds the SeekSlider gray-bar paint path (closes audit P0-1 buffered-range
// surface). Pure read; merges adjacent have-pieces into single spans so the
// caller repaints ~O(N_spans) rather than O(N_pieces). Semantics match the
// have_piece() walk in contiguousBytesFromOffset above (fully downloaded +
// written to disk). Agent 4B pre-offered Axis 1 HELP covers this addition.
QList<QPair<qint64, qint64>> TorrentEngine::fileByteRangesOfHavePieces(
    const QString& infoHash, int fileIndex) const
{
    QList<QPair<qint64, qint64>> result;

    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return result;

    auto ti = it->handle.torrent_file();
    if (!ti) return result;

    auto& fs = ti->files();
    lt::file_index_t fi(fileIndex);
    if (fileIndex < 0 || fileIndex >= fs.num_files()) return result;

    const qint64 fileSize = fs.file_size(fi);
    if (fileSize <= 0) return result;

    const qint64 fileAbsStart = fs.file_offset(fi);
    const qint64 fileAbsEnd   = fileAbsStart + fileSize;

    const int pieceLen = ti->piece_length();
    if (pieceLen <= 0) return result;

    // Resolve the file's piece range via the same map_file idiom used by
    // pieceRangeForFileOffset. fileSize > 0 guarantees the 1-byte lookups
    // land inside the file's byte space.
    lt::peer_request startReq = ti->map_file(fi, 0, 1);
    lt::peer_request endReq   = ti->map_file(fi, fileSize - 1, 1);
    const int firstPiece = static_cast<int>(startReq.piece);
    const int lastPiece  = static_cast<int>(endReq.piece);
    if (firstPiece < 0 || lastPiece < firstPiece) return result;

    qint64 runStart = -1;
    qint64 runEnd   = -1;
    const auto flushRun = [&]() {
        if (runStart >= 0 && runEnd > runStart) {
            result.append({ runStart, runEnd });
        }
        runStart = runEnd = -1;
    };

    for (int p = firstPiece; p <= lastPiece; ++p) {
        if (!it->handle.have_piece(lt::piece_index_t(p))) {
            flushRun();
            continue;
        }

        // Torrent-absolute byte range of this piece, intersected with the
        // file's byte space. piece_size() handles short last-piece +
        // cross-file boundary cases that pieceLen alone would miscompute.
        const qint64 pieceAbsStart = static_cast<qint64>(p) * pieceLen;
        const qint64 pieceAbsEnd   = pieceAbsStart
            + static_cast<qint64>(ti->piece_size(lt::piece_index_t(p)));
        const qint64 isectAbsStart = qMax(pieceAbsStart, fileAbsStart);
        const qint64 isectAbsEnd   = qMin(pieceAbsEnd, fileAbsEnd);
        if (isectAbsEnd <= isectAbsStart) continue;

        const qint64 localStart = isectAbsStart - fileAbsStart;
        const qint64 localEnd   = isectAbsEnd   - fileAbsStart;

        if (runStart < 0) {
            runStart = localStart;
            runEnd   = localEnd;
        } else if (localStart == runEnd) {
            runEnd = localEnd;
        } else {
            flushRun();
            runStart = localStart;
            runEnd   = localEnd;
        }
    }
    flushRun();

    return result;
}

// STREAM_ENGINE_FIX Phase 2.6.1 — per-piece have state. Diagnostic-only;
// const + lock-protected. Returns false on unknown infoHash, invalid handle,
// out-of-range pieceIdx, or any libtorrent error path. Same have_piece()
// semantics as the loop in contiguousBytesFromOffset above (line 1169) —
// piece is "have" only after fully downloaded + written to disk.
bool TorrentEngine::havePiece(const QString& infoHash, int pieceIdx) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return false;
    auto ti = it->handle.torrent_file();
    if (!ti) return false;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return false;
    return it->handle.have_piece(lt::piece_index_t(pieceIdx));
}

// STREAM_ENGINE_FIX Phase 2.6.3 — per-piece priority boost. Wraps
// libtorrent's set_piece_priority(piece, prio). Used by prepareSeekTarget to
// set priority 7 (max) on seek pieces in addition to the existing
// set_piece_deadline call — combining priority + deadline gives seek pieces
// unambiguous scheduler win. Silent no-op on unknown infoHash / invalid
// pieceIdx / out-of-range priority (libtorrent expects 0..7). Same
// lock-protected pattern as setFilePriorities.
void TorrentEngine::setPiecePriority(const QString& infoHash, int pieceIdx, int priority)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return;
    auto ti = it->handle.torrent_file();
    if (!ti) return;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return;
    if (priority < 0 || priority > 7) return;
    it->handle.piece_priority(lt::piece_index_t(pieceIdx),
                              static_cast<lt::download_priority_t>(priority));
}

// STREAM_ENGINE_REBUILD P3 — per-piece peer availability.
// Walks handle.get_peer_info() counting peers whose bitfield has pieceIdx
// set. Mirrors peersFor() at line 965 (same lock + snapshot shape). The
// piece_index_t construction is explicit because typed_bitfield<piece_index_t>
// shadows the base bitfield::operator[](int) with operator[](piece_index_t).
// Fresh-handshake peers (bitfield empty, BITFIELD/HAVE not yet received)
// are skipped — "unknown" not "no" — so R3 remains falsifiable as described
// at STREAM_ENGINE_REBUILD_TODO.md:174.
int TorrentEngine::peersWithPiece(const QString& infoHash, int pieceIdx) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return -1;

    auto ti = it->handle.torrent_file();
    if (!ti) return -1;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return -1;

    std::vector<lt::peer_info> peers;
    it->handle.get_peer_info(peers);

    const lt::piece_index_t pidx(pieceIdx);
    int count = 0;
    for (const auto& p : peers) {
        if (p.pieces.size() == 0) continue;             // unknown — not counted
        if (pieceIdx >= p.pieces.size()) continue;      // defensive; should not happen once bitfield is sized
        if (p.pieces[pidx]) ++count;
    }
    return count;
}

// STREAM_ENGINE_REBUILD 2026-04-19 — diagnostic projection for a single
// piece. Consumed by StreamEngine::onStallTick next to the existing
// stall_detected emit so the telemetry log captures WHY libtorrent isn't
// converging on a stalled piece. Walks get_download_queue() to find
// whether libtorrent is even tracking the piece + block-level state, then
// walks get_peer_info() to count peers-with-piece + peers actively
// downloading the piece + mean peer queue time. All pure reads; no
// libtorrent state mutated.
TorrentEngine::PieceDiag TorrentEngine::pieceDiagnostic(
    const QString& infoHash, int pieceIdx) const
{
    PieceDiag d;
    QMutexLocker lock(&m_mutex);
    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return d;

    auto ti = it->handle.torrent_file();
    if (!ti) return d;
    if (pieceIdx < 0 || pieceIdx >= ti->num_pieces()) return d;

    const lt::piece_index_t pidx(pieceIdx);

    // Block-level state via get_download_queue.
    std::vector<lt::partial_piece_info> dl;
    it->handle.get_download_queue(dl);
    for (const auto& pp : dl) {
        if (pp.piece_index != pidx) continue;
        d.inDownloadQueue = true;
        d.blocksInPiece   = pp.blocks_in_piece;
        d.finished        = pp.finished;
        d.writing         = pp.writing;
        d.requested       = pp.requested;
        break;
    }

    // Peer-level state.
    std::vector<lt::peer_info> peers;
    it->handle.get_peer_info(peers);
    qint64 totalQueueMs = 0;
    for (const auto& p : peers) {
        d.peerCount++;
        totalQueueMs += lt::total_milliseconds(p.download_queue_time);
        if (p.pieces.size() > 0 && pieceIdx < p.pieces.size()
            && p.pieces[pidx]) {
            d.peersWithPiece++;
        }
        if (p.downloading_piece_index == pidx) {
            d.peersDownloadingPiece++;
        }
    }
    if (d.peerCount > 0) {
        d.avgPeerQueueMs = static_cast<int>(totalQueueMs / d.peerCount);
    }
    return d;
}

// STREAM metadata investigation Wake 1 (2026-04-21) — pre-metadata-window
// diagnostic. Called at 1 Hz from StreamEngine::onMetadataFetchDiagTick
// while session state == Pending && mdReadyMs < 0. Walks handle.status() +
// handle.trackers() once per call to surface DHT / tracker / peer phase
// state so the 93-245 s magnet→metadata_received window can be diagnosed.
// Pure read; matches pieceDiagnostic's additive-read-only contract.
TorrentEngine::MetadataFetchDiag
TorrentEngine::metadataFetchDiagnostic(const QString& infoHash) const
{
    MetadataFetchDiag d;
    QMutexLocker lock(&m_mutex);
    d.dhtRunning = m_session.is_dht_running();

    auto it = m_records.find(infoHash);
    if (it == m_records.end() || !it->handle.is_valid()) return d;

    const auto st = it->handle.status();
    d.peersConnected       = st.num_peers;
    d.swarmSeeds           = st.num_complete;
    d.swarmLeechers        = st.num_incomplete;
    d.announcingToTrackers = st.announcing_to_trackers;

    // Count trackers with recent successful scrape/announce. A tracker is
    // "ok" if at least one of its endpoints has zero last_error AND has
    // either a scrape response populated (scrape_incomplete/complete > 0)
    // OR is currently updating (indicating an in-flight announce that
    // hasn't failed yet). Conservative; avoids marking a tracker "ok" on
    // pure TCP handshake with no protocol response.
    const auto trackers = it->handle.trackers();
    d.trackersTotal = static_cast<int>(trackers.size());
    for (const auto& tr : trackers) {
        bool ok = false;
        for (const auto& ih : tr.endpoints) {
            for (const auto& aih : ih.info_hashes) {
                if (!aih.last_error
                    && (aih.scrape_incomplete > 0 || aih.scrape_complete > 0
                        || aih.updating)) {
                    ok = true;
                    break;
                }
            }
            if (ok) break;
        }
        if (ok) d.trackersOk++;
    }
    return d;
}

// MOC needs to see the AlertWorker Q_OBJECT
#include "TorrentEngine.moc"

#else // !HAS_LIBTORRENT — stub implementation so the app compiles without libtorrent

TorrentEngine::TorrentEngine(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir) {}
TorrentEngine::~TorrentEngine() {}
void TorrentEngine::start() { qWarning("TorrentEngine: built without libtorrent"); }
void TorrentEngine::stop() {}
QString TorrentEngine::addMagnet(const QString&, const QString&, bool) { return {}; }
QString TorrentEngine::addFromResume(const QString&, const QString&, bool) { return {}; }
TorrentEngine::ResumeDiskState TorrentEngine::resumeDataDiskState(const QString&, const QString&) const { return {}; }
void TorrentEngine::startTorrent(const QString&, const QString&) {}
void TorrentEngine::moveStorage(const QString&, const QString&) {}
void TorrentEngine::setFilePriorities(const QString&, const QVector<int>&) {}
void TorrentEngine::renameFile(const QString&, int, const QString&) {}
QList<TrackerInfo> TorrentEngine::trackersFor(const QString&) const { return {}; }
void TorrentEngine::addTracker(const QString&, const QString&, int) {}
void TorrentEngine::removeTracker(const QString&, const QString&) {}
void TorrentEngine::editTrackerUrl(const QString&, const QString&, const QString&) {}
QList<PeerInfo> TorrentEngine::peersFor(const QString&) const { return {}; }
void TorrentEngine::banPeer(const QString&) {}
void TorrentEngine::addPeer(const QString&, const QString&) {}
QStringList TorrentEngine::bannedPeers() const { return {}; }
TorrentDetails TorrentEngine::torrentDetails(const QString&) const { return {}; }
void TorrentEngine::setSequentialDownload(const QString&, bool) {}
void TorrentEngine::flattenFiles(const QString&) {}
void TorrentEngine::resumeTorrent(const QString&) {}
void TorrentEngine::pauseTorrent(const QString&) {}
void TorrentEngine::removeTorrent(const QString&, bool) {}
void TorrentEngine::forceStart(const QString&) {}
void TorrentEngine::forceRecheck(const QString&) {}
void TorrentEngine::forceReannounce(const QString&) {}
void TorrentEngine::queuePositionUp(const QString&) {}
void TorrentEngine::queuePositionDown(const QString&) {}
void TorrentEngine::setQueueLimits(int, int, int) {}
void TorrentEngine::setSpeedLimits(const QString&, int, int) {}
void TorrentEngine::setGlobalSpeedLimits(int, int) {}
void TorrentEngine::setSeedingRules(const QString&, float, int) {}
void TorrentEngine::setGlobalSeedingRules(float, int) {}
QList<TorrentStatus> TorrentEngine::allStatuses() const { return {}; }
QJsonArray TorrentEngine::torrentFiles(const QString&) const { return {}; }
bool TorrentEngine::hasTorrent(const QString&) const { return false; }
bool TorrentEngine::hasMetadata(const QString&) const { return false; }
QJsonArray TorrentEngine::torrentFileProgress(const QString&) const { return {}; }
bool TorrentEngine::haveContiguousBytes(const QString&, int, qint64, qint64) const { return false; }
bool TorrentEngine::havePiece(const QString&, int) const { return false; }
void TorrentEngine::setPiecePriority(const QString&, int, int) {}
int TorrentEngine::peersWithPiece(const QString&, int) const { return -1; }
TorrentEngine::PieceDiag TorrentEngine::pieceDiagnostic(const QString&, int) const { return {}; }
TorrentEngine::MetadataFetchDiag TorrentEngine::metadataFetchDiagnostic(const QString&) const { return {}; }
void TorrentEngine::flushCache(const QString&) {}
// STREAM_PLAYBACK_FIX Phase 2 Batch 2.1 stubs — match header decls.
void TorrentEngine::setPieceDeadlines(const QString&, const QList<QPair<int, int>>&) {}
void TorrentEngine::clearPieceDeadlines(const QString&) {}
QPair<int, int> TorrentEngine::pieceRangeForFileOffset(const QString&, int, qint64, qint64) const { return {-1, -1}; }
qint64 TorrentEngine::contiguousBytesFromOffset(const QString&, int, qint64) const { return 0; }
QList<QPair<qint64, qint64>> TorrentEngine::fileByteRangesOfHavePieces(const QString&, int) const { return {}; }

#endif

// ── STREAM_ENGINE_FIX Phase 3.1 — default tracker pool (Agent 4B) ──────────
//
// Library-path-independent (no libtorrent call) so the accessor is defined
// once outside the HAS_LIBTORRENT branches. 25 publicly-known reliable UDP
// trackers curated as of 2026-04-18 — superset of the existing
// kFallbackTrackers list in StreamAggregator.cpp:32 so consumer migration
// preserves back-compat. Zero runtime mutation; static local + const
// reference return. Callers must not assume ordering beyond "most
// broadly-shared first" — Agent 4 Phase 3.2 append-injection can slice
// arbitrarily.
const QStringList& TorrentEngine::defaultTrackerPool()
{
    static const QStringList pool = {
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("udp://tracker.openbittorrent.com:6969/announce"),
        QStringLiteral("udp://open.stealth.si:80/announce"),
        QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
        QStringLiteral("udp://open.demonii.com:1337/announce"),
        QStringLiteral("udp://exodus.desync.com:6969/announce"),
        QStringLiteral("udp://explodie.org:6969/announce"),
        QStringLiteral("udp://tracker.dler.org:6969/announce"),
        QStringLiteral("udp://open.tracker.cl:1337/announce"),
        QStringLiteral("udp://tracker.cyberia.is:6969/announce"),
        QStringLiteral("udp://tracker.moeking.me:6969/announce"),
        QStringLiteral("udp://tracker.tiny-vps.com:6969/announce"),
        QStringLiteral("udp://tracker.theoks.net:6969/announce"),
        QStringLiteral("udp://tracker.birkenwald.de:6969/announce"),
        QStringLiteral("udp://tracker.altrosky.nl:6969/announce"),
        QStringLiteral("udp://tracker.auctor.tv:6969/announce"),
        QStringLiteral("udp://tracker.internetwarriors.net:1337/announce"),
        QStringLiteral("udp://tracker-udp.gbitt.info:80/announce"),
        QStringLiteral("udp://tracker.uw0.xyz:6969/announce"),
        QStringLiteral("udp://tracker.bittor.pw:1337/announce"),
        QStringLiteral("udp://ipv4.tracker.harry.lu:80/announce"),
        QStringLiteral("udp://retracker.lanta-net.ru:2710/announce"),
        QStringLiteral("udp://bt1.archive.org:6969/announce"),
        QStringLiteral("udp://bt2.archive.org:6969/announce"),
        QStringLiteral("udp://p4p.arenabg.com:1337/announce"),
    };
    return pool;
}
