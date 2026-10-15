#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>

#include <list>
#include <utility>

#ifdef HAS_WEBSOCKETS
#include <QtWebSockets/QWebSocket>
#endif

class QEventLoop;

// EdgeTtsClient — Qt-side direct client for Microsoft Edge's Read Aloud
// (consumer) WebSocket TTS service. Lives on a worker thread (see EdgeTtsWorker
// in Phase 1.2) so its synchronous-style API can use a local QEventLoop without
// blocking the Qt event loop.
//
// Behavior reference (no code copied):
//  - Readest src/libs/edgeTTS.ts (MIT)
//  - rany2/edge-tts (Python, GPLv3 — protocol behavior only)
//
// Phase 1.1 scope: WSS connect + Sec-MS-GEC token gen + speech.config + SSML
// send + probe round-trip + static voice table. synth() returns
// {ok:false, reason:"phase_2_pending"} until Phase 2 wires the audio
// accumulation + boundary parsing + base64 return path.
class EdgeTtsClient : public QObject {
    Q_OBJECT
public:
    struct ProbeResult {
        bool ok = false;
        QString reason;  // empty on success; enum-like string on failure
    };

    struct SynthResult {
        bool ok = false;
        QByteArray mp3;          // empty on Phase 1; populated in Phase 2
        QJsonArray boundaries;   // sentence/word boundary metadata; Phase 2+
        QString reason;
    };

    explicit EdgeTtsClient(QObject* parent = nullptr);
    ~EdgeTtsClient() override;

    // Static voice table (en-US / en-GB / en-AU / en-IN neural voices). Each
    // entry is {name, locale, gender, displayName}. Built once at construction.
    QJsonArray voicesTable() const { return m_voices; }

    // Probe — opens WSS, sends a 3-5 word synthesis request, waits for first
    // audio frame + turn.end, closes. Used by tts_core.js init() to verify
    // the bridge is reachable before populating the voice picker.
    ProbeResult probe(const QString& voice);

    // Synth — Phase 2 stub.
    SynthResult synth(const QString& text, const QString& voice,
                      double rate, double pitch);

    // Static helpers — token + URL primitives. Exposed for unit-test
    // verifiability (deterministic given a fixed timestamp).
    //
    // Sec-MS-GEC algorithm (from rany2/edge-tts drm.py behavior):
    //   1. Take Unix epoch seconds, add 11644473600 (file-time epoch shift).
    //   2. Multiply by 10_000_000 (file-time tick = 100ns).
    //   3. Floor to nearest 5-minute boundary (3_000_000_000 ticks).
    //   4. Concatenate as decimal string with TRUSTED_CLIENT_TOKEN.
    //   5. SHA256 the ASCII bytes; uppercase hex digest.
    static QString generateSecMSGEC(qint64 unixSecondsSinceEpoch);
    static QString trustedClientToken();
    static QString wssBaseUrl();
    static QString edgeVersion();      // for Sec-MS-GEC-Version query param
    static QString edgeUserAgent();    // for WSS handshake header
    static QString edgeOrigin();       // for WSS handshake header

private:
#ifdef HAS_WEBSOCKETS
    // Round-trip outcome shared by probe() + synth(). Bundles the binary
    // signals (audio received? turn.end seen?) + accumulated MP3 + parsed
    // WordBoundary entries + per-call structured failure reason.
    struct RoundTripOutcome {
        bool gotAudio = false;
        bool gotTurnEnd = false;
        QByteArray mp3;          // accumulated audio frames
        QJsonArray boundaries;   // [{text, offsetMs}, ...] from audio.metadata
        QString errorReason;     // non-empty on early-exit failure
    };

    // Connection lifecycle. Lazy: opened on first probe/synth, kept alive for
    // potential reuse, torn down on destructor or explicit reset.
    bool ensureSocketOpen();
    void closeSocket();

    // Single round-trip — sends speech.config + SSML, accumulates audio binary
    // frames + watches for turn.end, exits on success / error / timeout.
    // Caller decides how strict to be on missing turn.end.
    RoundTripOutcome runRoundTrip(const QString& text, const QString& voice,
                                  double rate, double pitch, int timeoutMs);

    // Edge WSS protocol message builders. Both produce text frames with
    // \r\n-separated headers + body; rendered as a single QString and sent
    // via QWebSocket::sendTextMessage.
    QString buildSpeechConfigMessage(const QString& requestId,
                                     const QString& outputFormat);
    QString buildSsmlMessage(const QString& requestId, const QString& text,
                             const QString& voice, double rate, double pitch);

    // Binary frame parsing. Edge wraps audio chunks as:
    //   [2 bytes: header length BE uint16][header text][audio bytes]
    // Returns the audio payload + populates outPath with the Path: header
    // value (e.g., "audio" or "audio.metadata"); returns empty bytes if frame
    // is malformed.
    QByteArray parseBinaryFrame(const QByteArray& raw, QString* outPath);

    // Helpers.
    static QString generateConnectionId();   // 32-char lowercase hex (no dashes)
    static QString generateRequestId();      // same shape; per-request
    static QString generateMuidCookie();
    static QString edgeIso8601Timestamp();
    static QString sanitizeForXml(const QString& raw);
    static QString rateToEdgePercent(double rate);   // 1.0 -> "+0%", 1.5 -> "+50%"
    static QString pitchToEdgeHz(double pitch);      // 1.0 -> "+0Hz", future use

    QWebSocket* m_socket = nullptr;
#endif

    void buildVoiceTable();
    QJsonArray m_voices;

    // ── LRU cache (EDGE_TTS_FIX Phase 3.1) ──────────────────────────────────
    // Strict LRU per Agent 2's design call (Readest pattern, simpler than
    // LRU-with-frequency). Key = SHA1(text|voice|rate|pitch|format) hex.
    // Value = MP3 bytes + boundary metadata (boundaries empty in Phase 2;
    // populated in Phase 4 streaming if pursued).
    //
    // Implementation: `std::list` for recency ordering (most-recent at front)
    // + `QHash` for O(1) key→list-iterator lookup. On hit, splice the entry
    // to front. On insert past capacity, drop the back.
    struct CacheEntry {
        QByteArray mp3;
        QJsonArray boundaries;
    };
    using CacheList = std::list<std::pair<QByteArray, CacheEntry>>;

    QByteArray makeCacheKey(const QString& text, const QString& voice,
                            double rate, double pitch,
                            const QString& outputFormat) const;
    bool cacheLookup(const QByteArray& key, CacheEntry* out);
    void cacheInsert(const QByteArray& key, CacheEntry entry);

    static constexpr int kCacheCapacity = 200;
    CacheList m_cacheList;
    QHash<QByteArray, CacheList::iterator> m_cacheIndex;
};
