#include "BookStores.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace {

// Same directory the OLD reader's BookBridge always used — moved verbatim so
// every store file lands in the exact same place under test mode's sandbox too.
QString stateDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/book_reader");
    QDir().mkpath(d);
    return d;
}

} // namespace

namespace BookStores {

QJsonObject readStore(const QString& fileName)
{
    QFile f(stateDir() + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeStore(const QString& fileName, const QJsonObject& all)
{
    QFile f(stateDir() + QLatin1Char('/') + fileName);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

// ── keyed single-object pattern (e.g. progress.json) ──

QJsonObject get(const QString& fileName, const QString& bookId)
{
    return readStore(fileName).value(bookId).toObject();
}

void save(const QString& fileName, const QString& bookId, const QJsonObject& data)
{
    QJsonObject all = readStore(fileName);
    all[bookId] = data;
    writeStore(fileName, all);
}

// ── shared {bookId: [items]} list logic for bookmarks + annotations ──

QJsonArray listGet(const QString& fileName, const QString& bookId)
{
    return readStore(fileName).value(bookId).toArray();
}

QJsonObject listSave(const QString& fileName, const QString& bookId, QJsonObject item)
{
    if (!item.contains(QStringLiteral("id")) || item.value(QStringLiteral("id")).toString().isEmpty())
        item[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!item.contains(QStringLiteral("createdAt")))
        item[QStringLiteral("createdAt")] = now;
    item[QStringLiteral("updatedAt")] = now;

    const QString id = item.value(QStringLiteral("id")).toString();
    QJsonObject all = readStore(fileName);
    QJsonArray arr = all.value(bookId).toArray();
    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            arr[i] = item; replaced = true; break;
        }
    }
    if (!replaced) arr.append(item);
    all[bookId] = arr;
    writeStore(fileName, all);
    return item;
}

QJsonObject listDelete(const QString& fileName, const QString& bookId, const QString& itemId)
{
    QJsonObject all = readStore(fileName);
    if (itemId.isEmpty()) {
        all.remove(bookId);                       // empty id => clear all for this book
    } else {
        QJsonArray arr = all.value(bookId).toArray();
        QJsonArray kept;
        for (const QJsonValue& v : arr)
            if (v.toObject().value(QStringLiteral("id")).toString() != itemId) kept.append(v);
        all[bookId] = kept;
    }
    writeStore(fileName, all);
    return QJsonObject{{QStringLiteral("ok"), true}};
}

void listClear(const QString& fileName, const QString& bookId)
{
    QJsonObject all = readStore(fileName);
    all.remove(bookId);
    writeStore(fileName, all);
}

} // namespace BookStores
