#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QObject>
#include <QString>

class EdgeTtsClient;

// EdgeTtsWorker — QObject wrapper around EdgeTtsClient. Designed to be moved
// onto a QThread (BookReader.cpp owns the QThread; this class is the thread's
// payload). Slots dispatch via Qt::QueuedConnection by default for cross-thread
// invocations from BookReader.
//
// All `requestId` / `streamId` arguments are caller-provided; the worker echoes
// them back in the corresponding `*Finished` / `*Ended` / `*Error` signal so
// the JS bridge can correlate replies with pending Promise resolvers.
//
// The owned EdgeTtsClient is constructed lazily on the worker thread (inside
// `ensureClient()`) so QWebSocket lives on the right thread context. Calling
// any slot triggers construction; `resetInstance()` destroys + recreates.
class EdgeTtsWorker : public QObject {
    Q_OBJECT
public:
    explicit EdgeTtsWorker(QObject* parent = nullptr);
    ~EdgeTtsWorker() override;

signals:
    // Phase 1 signals.
    void probeFinished(bool ok, const QString& reason);
    void voicesReady(const QJsonArray& voices);

    // Phase 2 signals (wired Phase 2.1; Phase 1.2 emits phase_2_pending reasons).
    void synthFinished(quint64 requestId, bool ok, const QByteArray& mp3,
                       const QJsonArray& boundaries, const QString& reason);

    // Phase 4 signals (wired Phase 4.2; Phase 1.2 emits phase_4_pending via streamError).
    void streamChunk(quint64 streamId, const QByteArray& mp3Chunk);
    void streamBound(quint64 streamId, qint64 audioOffsetMs,
                     qint64 textOffset, qint64 textLength);
    void streamEnded(quint64 streamId);
    void streamError(quint64 streamId, const QString& reason);

    // Lifecycle ack signals — emitted from warmup/resetInstance for the JS
    // bridge to resolve its Promise without polling.
    void warmupFinished(bool ok, const QString& reason);
    void resetFinished();

public slots:
    void probe(const QString& voice);
    void getVoices();
    void synth(quint64 requestId, const QString& text, const QString& voice,
               double rate, double pitch);
    void synthStream(quint64 streamId, const QString& text, const QString& voice,
                     double rate, double pitch);
    void cancelStream(quint64 streamId);
    void warmup();
    void resetInstance();

private:
    EdgeTtsClient* ensureClient();

    EdgeTtsClient* m_client = nullptr;  // owned; lives on this thread
};
