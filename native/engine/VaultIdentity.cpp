#include "VaultIdentity.h"

#include "VaultStoreIo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariantMap>

VaultIdentity::VaultIdentity(QString vaultDir, QObject* parent)
    : QObject(parent), m_dir(std::move(vaultDir))
{
    load();
}

QString VaultIdentity::normalizePath(const QString& path)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

QString VaultIdentity::computeId(const QString& path, qint64 size, qint64 mtimeMs)
{
    const QString basis = normalizePath(path) + QStringLiteral("::")
        + QString::number(size) + QStringLiteral("::") + QString::number(mtimeMs);
    const QByteArray digest =
        QCryptographicHash::hash(basis.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("vault:") + QString::fromLatin1(digest);
}

void VaultIdentity::load()
{
    const QJsonObject doc = VaultStoreIo::load(m_dir, QStringLiteral("identity.json"));

    for (const auto& v : doc.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString();
        e.path = o.value(QStringLiteral("path")).toString();
        e.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
        e.mtimeMs = o.value(QStringLiteral("mtimeMs")).toVariant().toLongLong();
        if (!e.id.isEmpty())
            m_byId.insert(e.id, e);
    }
    for (const auto& v : doc.value(QStringLiteral("aliases")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString c = o.value(QStringLiteral("computed")).toString();
        const QString canon = o.value(QStringLiteral("canonical")).toString();
        if (!c.isEmpty() && !canon.isEmpty())
            m_alias.insert(c, canon);
    }
    for (const auto& v : doc.value(QStringLiteral("pathAliases")).toArray()) {
        const QJsonObject o = v.toObject();
        m_pathAliases.append({o.value(QStringLiteral("oldPath")).toString(),
                              o.value(QStringLiteral("newPath")).toString()});
    }
}

void VaultIdentity::persist()
{
    QJsonObject doc;
    doc.insert(QStringLiteral("version"), 1);

    QJsonArray entries;
    for (const Entry& e : m_byId) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), e.id);
        o.insert(QStringLiteral("path"), e.path);
        o.insert(QStringLiteral("size"), e.size);
        o.insert(QStringLiteral("mtimeMs"), e.mtimeMs);
        entries.append(o);
    }
    doc.insert(QStringLiteral("entries"), entries);

    QJsonArray aliases;
    for (auto it = m_alias.constBegin(); it != m_alias.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("computed"), it.key());
        o.insert(QStringLiteral("canonical"), it.value());
        aliases.append(o);
    }
    doc.insert(QStringLiteral("aliases"), aliases);

    QJsonArray pathAliases;
    for (const QStringList& pa : m_pathAliases) {
        if (pa.size() != 2)
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("oldPath"), pa.at(0));
        o.insert(QStringLiteral("newPath"), pa.at(1));
        pathAliases.append(o);
    }
    doc.insert(QStringLiteral("pathAliases"), pathAliases);

    VaultStoreIo::save(m_dir, QStringLiteral("identity.json"), doc);
    emit changed();
}

QString VaultIdentity::resolve(const QString& id) const
{
    return m_alias.value(id, id);
}

bool VaultIdentity::knows(const QString& id) const
{
    return m_byId.contains(id) || m_alias.contains(id);
}

QString VaultIdentity::idForFile(const QString& path, qint64 size, qint64 mtimeMs)
{
    const QString cid = computeId(path, size, mtimeMs);
    if (m_alias.contains(cid))
        return m_alias.value(cid);
    if (m_byId.contains(cid)) {
        // Same file re-seen; keep its recorded location current.
        m_byId[cid].path = path;
        return cid;
    }
    Entry e{cid, path, size, mtimeMs};
    m_byId.insert(cid, e);
    persist();
    return cid;
}

VaultIdentity::ReconcileResult VaultIdentity::reconcile(const QList<FileFacts>& current)
{
    ReconcileResult result;

    // Compute ids for the current scan; note which canonical entries are present
    // (either directly by id or via an existing alias).
    struct Item { QString cid; FileFacts f; };
    QList<Item> items;
    QSet<QString> canonicalsPresent;
    for (const FileFacts& f : current) {
        const QString cid = computeId(f.path, f.size, f.mtimeMs);
        items.append({cid, f});
        if (m_byId.contains(cid))
            canonicalsPresent.insert(cid);
        else if (m_alias.contains(cid))
            canonicalsPresent.insert(m_alias.value(cid));
    }

    // Fresh = current files whose id is neither a known canonical nor a known alias.
    QList<Item> fresh;
    for (const Item& it : items) {
        if (m_byId.contains(it.cid)) {
            m_byId[it.cid].path = it.f.path; // keep location current
        } else if (m_alias.contains(it.cid)) {
            // already migrated in a prior pass — nothing to do
        } else {
            fresh.append(it);
        }
    }

    // Missing = registered canonical entries not present in this scan.
    QList<Entry> missing;
    for (const Entry& e : m_byId) {
        if (!canonicalsPresent.contains(e.id))
            missing.append(e);
    }

    // Re-attach each missing entry to a UNIQUE fresh file of the same signature.
    for (const Entry& m : missing) {
        int matchIdx = -1;
        int matchCount = 0;
        for (int i = 0; i < fresh.size(); ++i) {
            if (fresh.at(i).f.size == m.size && fresh.at(i).f.mtimeMs == m.mtimeMs) {
                ++matchCount;
                matchIdx = i;
            }
        }
        if (matchCount == 1) {
            const Item cand = fresh.takeAt(matchIdx);
            m_alias.insert(cand.cid, m.id);
            m_pathAliases.append({m.path, cand.f.path});
            m_byId[m.id].path = cand.f.path; // move the canonical entry's location
            result.migrated.append({m.id, cand.cid, m.path, cand.f.path});
        } else {
            result.parked.append(m.id); // vanished or ambiguous — never silently merged
        }
    }

    // Register whatever remains fresh.
    for (const Item& it : fresh) {
        Entry e{it.cid, it.f.path, it.f.size, it.f.mtimeMs};
        m_byId.insert(it.cid, e);
        result.fresh.append(it.cid);
    }

    persist();
    return result;
}

QVariantList VaultIdentity::pathAliases() const
{
    QVariantList out;
    for (const QStringList& pa : m_pathAliases) {
        if (pa.size() != 2)
            continue;
        QVariantMap m;
        m[QStringLiteral("oldPath")] = pa.at(0);
        m[QStringLiteral("newPath")] = pa.at(1);
        out.append(m);
    }
    return out;
}
