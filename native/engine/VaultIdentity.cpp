#include "VaultIdentity.h"

#include "VaultStoreIo.h"
#include "VaultLocation.h"

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
    return VaultLocation::normalize(path);
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
    for (const auto& v : doc.value(QStringLiteral("ceremonyDecisions")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString relationship = o.value(QStringLiteral("relationship")).toString();
        const QString choice = o.value(QStringLiteral("choice")).toString();
        if (!relationship.isEmpty() && !choice.isEmpty())
            m_decisions.insert(relationship, choice);
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

    QJsonArray ceremonyDecisions;
    for (auto it = m_decisions.constBegin(); it != m_decisions.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("relationship"), it.key());
        o.insert(QStringLiteral("choice"), it.value());
        ceremonyDecisions.append(o);
    }
    doc.insert(QStringLiteral("ceremonyDecisions"), ceremonyDecisions);

    VaultStoreIo::save(m_dir, QStringLiteral("identity.json"), doc);
    emit changed();
}

bool VaultIdentity::withinMtimeTolerance(qint64 a, qint64 b)
{
    constexpr qint64 kToleranceMs = 2000;
    return qAbs(a - b) <= kToleranceMs;
}

QString VaultIdentity::relationshipKey(const QString& type, const QString& oldId,
                                       const QString& path)
{
    return type + QLatin1Char('|') + oldId + QLatin1Char('|') + normalizePath(path);
}

QVariantMap VaultIdentity::ceremonyMap(const QStringList& fields) const
{
    QVariantMap out;
    if (fields.size() < 6)
        return out;
    out.insert(QStringLiteral("prompt"), true);
    out.insert(QStringLiteral("type"), fields.at(0));
    out.insert(QStringLiteral("relationship"), fields.at(1));
    out.insert(QStringLiteral("oldId"), fields.at(2));
    out.insert(QStringLiteral("newId"), fields.at(3));
    out.insert(QStringLiteral("oldPath"), fields.at(4));
    out.insert(QStringLiteral("newPath"), fields.at(5));
    return out;
}

void VaultIdentity::rememberPending(const QStringList& fields)
{
    if (fields.size() >= 6)
        m_pending.insert(fields.at(1), fields);
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
    return observeFile(path, size, mtimeMs).value(QStringLiteral("id")).toString();
}

QVariantMap VaultIdentity::observeFile(const QString& path, qint64 size, qint64 mtimeMs)
{
    const QString cid = computeId(path, size, mtimeMs);
    QVariantMap out;
    out.insert(QStringLiteral("id"), cid);
    if (m_alias.contains(cid)) {
        out[QStringLiteral("id")] = m_alias.value(cid);
        return out;
    }
    if (m_byId.contains(cid)) {
        m_byId[cid].path = path;
        return out;
    }

    const QString normalized = normalizePath(path);
    Entry pathEntry;
    bool hasPathEntry = false;
    for (const Entry& e : m_byId) {
        if (normalizePath(e.path) == normalized && e.id != cid) {
            pathEntry = e;
            hasPathEntry = true;
            break;
        }
    }
    auto registerFresh = [&]() {
        m_byId.insert(cid, Entry{cid, path, size, mtimeMs});
    };
    if (hasPathEntry) {
        if (pathEntry.size == size && withinMtimeTolerance(pathEntry.mtimeMs, mtimeMs)) {
            m_alias.insert(cid, pathEntry.id);
            m_byId[pathEntry.id].path = path;
            m_byId[pathEntry.id].size = size;
            m_byId[pathEntry.id].mtimeMs = mtimeMs;
            persist();
            out[QStringLiteral("id")] = pathEntry.id;
            return out;
        }
        const QString relationship = relationshipKey(QStringLiteral("changed-content"),
                                                      pathEntry.id, path);
        const QString choice = m_decisions.value(relationship);
        if (choice == QLatin1String("same-media")) {
            m_alias.insert(cid, pathEntry.id);
            m_byId[pathEntry.id].size = size;
            m_byId[pathEntry.id].mtimeMs = mtimeMs;
            persist();
            out[QStringLiteral("id")] = pathEntry.id;
            return out;
        }
        registerFresh();
        const QStringList fields{QStringLiteral("changed-content"), relationship,
                                 pathEntry.id, cid, pathEntry.path, path};
        if (choice.isEmpty()) {
            rememberPending(fields);
            const QVariantMap prompt = ceremonyMap(fields);
            for (auto it = prompt.constBegin(); it != prompt.constEnd(); ++it)
                out.insert(it.key(), it.value());
        }
        persist();
        return out;
    }

    Entry copyEntry;
    int copyMatches = 0;
    for (const Entry& e : m_byId) {
        if (e.id == cid || normalizePath(e.path) == normalized)
            continue;
        if (e.size == size && e.mtimeMs == mtimeMs && QFileInfo(e.path).exists()) {
            copyEntry = e;
            ++copyMatches;
        }
    }
    if (copyMatches == 1) {
        const QString relationship = relationshipKey(QStringLiteral("likely-copy"),
                                                      copyEntry.id, path);
        const QString choice = m_decisions.value(relationship);
        if (choice == QLatin1String("use-existing-state")) {
            m_alias.insert(cid, copyEntry.id);
            persist();
            out[QStringLiteral("id")] = copyEntry.id;
            return out;
        }
        registerFresh();
        const QStringList fields{QStringLiteral("likely-copy"), relationship,
                                 copyEntry.id, cid, copyEntry.path, path};
        if (choice.isEmpty()) {
            rememberPending(fields);
            const QVariantMap prompt = ceremonyMap(fields);
            for (auto it = prompt.constBegin(); it != prompt.constEnd(); ++it)
                out.insert(it.key(), it.value());
        }
        persist();
        return out;
    }

    registerFresh();
    persist();
    return out;
}

QVariantList VaultIdentity::pendingCeremonies() const
{
    QVariantList out;
    for (const QStringList& fields : m_pending)
        out.append(ceremonyMap(fields));
    return out;
}

bool VaultIdentity::decideCeremony(const QString& relationship, const QString& choice)
{
    const auto it = m_pending.constFind(relationship);
    if (it == m_pending.constEnd() || it->size() < 6)
        return false;
    const QString type = it->at(0);
    const bool valid = (type == QLatin1String("changed-content")
                            && (choice == QLatin1String("same-media")
                                || choice == QLatin1String("new-media")))
                       || (type == QLatin1String("likely-copy")
                            && (choice == QLatin1String("use-existing-state")
                                || choice == QLatin1String("separate-copy")));
    if (!valid)
        return false;

    m_decisions.insert(relationship, choice);
    if (choice == QLatin1String("same-media")
        || choice == QLatin1String("use-existing-state")) {
        const QString oldId = it->at(2);
        const QString newId = it->at(3);
        qint64 candidateSize = 0;
        qint64 candidateMtime = 0;
        if (m_byId.contains(newId)) {
            candidateSize = m_byId.value(newId).size;
            candidateMtime = m_byId.value(newId).mtimeMs;
        }
        m_alias.insert(newId, oldId);
        if (m_byId.contains(newId) && newId != oldId)
            m_byId.remove(newId);
        if (m_byId.contains(oldId)) {
            m_byId[oldId].path = it->at(5);
            if (candidateSize > 0 || candidateMtime > 0) {
                m_byId[oldId].size = candidateSize;
                m_byId[oldId].mtimeMs = candidateMtime;
            }
        }
    }
    m_pending.remove(relationship);
    persist();
    return true;
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
    auto signature = [](qint64 size, qint64 mtimeMs) {
        return QString::number(size) + QLatin1Char(':') + QString::number(mtimeMs);
    };
    QHash<QString, QList<int>> freshBySignature;
    for (int i = 0; i < fresh.size(); ++i)
        freshBySignature[signature(fresh.at(i).f.size, fresh.at(i).f.mtimeMs)].append(i);

    QHash<QString, QList<int>> missingBySignature;
    for (int i = 0; i < missing.size(); ++i)
        missingBySignature[signature(missing.at(i).size, missing.at(i).mtimeMs)].append(i);

    QSet<int> migratedFresh;
    // A known path whose facts changed materially is not a rename. Likewise, a fresh path
    // with an identical signature while its old canonical remains present is only a likely copy.
    // Both relationships are explicit ceremonies; an unambiguous rename below remains silent.
    for (int i = 0; i < fresh.size(); ++i) {
        const Item& candidate = fresh.at(i);
        Entry pathEntry;
        bool hasPathEntry = false;
        for (const Entry& e : m_byId) {
            if (e.id != candidate.cid
                && normalizePath(e.path) == normalizePath(candidate.f.path)) {
                pathEntry = e;
                hasPathEntry = true;
                break;
            }
        }
        if (hasPathEntry) {
            const QString relationship = relationshipKey(QStringLiteral("changed-content"),
                                                          pathEntry.id, candidate.f.path);
            const QString choice = m_decisions.value(relationship);
            if (pathEntry.size == candidate.f.size
                && withinMtimeTolerance(pathEntry.mtimeMs, candidate.f.mtimeMs)) {
                m_alias.insert(candidate.cid, pathEntry.id);
                m_byId[pathEntry.id].path = candidate.f.path;
                m_byId[pathEntry.id].size = candidate.f.size;
                m_byId[pathEntry.id].mtimeMs = candidate.f.mtimeMs;
                migratedFresh.insert(i);
            } else if (choice == QLatin1String("same-media")) {
                m_alias.insert(candidate.cid, pathEntry.id);
                m_byId[pathEntry.id].path = candidate.f.path;
                m_byId[pathEntry.id].size = candidate.f.size;
                m_byId[pathEntry.id].mtimeMs = candidate.f.mtimeMs;
                migratedFresh.insert(i);
            } else if (choice.isEmpty()) {
                const QStringList fields{QStringLiteral("changed-content"), relationship,
                                         pathEntry.id, candidate.cid, pathEntry.path,
                                         candidate.f.path};
                result.ceremonies.append(fields);
                rememberPending(fields);
            }
            continue;
        }

        Entry copyEntry;
        int copyMatches = 0;
        for (const Entry& e : m_byId) {
            if (e.id == candidate.cid || !canonicalsPresent.contains(e.id)
                || normalizePath(e.path) == normalizePath(candidate.f.path))
                continue;
            if (e.size == candidate.f.size && e.mtimeMs == candidate.f.mtimeMs) {
                copyEntry = e;
                ++copyMatches;
            }
        }
        if (copyMatches != 1)
            continue;
        const QString relationship = relationshipKey(QStringLiteral("likely-copy"),
                                                      copyEntry.id, candidate.f.path);
        const QString choice = m_decisions.value(relationship);
        if (choice == QLatin1String("use-existing-state")) {
            m_alias.insert(candidate.cid, copyEntry.id);
            migratedFresh.insert(i);
        } else if (choice.isEmpty()) {
            const QStringList fields{QStringLiteral("likely-copy"), relationship,
                                     copyEntry.id, candidate.cid, copyEntry.path,
                                     candidate.f.path};
            result.ceremonies.append(fields);
            rememberPending(fields);
        }
    }

    for (auto it = missingBySignature.constBegin(); it != missingBySignature.constEnd(); ++it) {
        const QList<int>& oldIndexes = it.value();
        const QList<int> newIndexes = freshBySignature.value(it.key());
        if (oldIndexes.size() == 1 && newIndexes.size() == 1) {
            const Entry& oldEntry = missing.at(oldIndexes.first());
            const Item& candidate = fresh.at(newIndexes.first());
            m_alias.insert(candidate.cid, oldEntry.id);
            m_pathAliases.append({oldEntry.path, candidate.f.path});
            m_byId[oldEntry.id].path = candidate.f.path; // move the canonical entry's location
            result.migrated.append({oldEntry.id, candidate.cid, oldEntry.path, candidate.f.path});
            migratedFresh.insert(newIndexes.first());
        } else {
            for (const int oldIndex : oldIndexes)
                result.parked.append(missing.at(oldIndex).id);
        }
    }

    // Register whatever remains fresh.
    for (int i = 0; i < fresh.size(); ++i) {
        if (migratedFresh.contains(i))
            continue;
        const Item& it = fresh.at(i);
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
