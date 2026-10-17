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
    m_recoverUnresolvedCandidates = doc.value("legacyAdmissionReview").toBool()
        || (!doc.isEmpty() && !doc.value("pendingCeremonies").isArray());

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
        const bool valid = (relationship.startsWith("changed-content|")
                            && (choice == "same-media" || choice == "new-media"))
            || (relationship.startsWith("likely-copy|")
                            && (choice == "use-existing-state" || choice == "separate-copy"));
        if (valid) m_decisions.insert(relationship, choice);
    }
    // Registration is not consent: unresolved candidates must survive restart.
    for (const auto& value : doc.value(QStringLiteral("pendingCeremonies")).toArray()) {
        const auto o = value.toObject();
        const QString type = o.value("type").toString();
        const QString oldId = o.value("oldId").toString();
        const QString newId = o.value("newId").toString();
        const QString oldPath = o.value("oldPath").toString();
        const QString newPath = o.value("newPath").toString();
        const QString relationship = o.value("relationship").toString();
        if ((type != QLatin1String("changed-content") && type != QLatin1String("likely-copy"))
            || oldId == newId || !m_byId.contains(oldId) || !m_byId.contains(newId)
            || oldPath.isEmpty() || newPath.isEmpty()
            || relationship != relationshipKey(type, oldId, newPath)
            || normalizePath(m_byId.value(newId).path) != normalizePath(newPath)
            || normalizePath(m_byId.value(oldId).path) != normalizePath(oldPath)) {
            m_recoverUnresolvedCandidates = true;
            continue; // Cached admission below reconstructs malformed/legacy candidates.
        }
        if (!m_decisions.contains(relationship))
            rememberPending({type, relationship, oldId, newId, oldPath, newPath});
    }
}

void VaultIdentity::persist()
{
    if (m_deferPersist) { m_persistPending = true; return; }
    QJsonObject doc;
    doc.insert(QStringLiteral("version"), 1);
    // Preserve legacy review across unrelated writes until every candidate has
    // an explicit relationship decision; no admission is inferred from load.
    if (m_recoverUnresolvedCandidates) doc.insert(QStringLiteral("legacyAdmissionReview"), true);

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

    QJsonArray pendingCeremonies;
    for (const auto& fields : m_pending)
        pendingCeremonies.append(QJsonObject::fromVariantMap(ceremonyMap(fields)));
    doc.insert(QStringLiteral("pendingCeremonies"), pendingCeremonies);

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

QList<QVariantMap> VaultIdentity::observeFiles(const QList<FileFacts>& files)
{
    const bool alreadyDeferred = m_deferPersist;
    m_deferPersist = true;
    QList<QVariantMap> results;
    results.reserve(files.size());
    for (const auto& file : files)
        results.append(observeFile(file.path, file.size, file.mtimeMs));
    m_deferPersist = alreadyDeferred;
    if (!alreadyDeferred && m_persistPending) {
        m_persistPending = false;
        persist();
    }
    return results;
}

QVariantMap VaultIdentity::observeFile(const QString& path, qint64 size, qint64 mtimeMs)
{
    const QString cid = computeId(path, size, mtimeMs);
    QVariantMap out;
    out.insert(QStringLiteral("id"), cid);
    for (const auto& fields : m_pending) {
        if (fields.size() >= 6 && (fields.at(3) == resolve(cid)
                || normalizePath(fields.at(5)) == normalizePath(path))) {
            const auto prompt = ceremonyMap(fields);
            for (auto it = prompt.constBegin(); it != prompt.constEnd(); ++it) out.insert(it.key(), it.value());
            return out;
        }
    }
    if (m_alias.contains(cid)) {
        const QString canonical = m_alias.value(cid);
        if (m_recoverUnresolvedCandidates && m_byId.contains(canonical)) {
            const auto candidate = m_byId.value(canonical);
            // A legacy rename alias can still point at an unapproved candidate.
            // Only block when conflicting registry facts demonstrate ambiguity;
            // a plain unique rename remains admitted.
            QSet<QString> visitedPaths{normalizePath(path)};
            QStringList pendingPaths{normalizePath(path)};
            QList<QStringList> provenance;
            while (!pendingPaths.isEmpty()) {
                const QString destination = pendingPaths.takeLast();
                for (const auto& pa : m_pathAliases) {
                    if (pa.size() != 2 || normalizePath(pa.at(1)) != destination) continue;
                    provenance.append(pa);
                    const QString predecessor = normalizePath(pa.at(0));
                    if (!visitedPaths.contains(predecessor)) {
                        visitedPaths.insert(predecessor);
                        pendingPaths.append(predecessor);
                    }
                }
            }
            for (const auto& pa : provenance) {
                for (const auto& other : m_byId) {
                    if (other.id==canonical) continue;
                    const bool changed = (normalizePath(other.path)==normalizePath(pa.at(0))
                        || normalizePath(other.path)==normalizePath(path))
                        && (other.size!=candidate.size || !withinMtimeTolerance(other.mtimeMs,candidate.mtimeMs));
                    const bool copy = normalizePath(other.path)!=normalizePath(path)
                        && other.size==candidate.size && other.mtimeMs==candidate.mtimeMs;
                    if (!changed && !copy) continue;
                    const QString type=changed ? QStringLiteral("changed-content") : QStringLiteral("likely-copy");
                    const QString before=m_decisions.value(relationshipKey(type,other.id,pa.at(0)));
                    const QString after=m_decisions.value(relationshipKey(type,other.id,path));
                    const QString reverse=m_decisions.value(relationshipKey(type,canonical,other.path));
                    const auto approved=[changed](const QString &choice) {
                        return changed ? choice=="same-media" || choice=="new-media"
                            : choice=="use-existing-state" || choice=="separate-copy";
                    };
                    bool hasApproval = approved(before) || approved(after) || approved(reverse);
                    for (const auto& prior : provenance)
                        hasApproval = hasApproval || approved(m_decisions.value(relationshipKey(type, other.id, prior.at(0))));
                    if (hasApproval) continue;
                    out[QStringLiteral("id")]=QString();
                    out[QStringLiteral("error")]=QStringLiteral("This older library has ambiguous moved-file identities. Import is blocked to protect existing progress. Restore a known-good library backup before retrying.");
                    return out;
                }
            }
        }
        out[QStringLiteral("id")] = canonical;
        return out;
    }
    if (m_byId.contains(cid)) {
        // Older registries saved the candidate entry but dropped its pending
        // ceremony. A known computed ID alone cannot approve such a candidate.
        // Also reconstruct conservatively when a pending row was malformed.
        QStringList ids = m_recoverUnresolvedCandidates ? m_byId.keys() : QStringList{};
        ids.sort();
        for (const auto& otherId : ids) {
            if (otherId == cid) continue;
            const auto other = m_byId.value(otherId);
            bool unapprovedCandidate = false;
            for (const auto& pending : m_pending)
                if (pending.size() >= 6 && pending.at(3) == otherId) { unapprovedCandidate = true; break; }
            if (unapprovedCandidate) continue;
            const bool changed = normalizePath(other.path) == normalizePath(path)
                && (other.size != size || !withinMtimeTolerance(other.mtimeMs, mtimeMs));
            const bool copy = normalizePath(other.path) != normalizePath(path)
                && other.size == size && other.mtimeMs == mtimeMs && QFileInfo(other.path).exists();
            if (!changed && !copy) continue;
            const QString type = changed ? QStringLiteral("changed-content") : QStringLiteral("likely-copy");
            const QString relationship = relationshipKey(type, otherId, path);
            const QString forward = m_decisions.value(relationship);
            const QString reverse = m_decisions.value(relationshipKey(type, cid, other.path));
            const auto approved = [changed](const QString& choice) {
                return changed ? choice == QLatin1String("new-media") || choice == QLatin1String("same-media")
                    : choice == QLatin1String("separate-copy") || choice == QLatin1String("use-existing-state");
            };
            if (approved(forward) || approved(reverse)) continue;
            if (copy) {
                // Old storage has no provenance telling which equal-signature
                // copy was accepted first. Never invent a merge direction.
                out[QStringLiteral("id")] = QString();
                out[QStringLiteral("error")] = QStringLiteral("This older library has ambiguous copy identities. Import is blocked to protect existing progress. Restore a known-good library backup before retrying.");
                return out;
            }
            const QStringList fields{type, relationship, otherId, cid, other.path, path};
            rememberPending(fields); persist();
            const auto prompt = ceremonyMap(fields);
            for (auto it = prompt.constBegin(); it != prompt.constEnd(); ++it) out.insert(it.key(), it.value());
            return out;
        }
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

bool VaultIdentity::decideFileCeremony(const QString& relationship, const QString& choice)
{
    const auto fields = m_pending.value(relationship);
    if (fields.size() < 6) return false;
    const QFileInfo current(fields.at(5));
    if (!current.exists() || !current.isFile()) return false;
    const auto currentId = computeId(fields.at(5), current.size(), current.lastModified().toMSecsSinceEpoch());
    const auto candidate = m_byId.value(fields.at(3));
    if (resolve(currentId) != fields.at(3) || candidate.size != current.size()
        || candidate.mtimeMs != current.lastModified().toMSecsSinceEpoch()) {
        // The user saw a different candidate. Discard only its unapproved
        // registration, then let the same owner observe the current facts.
        m_pending.remove(relationship);
        if (!m_alias.contains(fields.at(3))) m_byId.remove(fields.at(3));
        for (auto alias = m_alias.begin(); alias != m_alias.end(); ) {
            if (alias.value() == fields.at(3)) alias = m_alias.erase(alias);
            else ++alias;
        }
        observeFile(fields.at(5), current.size(), current.lastModified().toMSecsSinceEpoch());
        persist();
        return false;
    }
    return decideCeremony(relationship, choice);
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
        for (auto alias = m_alias.begin(); alias != m_alias.end(); ++alias)
            if (alias.value() == newId) alias.value() = oldId;
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
            // Moving a registered candidate does not approve its pending identity.
            const auto pendingBeforeMove = m_pending;
            for (auto pending = pendingBeforeMove.constBegin(); pending != pendingBeforeMove.constEnd(); ++pending) {
                auto fields = pending.value();
                if (fields.size() < 6) continue;
                if (fields.at(3) == oldEntry.id) {
                    m_pending.remove(pending.key());
                    fields[5] = candidate.f.path;
                    fields[1] = relationshipKey(fields.at(0), fields.at(2), fields.at(5));
                    rememberPending(fields);
                } else if (fields.at(2) == oldEntry.id) {
                    fields[4] = candidate.f.path;
                    rememberPending(fields);
                }
            }
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
