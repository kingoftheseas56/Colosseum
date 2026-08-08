#include "VaultRecent.h"

#include "VaultStoreIo.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QVariantMap>

#include <utility>

namespace {
const QString kFile = QStringLiteral("open-recent.json");
}

VaultRecent::VaultRecent(QString vaultDir) : m_dir(std::move(vaultDir)) { load(); }

QString VaultRecent::norm(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path).trimmed();
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
#ifdef Q_OS_WIN
    p = p.toLower();
#endif
    return p;
}

void VaultRecent::load()
{
    if (m_dir.isEmpty())
        return;
    const QJsonObject o = VaultStoreIo::load(m_dir, kFile);
    m_items = o.value(QStringLiteral("items")).toArray();
}

void VaultRecent::persist()
{
    if (m_dir.isEmpty())
        return;
    QJsonObject o;
    o[QStringLiteral("items")] = m_items;
    VaultStoreIo::save(m_dir, kFile, o);
}

void VaultRecent::record(const QString& path, const QString& title,
                         const QString& kind, const QString& vaultId)
{
    if (path.isEmpty())
        return;
    const QString key = norm(path);
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (norm(m_items.at(i).toObject().value(QStringLiteral("path")).toString()) == key)
            m_items.removeAt(i); // dedup — the fresh record moves it to the front
    }
    QJsonObject e;
    e[QStringLiteral("path")] = path;
    e[QStringLiteral("title")] = title;
    e[QStringLiteral("kind")] = kind;
    e[QStringLiteral("vaultId")] = vaultId;
    m_items.prepend(e);
    while (m_items.size() > kMax)
        m_items.removeLast();
    persist();
}

QVariantList VaultRecent::items() const
{
    QVariantList out;
    out.reserve(m_items.size());
    for (const auto& v : m_items) {
        const QJsonObject e = v.toObject();
        const QString path = e.value(QStringLiteral("path")).toString();
        out.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("title"), e.value(QStringLiteral("title")).toString()},
            {QStringLiteral("kind"), e.value(QStringLiteral("kind")).toString()},
            {QStringLiteral("vaultId"), e.value(QStringLiteral("vaultId")).toString()},
            {QStringLiteral("available"), QFileInfo::exists(path)},
        });
    }
    return out;
}

void VaultRecent::clear()
{
    m_items = QJsonArray();
    persist();
}
