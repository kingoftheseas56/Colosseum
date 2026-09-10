#include "BookStores.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

// Same directory the OLD reader's BookBridge always used. This remains the
// explicit LegacyLocal route; account/local-only routes use ScopedStore roots.
QString legacyStateDir()
{
    return QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/book_reader"));
}

QJsonObject readStoreAt(const QString &root, const QString &fileName)
{
    if (root.isEmpty() || fileName.isEmpty()) return {};
    QFile f(QDir(root).filePath(fileName));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeStoreAt(const QString &root, const QString &fileName, const QJsonObject &all)
{
    if (root.isEmpty() || fileName.isEmpty()) return;
    if (!QDir().mkpath(root)) return;
    const QByteArray payload = QJsonDocument(all).toJson(QJsonDocument::Compact);
    QSaveFile f(QDir(root).filePath(fileName));
    if (!f.open(QIODevice::WriteOnly))
        return;
    if (f.write(payload) != payload.size()) {
        f.cancelWriting();
        return;
    }
    f.commit();
}

} // namespace

namespace BookStores {

ScopedStore::ScopedStore()
    : m_root(legacyStateDir())
{
}

void ScopedStore::useLegacyRoot()
{
    m_root = legacyStateDir();
}

void ScopedStore::useProfileRoot(const QString &profileRoot)
{
    const QString root = profileRoot.trimmed();
    if (root.isEmpty()) {
        seal();
        return;
    }
    m_root = QDir::cleanPath(QDir(root).filePath(QStringLiteral("book_reader")));
}

void ScopedStore::seal()
{
    m_root.clear();
}

bool ScopedStore::isSealed() const
{
    return m_root.isEmpty();
}

QString ScopedStore::root() const
{
    return m_root;
}

QJsonObject ScopedStore::readStore(const QString &fileName) const
{
    return readStoreAt(m_root, fileName);
}

void ScopedStore::writeStore(const QString &fileName, const QJsonObject &all) const
{
    writeStoreAt(m_root, fileName, all);
}

QJsonObject ScopedStore::get(const QString &fileName, const QString &bookId) const
{
    return readStore(fileName).value(bookId).toObject();
}

void ScopedStore::save(const QString &fileName, const QString &bookId,
                       const QJsonObject &data) const
{
    if (isSealed()) return;
    QJsonObject all = readStore(fileName);
    all[bookId] = data;
    writeStore(fileName, all);
}

QJsonArray ScopedStore::listGet(const QString &fileName, const QString &bookId) const
{
    return readStore(fileName).value(bookId).toArray();
}

QJsonObject ScopedStore::listSave(const QString &fileName, const QString &bookId,
                                  QJsonObject item) const
{
    if (isSealed()) return {};
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
            arr[i] = item;
            replaced = true;
            break;
        }
    }
    if (!replaced) arr.append(item);
    all[bookId] = arr;
    writeStore(fileName, all);
    return item;
}

QJsonObject ScopedStore::listDelete(const QString &fileName, const QString &bookId,
                                    const QString &itemId) const
{
    if (isSealed()) return {};
    QJsonObject all = readStore(fileName);
    if (itemId.isEmpty()) {
        all.remove(bookId);
    } else {
        QJsonArray arr = all.value(bookId).toArray();
        QJsonArray kept;
        for (const QJsonValue &v : arr)
            if (v.toObject().value(QStringLiteral("id")).toString() != itemId) kept.append(v);
        all[bookId] = kept;
    }
    writeStore(fileName, all);
    return QJsonObject{{QStringLiteral("ok"), true}};
}

void ScopedStore::listClear(const QString &fileName, const QString &bookId) const
{
    if (isSealed()) return;
    QJsonObject all = readStore(fileName);
    all.remove(bookId);
    writeStore(fileName, all);
}

// Canonical store key: normalize separators, SHA1 the UTF-8 bytes, take the first
// 20 hex chars. The old reader keyed progress/bookmarks/annotations by exactly this
// (it set state.book.id before every save/read); the fresh reader must derive the
// SAME fingerprint to read those records — so both call HERE, never their own copy.
QString keyFor(const QString& absPath)
{
    // On Linux, backslash is a valid filename character. Start from the caller's
    // spelling and rewrite it only when the input itself has Windows absolute-path
    // syntax, so cross-platform keys stay stable even when a Windows test probes a
    // Linux-shaped literal path.
    QString norm = absPath;
    const bool windowsDrivePath = norm.size() >= 3
        && norm.at(0).isLetter()
        && norm.at(1) == QLatin1Char(':')
        && (norm.at(2) == QLatin1Char('\\') || norm.at(2) == QLatin1Char('/'));
    const bool windowsUncPath = norm.startsWith(QStringLiteral("\\\\"));
    if (windowsDrivePath || windowsUncPath)
        norm.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QByteArray hex =
        QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(hex.left(20));
}

QJsonObject readStore(const QString& fileName)
{
    return ScopedStore().readStore(fileName);
}

void writeStore(const QString& fileName, const QJsonObject& all)
{
    ScopedStore().writeStore(fileName, all);
}

// ── keyed single-object pattern (e.g. progress.json) ──

QJsonObject get(const QString& fileName, const QString& bookId)
{
    return ScopedStore().get(fileName, bookId);
}

void save(const QString& fileName, const QString& bookId, const QJsonObject& data)
{
    ScopedStore().save(fileName, bookId, data);
}

// ── shared {bookId: [items]} list logic for bookmarks + annotations ──

QJsonArray listGet(const QString& fileName, const QString& bookId)
{
    return ScopedStore().listGet(fileName, bookId);
}

QJsonObject listSave(const QString& fileName, const QString& bookId, QJsonObject item)
{
    return ScopedStore().listSave(fileName, bookId, item);
}

QJsonObject listDelete(const QString& fileName, const QString& bookId, const QString& itemId)
{
    return ScopedStore().listDelete(fileName, bookId, itemId);
}

void listClear(const QString& fileName, const QString& bookId)
{
    ScopedStore().listClear(fileName, bookId);
}

} // namespace BookStores
