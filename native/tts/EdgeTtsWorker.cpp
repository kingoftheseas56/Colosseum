#include "EdgeTtsWorker.h"

#include "EdgeTtsClient.h"

#include <QDebug>

EdgeTtsWorker::EdgeTtsWorker(QObject* parent) : QObject(parent) {
    // Client construction deferred to ensureClient() so QWebSocket lives on
    // the worker thread, not whatever thread invoked the ctor.
}

EdgeTtsWorker::~EdgeTtsWorker() {
    delete m_client;
    m_client = nullptr;
}

EdgeTtsClient* EdgeTtsWorker::ensureClient() {
    if (!m_client) {
        m_client = new EdgeTtsClient(this);
    }
    return m_client;
}

void EdgeTtsWorker::probe(const QString& voice) {
    EdgeTtsClient* c = ensureClient();
    const auto result = c->probe(voice);
    emit probeFinished(result.ok, result.reason);
}

void EdgeTtsWorker::getVoices() {
    emit voicesReady(ensureClient()->voicesTable());
}

void EdgeTtsWorker::synth(quint64 requestId, const QString& text,
                          const QString& voice, double rate, double pitch) {
    EdgeTtsClient* c = ensureClient();
    const auto result = c->synth(text, voice, rate, pitch);
    emit synthFinished(requestId, result.ok, result.mp3,
                       result.boundaries, result.reason);
}

void EdgeTtsWorker::synthStream(quint64 streamId, const QString& /*text*/,
                                const QString& /*voice*/, double /*rate*/,
                                double /*pitch*/) {
    // Phase 4.2 implementation. Phase 1.2 emits a structured failure so the
    // JS bridge resolves without hanging.
    emit streamError(streamId, QStringLiteral("phase_4_pending"));
}

void EdgeTtsWorker::cancelStream(quint64 streamId) {
    // Phase 4.2 wires real cancellation. Phase 1.2: ack with streamEnded so
    // the JS bridge's _activeStreamId tracker clears cleanly.
    emit streamEnded(streamId);
}

void EdgeTtsWorker::warmup() {
    // Pre-warm the WSS connection by running a probe. tts_core.js init() calls
    // this on the side after engine selection to reduce first-real-listen
    // latency. Result is informational; failure does not surface as a user
    // error (it's a hint-style optimization).
    EdgeTtsClient* c = ensureClient();
    const auto result = c->probe(QStringLiteral("en-US-AndrewNeural"));
    emit warmupFinished(result.ok, result.reason);
}

void EdgeTtsWorker::resetInstance() {
    // Force a clean WSS reconnect on next call. JS bridge invokes this when
    // the JS engine wants to recover from a bad state without rebuilding the
    // whole worker thread.
    delete m_client;
    m_client = nullptr;
    emit resetFinished();
}
