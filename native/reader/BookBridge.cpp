#include "BookBridge.h"
#include "BookStores.h"
#include "../tts/EdgeTtsWorker.h"
#include "../engine/AudiobookDownloader.h"
#include "../AudioPairingStore.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

BookBridge::BookBridge(QObject* parent) : QObject(parent)
{
    // Edge-TTS worker on its own thread (QWebSocket must live off the GUI thread).
    // Lifetime = the reader bridge; torn down in the dtor. Ported from Tankoban 2.
    m_ttsThread = new QThread(this);
    m_ttsWorker = new EdgeTtsWorker;
    m_ttsWorker->moveToThread(m_ttsThread);
    connect(m_ttsWorker, &EdgeTtsWorker::probeFinished,  this, &BookBridge::onWorkerProbeFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::voicesReady,    this, &BookBridge::onWorkerVoicesReady);
    connect(m_ttsWorker, &EdgeTtsWorker::synthFinished,  this, &BookBridge::onWorkerSynthFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::streamError,    this, &BookBridge::onWorkerStreamError);
    connect(m_ttsWorker, &EdgeTtsWorker::streamEnded,    this, &BookBridge::onWorkerStreamEnded);
    connect(m_ttsWorker, &EdgeTtsWorker::warmupFinished, this, &BookBridge::onWorkerWarmupFinished);
    connect(m_ttsWorker, &EdgeTtsWorker::resetFinished,  this, &BookBridge::onWorkerResetFinished);
    m_ttsThread->setObjectName(QStringLiteral("EdgeTtsThread"));
    m_ttsThread->start();
}

BookBridge::~BookBridge()
{
    if (m_ttsThread) { m_ttsThread->quit(); m_ttsThread->wait(5000); }
    delete m_ttsWorker;
    m_ttsWorker = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// files
// ─────────────────────────────────────────────────────────────────────────────

// Returns the file BASE64-encoded. QWebChannel marshals a QByteArray return value by
// UTF-8-decoding it into a JS string, which CORRUPTS binary (a raw .epub's bytes are not
// valid UTF-8) — so the reader's byte-converters saw a lossy string and silently fell back
// to fetch(file://). Base64 is pure ASCII, survives the string transfer intact, and the JS
// side (toArrayBuffer / txt decoder) atob-decodes it. This makes the native bridge — which
// also self-heals stale paths below — the reliable read path instead of fragile file:// fetch.
QByteArray BookBridge::filesRead(const QString& filePath)
{
    QString p = filePath;
    if (p.startsWith(QStringLiteral("file:///"))) p = QUrl(p).toLocalFile();
    QFile f(p);
    if (f.open(QIODevice::ReadOnly)) return f.readAll().toBase64();

    // Self-heal a stale app-data path (the org-name migration Roaming/Colosseum ->
    // Roaming/Brotherhood/Colosseum moved the tree but left absolute paths — in the
    // library index AND in resume/Continue cards — pointing at the vanished old dir).
    // Re-root the segment after the last ".../Colosseum/" onto the CURRENT app-data dir.
    static const QString kAnchor = QStringLiteral("/Colosseum/");
    const int idx = p.lastIndexOf(kAnchor);
    if (idx >= 0) {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString rerooted = appData + QStringLiteral("/") + p.mid(idx + kAnchor.size());
        if (rerooted != p) {
            QFile g(rerooted);
            if (g.open(QIODevice::ReadOnly)) return g.readAll().toBase64();
        }
    }
    return {};
}

// Delegates to BookStores::keyFor — the ONE derivation shared with the fresh reader
// (Reader2Bridge::bookKey), so the zero-migration key can't drift between readers.
QString BookBridge::progressKey(const QString& absPath) const
{
    return BookStores::keyFor(absPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON store — delegates to BookStores (native/reader/BookStores.h), shared
// with reader2. SAME files, SAME shapes; only the call site moved.
// ─────────────────────────────────────────────────────────────────────────────

// ── progress ──

QJsonObject BookBridge::booksProgressGet(const QString& bookId)
{
    return BookStores::get(QStringLiteral("progress.json"), bookId);
}

void BookBridge::booksProgressSave(const QString& bookId, const QJsonObject& data)
{
    BookStores::save(QStringLiteral("progress.json"), bookId, data);
    emit progressSaved(bookId, data.value(QStringLiteral("fraction")).toDouble());
}

// ── settings (global flat bag) ──

QJsonObject BookBridge::booksSettingsGet()
{
    QJsonObject wrap;
    wrap[QStringLiteral("settings")] = BookStores::readStore(QStringLiteral("settings.json"));
    return wrap;
}

void BookBridge::booksSettingsSave(const QJsonObject& data)
{
    BookStores::writeStore(QStringLiteral("settings.json"), data);
}

// ── shared {bookId: [items]} list logic for bookmarks + annotations ──

QJsonArray  BookBridge::booksBookmarksGet(const QString& bookId)            { return BookStores::listGet(QStringLiteral("bookmarks.json"), bookId); }
QJsonObject BookBridge::booksBookmarksSave(const QString& bookId, const QJsonObject& bm)   { return BookStores::listSave(QStringLiteral("bookmarks.json"), bookId, bm); }
QJsonObject BookBridge::booksBookmarksDelete(const QString& bookId, const QString& bmId)   { return BookStores::listDelete(QStringLiteral("bookmarks.json"), bookId, bmId); }
void        BookBridge::booksBookmarksClear(const QString& bookId)          { BookStores::listClear(QStringLiteral("bookmarks.json"), bookId); }

QJsonArray  BookBridge::booksAnnotationsGet(const QString& bookId)          { return BookStores::listGet(QStringLiteral("annotations.json"), bookId); }
QJsonObject BookBridge::booksAnnotationsSave(const QString& bookId, const QJsonObject& an) { return BookStores::listSave(QStringLiteral("annotations.json"), bookId, an); }
QJsonObject BookBridge::booksAnnotationsDelete(const QString& bookId, const QString& anId) { return BookStores::listDelete(QStringLiteral("annotations.json"), bookId, anId); }
void        BookBridge::booksAnnotationsClear(const QString& bookId)        { BookStores::listClear(QStringLiteral("annotations.json"), bookId); }

// ── display names ──

QJsonObject BookBridge::booksDisplayNamesGetAll()
{
    return BookStores::readStore(QStringLiteral("display_names.json"));
}

void BookBridge::booksDisplayNamesSave(const QString& bookId, const QString& name)
{
    QJsonObject all = BookStores::readStore(QStringLiteral("display_names.json"));
    all[bookId] = name;
    BookStores::writeStore(QStringLiteral("display_names.json"), all);
}

void BookBridge::booksDisplayNamesDelete(const QString& bookId)
{
    QJsonObject all = BookStores::readStore(QStringLiteral("display_names.json"));
    all.remove(bookId);
    BookStores::writeStore(QStringLiteral("display_names.json"), all);
}

// ─────────────────────────────────────────────────────────────────────────────
// audiobook read-along (the reader's Audio tab)
// ─────────────────────────────────────────────────────────────────────────────

QJsonArray BookBridge::audiobookLibrary()
{
    QJsonArray out;
    if (!m_audiobooks)
        return out;
    const QVariantList books = m_audiobooks->downloadedAudiobooks();
    for (const QVariant& v : books) {
        const QVariantMap b = v.toMap();
        const QString id = b.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        // Chapters = the on-disk audio files, in order, titled by filename stem.
        QJsonArray chapters;
        const QStringList files = m_audiobooks->localFiles(id);
        for (const QString& path : files) {
            QJsonObject ch;
            ch[QStringLiteral("title")] = QFileInfo(path).completeBaseName();
            chapters.append(ch);
        }
        QJsonObject rec;
        rec[QStringLiteral("id")]       = id;
        rec[QStringLiteral("title")]    = b.value(QStringLiteral("title")).toString();
        rec[QStringLiteral("author")]   = b.value(QStringLiteral("author")).toString();
        rec[QStringLiteral("chapters")] = chapters;
        out.append(rec);
    }
    return out;
}

QJsonObject BookBridge::audiobookPairingGet(const QString& bookId)
{
    if (!m_pairing)
        return {};
    return QJsonObject::fromVariantMap(m_pairing->getPairing(bookId));
}

void BookBridge::audiobookPairingSave(const QString& bookId, const QJsonObject& pairing)
{
    if (!m_pairing)
        return;
    m_pairing->savePairing(bookId, pairing.toVariantMap());
}

void BookBridge::audiobookPairingDelete(const QString& bookId)
{
    if (m_pairing)
        m_pairing->deletePairing(bookId);
}

void BookBridge::audiobookLoadAtChapter(const QString& pairKey, int chapterIndex)
{
    if (pairKey.isEmpty())
        return;
    emit audiobookLoadRequested(pairKey, chapterIndex);
}

void BookBridge::audiobookClose()
{
    emit audiobookCloseRequested();
}

// ─────────────────────────────────────────────────────────────────────────────
// window chrome
// ─────────────────────────────────────────────────────────────────────────────

bool BookBridge::windowIsFullscreen() const { return m_fullscreen; }

QJsonObject BookBridge::windowToggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    emit fullscreenRequested(m_fullscreen);
    return QJsonObject{{QStringLiteral("fullscreen"), m_fullscreen}};
}

void BookBridge::windowMinimize()       { emit windowMinimizeRequested(); }
void BookBridge::windowToggleMaximize() { emit windowMaximizeToggleRequested(); }
void BookBridge::windowClose()          { emit windowCloseRequested(); }
bool BookBridge::windowIsMaximized() const { return m_isMaximized; }

void BookBridge::emitWindowMaximizeChanged(bool isMax)
{
    m_isMaximized = isMax;
    emit windowMaximizeChanged(isMax);
}

void BookBridge::setFullscreen(bool fs) { m_fullscreen = fs; }

void BookBridge::requestClose()    { emit closeRequested(); }
void BookBridge::markReaderReady() { emit readerReady(); }

// ─────────────────────────────────────────────────────────────────────────────
// Edge TTS — LIVE (ported from Tankoban 2). *Start dispatches to the worker thread
// via QueuedConnection; the worker's *Finished signals land in onWorker* below,
// which build the {ok, audioBase64, boundaries} JSON the reader's TTS JS consumes.
// Single-pending discipline for probe/voices/warmup/reset (issued one-at-a-time by
// tts_core.js init()); synth/synthStream carry their reqId/streamId through the worker.
// ─────────────────────────────────────────────────────────────────────────────

void BookBridge::booksTtsEdgeProbeStart(quint64 reqId, const QString& voice)
{
    m_pendingProbeReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "probe", Qt::QueuedConnection, Q_ARG(QString, voice));
}
void BookBridge::booksTtsEdgeGetVoicesStart(quint64 reqId)
{
    m_pendingVoicesReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "getVoices", Qt::QueuedConnection);
}
void BookBridge::booksTtsEdgeSynthStart(quint64 reqId, const QString& text,
                                        const QString& voice, double rate, double pitch)
{
    QMetaObject::invokeMethod(m_ttsWorker, "synth", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, text),
                              Q_ARG(QString, voice), Q_ARG(double, rate), Q_ARG(double, pitch));
}
void BookBridge::booksTtsEdgeSynthStreamStart(quint64 reqId, const QString& text,
                                              const QString& voice, double rate, double pitch)
{
    QMetaObject::invokeMethod(m_ttsWorker, "synthStream", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, text),
                              Q_ARG(QString, voice), Q_ARG(double, rate), Q_ARG(double, pitch));
}
void BookBridge::booksTtsEdgeCancelStream(quint64 streamId)
{
    QMetaObject::invokeMethod(m_ttsWorker, "cancelStream", Qt::QueuedConnection, Q_ARG(quint64, streamId));
}
void BookBridge::booksTtsEdgeWarmupStart(quint64 reqId)
{
    m_pendingWarmupReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "warmup", Qt::QueuedConnection);
}
void BookBridge::booksTtsEdgeResetStart(quint64 reqId)
{
    m_pendingResetReqId = reqId;
    QMetaObject::invokeMethod(m_ttsWorker, "resetInstance", Qt::QueuedConnection);
}

// ── worker → bridge → JS dispatch ─────────────────────────────────────────────

void BookBridge::onWorkerProbeFinished(bool ok, const QString& reason)
{
    QJsonObject result{{QStringLiteral("ok"), ok}};
    if (ok) result.insert(QStringLiteral("available"), true);   // JS checks res.ok && res.available
    if (!reason.isEmpty()) result.insert(QStringLiteral("reason"), reason);
    const quint64 reqId = m_pendingProbeReqId; m_pendingProbeReqId = 0;
    emit booksTtsEdgeProbeFinished(reqId, result);
}
void BookBridge::onWorkerVoicesReady(const QJsonArray& voices)
{
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("voices"), voices}};
    const quint64 reqId = m_pendingVoicesReqId; m_pendingVoicesReqId = 0;
    emit booksTtsEdgeVoicesReady(reqId, result);
}
void BookBridge::onWorkerSynthFinished(quint64 requestId, bool ok, const QByteArray& mp3,
                                       const QJsonArray& boundaries, const QString& reason)
{
    QJsonObject result{{QStringLiteral("ok"), ok}};
    if (ok) {
        result.insert(QStringLiteral("audioBase64"), QString::fromLatin1(mp3.toBase64()));
        result.insert(QStringLiteral("boundaries"), boundaries);
    }
    if (!reason.isEmpty()) result.insert(QStringLiteral("reason"), reason);
    emit booksTtsEdgeSynthFinished(requestId, result);
}
void BookBridge::onWorkerStreamError(quint64 streamId, const QString& reason)
{
    emit booksTtsEdgeSynthStreamFinished(streamId,
        QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("reason"), reason}});
}
void BookBridge::onWorkerStreamEnded(quint64 streamId)
{
    Q_UNUSED(streamId);
}
void BookBridge::onWorkerWarmupFinished(bool ok, const QString& reason)
{
    QJsonObject result{{QStringLiteral("ok"), ok}};
    if (!reason.isEmpty()) result.insert(QStringLiteral("reason"), reason);
    const quint64 reqId = m_pendingWarmupReqId; m_pendingWarmupReqId = 0;
    emit booksTtsEdgeWarmupFinished(reqId, result);
}
void BookBridge::onWorkerResetFinished()
{
    const quint64 reqId = m_pendingResetReqId; m_pendingResetReqId = 0;
    emit booksTtsEdgeResetFinished(reqId, QJsonObject{{QStringLiteral("ok"), true}});
}
