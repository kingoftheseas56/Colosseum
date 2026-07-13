#include "BookBridge.h"
#include "../tts/EdgeTtsWorker.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
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

QString BookBridge::progressKey(const QString& absPath) const
{
    const QString norm = QDir::fromNativeSeparators(absPath);
    const QByteArray hex =
        QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(hex.left(20));
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON store
// ─────────────────────────────────────────────────────────────────────────────

QString BookBridge::stateDir() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/book_reader");
    QDir().mkpath(d);
    return d;
}

QJsonObject BookBridge::readStore(const QString& file) const
{
    QFile f(stateDir() + QLatin1Char('/') + file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void BookBridge::writeStore(const QString& file, const QJsonObject& obj) const
{
    QFile f(stateDir() + QLatin1Char('/') + file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── progress ──

QJsonObject BookBridge::booksProgressGet(const QString& bookId)
{
    return readStore(QStringLiteral("progress.json")).value(bookId).toObject();
}

void BookBridge::booksProgressSave(const QString& bookId, const QJsonObject& data)
{
    QJsonObject all = readStore(QStringLiteral("progress.json"));
    all[bookId] = data;
    writeStore(QStringLiteral("progress.json"), all);
    emit progressSaved(bookId, data.value(QStringLiteral("fraction")).toDouble());
}

// ── settings (global flat bag) ──

QJsonObject BookBridge::booksSettingsGet()
{
    QJsonObject wrap;
    wrap[QStringLiteral("settings")] = readStore(QStringLiteral("settings.json"));
    return wrap;
}

void BookBridge::booksSettingsSave(const QJsonObject& data)
{
    writeStore(QStringLiteral("settings.json"), data);
}

// ── shared {bookId: [items]} list logic for bookmarks + annotations ──

QJsonArray BookBridge::listGet(const QString& file, const QString& bookId) const
{
    return readStore(file).value(bookId).toArray();
}

QJsonObject BookBridge::listSave(const QString& file, const QString& bookId, QJsonObject item)
{
    if (!item.contains(QStringLiteral("id")) || item.value(QStringLiteral("id")).toString().isEmpty())
        item[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!item.contains(QStringLiteral("createdAt")))
        item[QStringLiteral("createdAt")] = now;
    item[QStringLiteral("updatedAt")] = now;

    const QString id = item.value(QStringLiteral("id")).toString();
    QJsonObject all = readStore(file);
    QJsonArray arr = all.value(bookId).toArray();
    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            arr[i] = item; replaced = true; break;
        }
    }
    if (!replaced) arr.append(item);
    all[bookId] = arr;
    writeStore(file, all);
    return item;
}

QJsonObject BookBridge::listDelete(const QString& file, const QString& bookId, const QString& itemId)
{
    QJsonObject all = readStore(file);
    if (itemId.isEmpty()) {
        all.remove(bookId);                       // empty id ⇒ clear all for this book
    } else {
        QJsonArray arr = all.value(bookId).toArray();
        QJsonArray kept;
        for (const QJsonValue& v : arr)
            if (v.toObject().value(QStringLiteral("id")).toString() != itemId) kept.append(v);
        all[bookId] = kept;
    }
    writeStore(file, all);
    return QJsonObject{{QStringLiteral("ok"), true}};
}

void BookBridge::listClear(const QString& file, const QString& bookId)
{
    QJsonObject all = readStore(file);
    all.remove(bookId);
    writeStore(file, all);
}

QJsonArray  BookBridge::booksBookmarksGet(const QString& bookId)            { return listGet(QStringLiteral("bookmarks.json"), bookId); }
QJsonObject BookBridge::booksBookmarksSave(const QString& bookId, const QJsonObject& bm)   { return listSave(QStringLiteral("bookmarks.json"), bookId, bm); }
QJsonObject BookBridge::booksBookmarksDelete(const QString& bookId, const QString& bmId)   { return listDelete(QStringLiteral("bookmarks.json"), bookId, bmId); }
void        BookBridge::booksBookmarksClear(const QString& bookId)          { listClear(QStringLiteral("bookmarks.json"), bookId); }

QJsonArray  BookBridge::booksAnnotationsGet(const QString& bookId)          { return listGet(QStringLiteral("annotations.json"), bookId); }
QJsonObject BookBridge::booksAnnotationsSave(const QString& bookId, const QJsonObject& an) { return listSave(QStringLiteral("annotations.json"), bookId, an); }
QJsonObject BookBridge::booksAnnotationsDelete(const QString& bookId, const QString& anId) { return listDelete(QStringLiteral("annotations.json"), bookId, anId); }
void        BookBridge::booksAnnotationsClear(const QString& bookId)        { listClear(QStringLiteral("annotations.json"), bookId); }

// ── display names ──

QJsonObject BookBridge::booksDisplayNamesGetAll()
{
    return readStore(QStringLiteral("display_names.json"));
}

void BookBridge::booksDisplayNamesSave(const QString& bookId, const QString& name)
{
    QJsonObject all = readStore(QStringLiteral("display_names.json"));
    all[bookId] = name;
    writeStore(QStringLiteral("display_names.json"), all);
}

void BookBridge::booksDisplayNamesDelete(const QString& bookId)
{
    QJsonObject all = readStore(QStringLiteral("display_names.json"));
    all.remove(bookId);
    writeStore(QStringLiteral("display_names.json"), all);
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
void BookBridge::requestListen()   { emit listenRequested(); }

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
