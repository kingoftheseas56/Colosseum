#include "BookBridge.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

BookBridge::BookBridge(QObject* parent) : QObject(parent) {}

// ─────────────────────────────────────────────────────────────────────────────
// files
// ─────────────────────────────────────────────────────────────────────────────

QByteArray BookBridge::filesRead(const QString& filePath)
{
    QString p = filePath;
    if (p.startsWith(QStringLiteral("file:///"))) p = QUrl(p).toLocalFile();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
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

// ─────────────────────────────────────────────────────────────────────────────
// Edge TTS — stubbed (answer each *Start with {ok:false} so JS Promises resolve)
// ─────────────────────────────────────────────────────────────────────────────

void BookBridge::booksTtsEdgeProbeStart(quint64 reqId, const QString&)
{
    emit booksTtsEdgeProbeFinished(reqId, QJsonObject{{QStringLiteral("ok"), false},
        {QStringLiteral("reason"), QStringLiteral("tts_unavailable")}});
}
void BookBridge::booksTtsEdgeGetVoicesStart(quint64 reqId)
{
    emit booksTtsEdgeVoicesReady(reqId, QJsonObject{{QStringLiteral("ok"), false},
        {QStringLiteral("voices"), QJsonArray{}}});
}
void BookBridge::booksTtsEdgeSynthStart(quint64 reqId, const QString&, const QString&, double, double)
{
    emit booksTtsEdgeSynthFinished(reqId, QJsonObject{{QStringLiteral("ok"), false},
        {QStringLiteral("reason"), QStringLiteral("tts_unavailable")}});
}
void BookBridge::booksTtsEdgeSynthStreamStart(quint64 reqId, const QString&, const QString&, double, double)
{
    emit booksTtsEdgeSynthStreamFinished(reqId, QJsonObject{{QStringLiteral("ok"), false}});
}
void BookBridge::booksTtsEdgeCancelStream(quint64 streamId)
{
    emit booksTtsEdgeSynthStreamFinished(streamId, QJsonObject{{QStringLiteral("ok"), false}});
}
void BookBridge::booksTtsEdgeWarmupStart(quint64 reqId)
{
    emit booksTtsEdgeWarmupFinished(reqId, QJsonObject{{QStringLiteral("ok"), false}});
}
void BookBridge::booksTtsEdgeResetStart(quint64 reqId)
{
    emit booksTtsEdgeResetFinished(reqId, QJsonObject{{QStringLiteral("ok"), true}});
}
