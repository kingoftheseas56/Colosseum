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
class AudiobookDownloader;
class AudioPairingStore;

class BookBridge : public QObject {
    Q_OBJECT
public:
    explicit BookBridge(QObject* parent = nullptr);
    ~BookBridge() override;

    // Read-along dependencies, injected from main.cpp after construction (the
    // downloader + pairing store are built later in the wiring order). The reader's
    // Audio tab reaches the SAME instances QML uses, so a pairing saved in the
    // reader is the one the auto-load path reads back.
    void setAudiobooks(AudiobookDownloader* books) { m_audiobooks = books; }
    void setPairing(AudioPairingStore* pairing) { m_pairing = pairing; }

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

    // ── audiobook read-along (the reader's Audio tab) ──
    // The library: every downloaded audiobook with its chapter list already
    // assembled — [ { id (pairKey), title, author, chapters: [ { title } ] } ].
    // Chapters are derived from the on-disk audio files (localFiles), so the tab
    // can offer a per-chapter mapping without a second round-trip.
    Q_INVOKABLE QJsonArray audiobookLibrary();
    // Pairing persistence — a thin pass-through to AudioPairingStore (the same
    // store QML's auto-load path reads), so the Audio tab and QML agree.
    Q_INVOKABLE QJsonObject audiobookPairingGet(const QString& bookId);
    Q_INVOKABLE void audiobookPairingSave(const QString& bookId, const QJsonObject& pairing);
    Q_INVOKABLE void audiobookPairingDelete(const QString& bookId);
    // Commands routed to the docked AudiobookSession via signals (QML owns the
    // player; the reader iframe can only reach it through the bridge). chapterIndex
    // < 0 means "load and resume wherever it left off" (no chapter jump).
    Q_INVOKABLE void audiobookLoadAtChapter(const QString& pairKey, int chapterIndex);
    Q_INVOKABLE void audiobookClose();

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
    // Emitted on every foliate progress save so QML can feed the Continue/resume
    // store (`Progress`). fraction is the 0..1 reading position foliate reports.
    void progressSaved(const QString& bookId, double fraction);
    void windowMinimizeRequested();
    void windowMaximizeToggleRequested();
    void windowCloseRequested();
    void windowMaximizeChanged(bool isMax);

    // Read-along: the reader asks the docked AudiobookSession (in QML) to load a
    // paired audiobook (optionally at a chapter) or to drop the stream on unlink.
    void audiobookLoadRequested(const QString& pairKey, int chapterIndex);
    void audiobookCloseRequested();

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
    // The actual file I/O (readStore/writeStore/get/save/listGet/listSave/
    // listDelete/listClear) now lives in BookStores (native/reader/BookStores.h),
    // shared with reader2 so both readers hit the SAME files byte-for-byte.

    bool m_fullscreen = true;     // Colosseum is a fullscreen surface by default
    bool m_isMaximized = true;

    // ── read-along deps (injected; may be null if wiring is absent) ──
    AudiobookDownloader* m_audiobooks = nullptr;
    AudioPairingStore* m_pairing = nullptr;

    // ── Edge TTS worker (own thread; QWebSocket must live off the GUI thread) ──
    QThread* m_ttsThread = nullptr;
    EdgeTtsWorker* m_ttsWorker = nullptr;
    quint64 m_pendingProbeReqId = 0;
    quint64 m_pendingVoicesReqId = 0;
    quint64 m_pendingWarmupReqId = 0;
    quint64 m_pendingResetReqId = 0;
};
