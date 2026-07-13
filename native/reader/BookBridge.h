// BookBridge.h
//
// QWebChannel bridge object exposed to the foliate EPUB reader's JS as "bridge"
// (a JS shim maps it to window.electronAPI + window.__ebookNav). This is the
// Colosseum-native port of Tankoban 2's BookBridge: the JS-facing API is IDENTICAL
// (so TB2's book_reader/ web app runs unchanged), but the guts differ —
//   - persistence: a small self-contained JSON store under <appdata>/book_reader/
//     (progress / settings / bookmarks / annotations / display-names), NOT TB2's
//     CoreBridge → JsonStore.
//   - window chrome: emits signals the QML reader layer handles (minimize / close /
//     fullscreen), since Colosseum is an all-Quick frameless surface.
//   - Edge TTS: STUBBED — every *Start immediately answers its *Finished with
//     {ok:false} so the reader's read-aloud reports "unavailable" instead of
//     hanging on an unresolved Promise. (Port the EdgeTtsWorker later if wanted.)

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>

class EdgeTtsWorker;
class QThread;

class BookBridge : public QObject {
    Q_OBJECT
public:
    explicit BookBridge(QObject* parent = nullptr);
    ~BookBridge() override;

    // ── files ──
    Q_INVOKABLE QByteArray filesRead(const QString& filePath);

    // ── booksProgress ── (SHA1[:20] of the forward-slash-normalized path = bookId)
    Q_INVOKABLE QString progressKey(const QString& absPath) const;
    Q_INVOKABLE QJsonObject booksProgressGet(const QString& bookId);
    Q_INVOKABLE void booksProgressSave(const QString& bookId, const QJsonObject& data);

    // ── booksSettings ── (global flat bag; get() returns { settings: <obj> })
    Q_INVOKABLE QJsonObject booksSettingsGet();
    Q_INVOKABLE void booksSettingsSave(const QJsonObject& data);

    // ── booksBookmarks ──
    Q_INVOKABLE QJsonArray booksBookmarksGet(const QString& bookId);
    Q_INVOKABLE QJsonObject booksBookmarksSave(const QString& bookId, const QJsonObject& bookmark);
    Q_INVOKABLE QJsonObject booksBookmarksDelete(const QString& bookId, const QString& bookmarkId);
    Q_INVOKABLE void booksBookmarksClear(const QString& bookId);

    // ── booksAnnotations ──
    Q_INVOKABLE QJsonArray booksAnnotationsGet(const QString& bookId);
    Q_INVOKABLE QJsonObject booksAnnotationsSave(const QString& bookId, const QJsonObject& annotation);
    Q_INVOKABLE QJsonObject booksAnnotationsDelete(const QString& bookId, const QString& annotationId);
    Q_INVOKABLE void booksAnnotationsClear(const QString& bookId);

    // ── booksDisplayNames ──
    Q_INVOKABLE QJsonObject booksDisplayNamesGetAll();
    Q_INVOKABLE void booksDisplayNamesSave(const QString& bookId, const QString& name);
    Q_INVOKABLE void booksDisplayNamesDelete(const QString& bookId);

    // ── window chrome (routed to QML via signals) ──
    Q_INVOKABLE bool windowIsFullscreen() const;
    Q_INVOKABLE QJsonObject windowToggleFullscreen();
    Q_INVOKABLE void windowMinimize();
    Q_INVOKABLE void windowToggleMaximize();
    Q_INVOKABLE void windowClose();
    Q_INVOKABLE bool windowIsMaximized() const;
    void emitWindowMaximizeChanged(bool isMax);
    void setFullscreen(bool fs);

    // ── navigation + readiness ──
    Q_INVOKABLE void requestClose();      // BACK to library
    Q_INVOKABLE void markReaderReady();   // foliate's `stabilized` → fade the loading overlay
    Q_INVOKABLE void requestListen();     // pill's "listen (audiobook)" → QML mounts the strip

    // ── Edge TTS (STUBBED — answers each *Start with {ok:false}) ──
    Q_INVOKABLE void booksTtsEdgeProbeStart(quint64 reqId, const QString& voice);
    Q_INVOKABLE void booksTtsEdgeGetVoicesStart(quint64 reqId);
    Q_INVOKABLE void booksTtsEdgeSynthStart(quint64 reqId, const QString& text,
                                            const QString& voice, double rate, double pitch);
    Q_INVOKABLE void booksTtsEdgeSynthStreamStart(quint64 reqId, const QString& text,
                                                  const QString& voice, double rate, double pitch);
    Q_INVOKABLE void booksTtsEdgeCancelStream(quint64 streamId);
    Q_INVOKABLE void booksTtsEdgeWarmupStart(quint64 reqId);
    Q_INVOKABLE void booksTtsEdgeResetStart(quint64 reqId);

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);
    void readerReady();
    // HTML pill asked for the audiobook strip. Pure signal — no payload; the QML
    // reader layer already knows the open book's pairKey.
    void listenRequested();
    // Emitted on every foliate progress save so QML can feed the Continue/resume
    // store (`Progress`). fraction is the 0..1 reading position foliate reports.
    void progressSaved(const QString& bookId, double fraction);
    void windowMinimizeRequested();
    void windowMaximizeToggleRequested();
    void windowCloseRequested();
    void windowMaximizeChanged(bool isMax);

    void booksTtsEdgeProbeFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeVoicesReady(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeSynthFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeSynthStreamFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeWarmupFinished(quint64 reqId, const QJsonObject& result);
    void booksTtsEdgeResetFinished(quint64 reqId, const QJsonObject& result);

private slots:
    // Edge-TTS worker → bridge → JS: translate worker signals into the {ok,...} JSON
    // the reader's tts_engine_edge.js expects (audioBase64 + boundaries on synth).
    void onWorkerProbeFinished(bool ok, const QString& reason);
    void onWorkerVoicesReady(const QJsonArray& voices);
    void onWorkerSynthFinished(quint64 requestId, bool ok, const QByteArray& mp3,
                               const QJsonArray& boundaries, const QString& reason);
    void onWorkerStreamError(quint64 streamId, const QString& reason);
    void onWorkerStreamEnded(quint64 streamId);
    void onWorkerWarmupFinished(bool ok, const QString& reason);
    void onWorkerResetFinished();

private:
    // ── self-contained JSON store under <appdata>/book_reader/ ──
    QString stateDir() const;
    QJsonObject readStore(const QString& file) const;
    void writeStore(const QString& file, const QJsonObject& obj) const;
    // bookmarks/annotations share the same {bookId: [items]} shape + save/delete logic.
    QJsonArray listGet(const QString& file, const QString& bookId) const;
    QJsonObject listSave(const QString& file, const QString& bookId, QJsonObject item);
    QJsonObject listDelete(const QString& file, const QString& bookId, const QString& itemId);
    void listClear(const QString& file, const QString& bookId);

    bool m_fullscreen = true;     // Colosseum is a fullscreen surface by default
    bool m_isMaximized = true;

    // ── Edge TTS worker (own thread; QWebSocket must live off the GUI thread) ──
    QThread* m_ttsThread = nullptr;
    EdgeTtsWorker* m_ttsWorker = nullptr;
    quint64 m_pendingProbeReqId = 0;
    quint64 m_pendingVoicesReqId = 0;
    quint64 m_pendingWarmupReqId = 0;
    quint64 m_pendingResetReqId = 0;
};
