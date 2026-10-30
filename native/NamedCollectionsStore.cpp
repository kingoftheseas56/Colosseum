#include "NamedCollectionsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <utility>

namespace {
constexpr int kSchema = 2;
constexpr qsizetype kMaxFileBytes = 8 * 1024 * 1024;
constexpr qsizetype kMaxReferenceBytes = 64 * 1024;
constexpr int kMaxCollections = 500;
constexpr int kMaxMembers = 10000;
constexpr int kMaxDrafts = 32;

bool validText(const QString &value, bool multiline)
{
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch.isNull() || (ch.category() == QChar::Other_Control
            && !(multiline && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')
                              || ch == QLatin1Char('\t'))))) return false;
        if (ch.isHighSurrogate()) {
            if (i + 1 >= value.size() || !value.at(i + 1).isLowSurrogate()) return false;
            ++i;
        } else if (ch.isLowSurrogate()) return false;
    }
    return true;
}

bool jsonSafe(const QVariant &value, int depth, int *nodes)
{
    if (depth > 8 || ++*nodes > 4096) return false;
    if (!value.isValid() || value.isNull()) return true;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return true;
    case QMetaType::QString:
        return validText(value.toString(), true);
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float: {
        const double number = value.toDouble();
        return std::isfinite(number) && std::abs(number) <= 9007199254740991.0;
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        for (const QVariant &child : value.toList())
            if (!jsonSafe(child, depth + 1, nodes)) return false;
        return true;
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (it.key().size() > 256 || !validText(it.key(), false)
                || !jsonSafe(it.value(), depth + 1, nodes)) return false;
        return true;
    }
    default:
        return false;
    }
}

bool exactKeys(const QJsonObject &object, const QStringList &expected)
{
    if (object.size() != expected.size()) return false;
    for (const QString &key : expected)
        if (!object.contains(key)) return false;
    return true;
}

bool timestamp(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > 9007199254740991.0
        || std::floor(number) != number) return false;
    *result = static_cast<qint64>(number);
    return true;
}

QString referenceKey(const QVariantMap &reference)
{
    return reference.value(QStringLiteral("key")).toString();
}
}

NamedCollectionsStore::NamedCollectionsStore(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath.trimmed())
{
    setObjectName(QStringLiteral("namedCollectionsStore"));
    load(false);
}

bool NamedCollectionsStore::boundedText(const QString &value, qsizetype limit, bool multiline)
{
    return value.size() <= limit && validText(value, multiline);
}

QString NamedCollectionsStore::normalName(const QString &value)
{
    if (!boundedText(value, 512)) return {};
    const QString normalized = value.normalized(QString::NormalizationForm_C).simplified();
    return normalized.isEmpty() || normalized.size() > 100 ? QString() : normalized;
}

QString NamedCollectionsStore::nameKey(const QString &value)
{
    return value.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

bool NamedCollectionsStore::normalizedReference(const QVariantMap &input, QVariantMap *output)
{
    if (!output) return false;
    const QList<QPair<QString, qsizetype>> fields = {
        {QStringLiteral("key"), 8192}, {QStringLiteral("world"), 80},
        {QStringLiteral("kind"), 80}, {QStringLiteral("mediaId"), 256},
        {QStringLiteral("source"), 120}, {QStringLiteral("sourceId"), 4096},
        {QStringLiteral("editionId"), 2048}, {QStringLiteral("title"), 1024},
        {QStringLiteral("cover"), 4096}, {QStringLiteral("creator"), 1024},
        {QStringLiteral("year"), 64}, {QStringLiteral("route"), 120}
    };
    QVariantMap clean;
    for (const auto &field : fields) {
        const QVariant value = input.value(field.first);
        if (value.isValid() && value.metaType().id() != QMetaType::QString) return false;
        const QString raw = value.toString();
        if (!boundedText(raw, field.second)) return false;
        QString text = raw.trimmed();
        if (field.first == QLatin1String("world") || field.first == QLatin1String("kind")
            || field.first == QLatin1String("source")) text = text.toLower();
        if (!boundedText(text, field.second)) return false;
        clean.insert(field.first, text);
    }
    for (const QString &field : {QStringLiteral("key"), QStringLiteral("world"),
            QStringLiteral("kind"), QStringLiteral("source"), QStringLiteral("sourceId")})
        if (clean.value(field).toString().isEmpty()) return false;
    const QVariant payload = input.value(QStringLiteral("payload"), QVariantMap());
    if (payload.metaType().id() != QMetaType::QVariantMap) return false;
    int nodes = 0;
    if (!jsonSafe(payload, 0, &nodes)) return false;
    // Freeze only JSON values. Provider admission and payload allowlisting belong
    // to the existing identity/reopen adapter, never a second resolver here.
    const QJsonObject payloadJson = QJsonObject::fromVariantMap(payload.toMap());
    clean.insert(QStringLiteral("payload"), payloadJson.toVariantMap());
    if (QJsonDocument(QJsonObject::fromVariantMap(clean)).toJson(QJsonDocument::Compact).size()
        > kMaxReferenceBytes) return false;
    *output = std::move(clean);
    return true;
}

bool NamedCollectionsStore::sameIdentity(const QVariantMap &left, const QVariantMap &right)
{
    for (const QString &field : {QStringLiteral("mediaId"), QStringLiteral("editionId")})
        if (left.value(field) != right.value(field)) return false;
    // A validated canonical id can intentionally join owning-world routes
    // (Theatre/movie and Vault/film). The registry/admission adapter owns that
    // equivalence; keep the first saved route rather than creating a duplicate.
    if (left.value(QStringLiteral("mediaId")).toString().isEmpty()) {
        for (const QString &field : {QStringLiteral("world"), QStringLiteral("kind"),
                QStringLiteral("source"), QStringLiteral("sourceId")})
            if (left.value(field) != right.value(field)) return false;
    }
    return true;
}

int NamedCollectionsStore::indexForId(const QVector<List> &lists, const QString &id)
{
    for (int i = 0; i < lists.size(); ++i)
        if (lists.at(i).id == id) return i;
    return -1;
}

bool NamedCollectionsStore::hasName(const QVector<List> &lists, const QString &name,
                                    const QString &excludingId)
{
    const QString folded = nameKey(name);
    for (const List &list : lists)
        if (list.id != excludingId && nameKey(list.name) == folded) return true;
    return false;
}

QVariantMap NamedCollectionsStore::row(const List &list)
{
    QStringList keys;
    QVariantMap notes;
    QVariantList members;
    for (const Member &member : list.members) {
        const QString key = referenceKey(member.reference);
        keys.append(key);
        if (!member.note.isEmpty()) notes.insert(key, member.note);
        members.append(member.reference);
    }
    return {{QStringLiteral("id"), list.id}, {QStringLiteral("name"), list.name},
        {QStringLiteral("description"), list.description}, {QStringLiteral("items"), keys},
        {QStringLiteral("notes"), notes}, {QStringLiteral("members"), members},
        {QStringLiteral("count"), list.members.size()}, {QStringLiteral("createdAt"), list.createdAt},
        {QStringLiteral("updatedAt"), list.updatedAt}};
}

QVariantList NamedCollectionsStore::collections() const
{
    QVariantList rows;
    for (const List &list : m_lists) rows.append(row(list));
    return rows;
}

QVariantMap NamedCollectionsStore::collection(const QString &collectionId) const
{
    const int index = indexForId(m_lists, collectionId);
    return index < 0 ? QVariantMap() : row(m_lists.at(index));
}

QVariantList NamedCollectionsStore::items(const QString &collectionId) const
{
    QVariantList result;
    const int index = indexForId(m_lists, collectionId);
    if (index < 0) return result;
    for (const Member &member : m_lists.at(index).members) {
        QVariantMap reference = member.reference;
        reference.insert(QStringLiteral("note"), member.note);
        result.append(reference);
    }
    return result;
}

QStringList NamedCollectionsStore::memberships(const QString &key) const
{
    QStringList result;
    for (const List &list : m_lists)
        for (const Member &member : list.members)
            if (referenceKey(member.reference) == key) { result.append(list.id); break; }
    return result;
}

bool NamedCollectionsStore::contains(const QString &collectionId, const QString &key) const
{
    return memberships(key).contains(collectionId);
}

bool NamedCollectionsStore::mutableState()
{
    if (!m_loadHealthy)
        return fail(tr("Collections could not be loaded. The existing file has been preserved."));
    return true;
}

QString NamedCollectionsStore::createCollection(const QString &name,
    const QVariantMap &initialReference, const QString &description)
{
    if (!mutableState()) return {};
    const QString normalized = normalName(name);
    if (normalized.isEmpty()) { fail(tr("Enter a collection name of 1–100 characters.")); return {}; }
    if (hasName(m_lists, normalized)) { fail(tr("A collection already has that name.")); return {}; }
    if (!boundedText(description, 300, true)) { fail(tr("Keep the description to 300 characters.")); return {}; }
    if (m_lists.size() >= kMaxCollections) { fail(tr("The local collection limit has been reached.")); return {}; }
    QVariantMap reference;
    if (!initialReference.isEmpty()) {
        if (!normalizedReference(initialReference, &reference)) { fail(tr("The title reference is invalid.")); return {}; }
        for (const List &existing : m_lists)
            for (const Member &member : existing.members)
                if (referenceKey(member.reference) == referenceKey(reference)) {
                    if (!sameIdentity(member.reference, reference)) { fail(tr("The title identity conflicts with an existing reference.")); return {}; }
                    reference = member.reference;
                }
    }
    List list;
    list.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    list.name = normalized;
    list.description = description.trimmed();
    list.createdAt = list.updatedAt = QDateTime::currentMSecsSinceEpoch();
    if (!reference.isEmpty()) list.members.append({reference, {}});
    QVector<List> next = m_lists;
    next.prepend(list);
    return commit(std::move(next)) ? list.id : QString();
}

QVariantMap NamedCollectionsStore::saveMemberships(const QVariantMap &input,
    const QStringList &selectedIds, const QString &newCollectionName)
{
    if (!mutableState()) return result(false);
    QVariantMap reference;
    if (!normalizedReference(input, &reference)) { fail(tr("The title reference is invalid.")); return result(false); }
    QSet<QString> selected;
    for (const QString &id : selectedIds) {
        if (selected.contains(id) || indexForId(m_lists, id) < 0) {
            fail(tr("A selected collection is missing or repeated.")); return result(false);
        }
        selected.insert(id);
    }
    QString newId;
    QString newName;
    if (!newCollectionName.isEmpty()) {
        newName = normalName(newCollectionName);
        if (newName.isEmpty()) { fail(tr("Enter a collection name of 1–100 characters.")); return result(false); }
        if (hasName(m_lists, newName)) { fail(tr("A collection already has that name.")); return result(false); }
        if (m_lists.size() >= kMaxCollections) { fail(tr("The local collection limit has been reached.")); return result(false); }
        newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        selected.insert(newId);
    }
    const QString key = referenceKey(reference);
    for (const List &list : m_lists)
        for (const Member &member : list.members)
            if (referenceKey(member.reference) == key) {
                if (!sameIdentity(member.reference, reference)) { fail(tr("The title identity conflicts with an existing reference.")); return result(false); }
                reference = member.reference;
            }
    QVector<List> next = m_lists;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    if (!newId.isEmpty()) {
        List list;
        list.id = newId;
        list.name = newName;
        list.createdAt = list.updatedAt = now;
        next.prepend(list);
        changed = true;
    }
    for (List &list : next) {
        int found = -1;
        for (int i = 0; i < list.members.size(); ++i)
            if (referenceKey(list.members.at(i).reference) == key) { found = i; break; }
        if (selected.contains(list.id) && found < 0) {
            if (list.members.size() >= kMaxMembers) { fail(tr("A collection has reached its title limit.")); return result(false); }
            list.members.append({reference, {}});
            list.updatedAt = now;
            changed = true;
        } else if (!selected.contains(list.id) && found >= 0) {
            list.members.removeAt(found);
            list.updatedAt = now;
            changed = true;
        }
    }
    if (!changed) return result(commit(std::move(next)));
    return result(commit(std::move(next)), newId);
}

bool NamedCollectionsStore::deleteCollection(const QString &collectionId)
{
    if (!mutableState()) return false;
    const int index = indexForId(m_lists, collectionId);
    if (index < 0) return fail(tr("This collection no longer exists."));
    QVector<List> next = m_lists;
    next.removeAt(index);
    return commit(std::move(next));
}

QVariantMap NamedCollectionsStore::beginEdit(const QString &collectionId)
{
    if (!mutableState()) return {};
    const int index = indexForId(m_lists, collectionId);
    if (index < 0) { fail(tr("This collection no longer exists.")); return {}; }
    if (m_drafts.size() >= kMaxDrafts) { fail(tr("Close another collection editor before opening this one.")); return {}; }
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_drafts.insert(token, {collectionId, m_revision});
    QVariantMap draft = row(m_lists.at(index));
    draft.insert(QStringLiteral("token"), token);
    draft.insert(QStringLiteral("revision"), m_revision);
    setError({});
    return draft;
}

bool NamedCollectionsStore::saveEdit(const QString &token, const QString &name,
    const QString &description, const QStringList &orderedKeys, const QVariantMap &notes)
{
    if (!mutableState()) return false;
    const auto draft = m_drafts.constFind(token);
    if (draft == m_drafts.cend()) return fail(tr("This edit belongs to a closed or different profile."));
    if (draft->revision != m_revision) return fail(tr("Collections changed while you were editing. Cancel and reopen the editor."));
    const int index = indexForId(m_lists, draft->collectionId);
    if (index < 0) return fail(tr("This collection no longer exists."));
    const QString normalized = normalName(name);
    if (normalized.isEmpty()) return fail(tr("Enter a collection name of 1–100 characters."));
    if (hasName(m_lists, normalized, draft->collectionId)) return fail(tr("A collection already has that name."));
    if (!boundedText(description, 300, true)) return fail(tr("Keep the description to 300 characters."));
    QHash<QString, Member> existing;
    for (const Member &member : m_lists.at(index).members)
        existing.insert(referenceKey(member.reference), member);
    for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
        if (!existing.contains(it.key()) || it.value().metaType().id() != QMetaType::QString
            || !boundedText(it.value().toString(), 500, true))
            return fail(tr("A personal note is invalid or longer than 500 characters."));
    }
    QVector<Member> reordered;
    QSet<QString> seen;
    for (const QString &key : orderedKeys) {
        if (!existing.contains(key) || seen.contains(key))
            return fail(tr("The saved order contains a missing or repeated title."));
        seen.insert(key);
        Member member = existing.value(key);
        member.note = notes.value(key).toString();
        reordered.append(std::move(member));
    }
    QVector<List> next = m_lists;
    next[index].name = normalized;
    next[index].description = description.trimmed();
    next[index].members = std::move(reordered);
    if (serialize(next) == serialize(m_lists)) {
        if (!commit(std::move(next))) return false;
        m_drafts.remove(token);
        return true;
    }
    next[index].updatedAt = QDateTime::currentMSecsSinceEpoch();
    if (!commit(std::move(next))) return false;
    m_drafts.remove(token);
    return true;
}

bool NamedCollectionsStore::cancelEdit(const QString &token)
{
    if (!m_drafts.remove(token)) return fail(tr("This collection edit is already closed."));
    setError({});
    return true;
}

QByteArray NamedCollectionsStore::serialize(const QVector<List> &lists)
{
    QJsonArray rows;
    for (const List &list : lists) {
        QJsonArray members;
        for (const Member &member : list.members)
            members.append(QJsonObject{{QStringLiteral("reference"), QJsonObject::fromVariantMap(member.reference)},
                                       {QStringLiteral("note"), member.note}});
        rows.append(QJsonObject{{QStringLiteral("id"), list.id}, {QStringLiteral("name"), list.name},
            {QStringLiteral("description"), list.description}, {QStringLiteral("createdAt"), list.createdAt},
            {QStringLiteral("updatedAt"), list.updatedAt}, {QStringLiteral("items"), members}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), kSchema},
        {QStringLiteral("collections"), rows}}).toJson(QJsonDocument::Compact);
}

bool NamedCollectionsStore::parse(const QByteArray &bytes, QVector<List> *lists, QString *error)
{
    const auto invalid = [&] {
        if (error) *error = tr("Invalid collections data. The original file has been preserved.");
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("schemaVersion")).isDouble()
        || root.value(QStringLiteral("schemaVersion")).toDouble() != kSchema) {
        if (error) *error = tr("This collections file uses an unsupported version. It has not been changed.");
        return false;
    }
    if (!exactKeys(root, {QStringLiteral("schemaVersion"), QStringLiteral("collections")})
        || !root.value(QStringLiteral("collections")).isArray()) return invalid();
    const QJsonArray rows = root.value(QStringLiteral("collections")).toArray();
    if (rows.size() > kMaxCollections) return invalid();
    QVector<List> loaded;
    QSet<QString> ids;
    QHash<QString, QVariantMap> identities;
    for (const QJsonValue &value : rows) {
        if (!value.isObject()) return invalid();
        const QJsonObject object = value.toObject();
        if (!exactKeys(object, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("description"),
                QStringLiteral("createdAt"), QStringLiteral("updatedAt"), QStringLiteral("items")})) return invalid();
        List list;
        list.id = object.value(QStringLiteral("id")).toString();
        const QUuid uuid(list.id);
        const QJsonValue rawName = object.value(QStringLiteral("name"));
        list.name = normalName(rawName.toString());
        const QJsonValue description = object.value(QStringLiteral("description"));
        list.description = description.toString();
        if (uuid.isNull() || list.id != uuid.toString(QUuid::WithoutBraces) || ids.contains(list.id)
            || !rawName.isString() || list.name.isEmpty() || list.name != rawName.toString()
            || hasName(loaded, list.name) || !description.isString()
            || !boundedText(list.description, 300, true)
            || !timestamp(object.value(QStringLiteral("createdAt")), &list.createdAt)
            || !timestamp(object.value(QStringLiteral("updatedAt")), &list.updatedAt)
            || !object.value(QStringLiteral("items")).isArray()) return invalid();
        const QJsonArray members = object.value(QStringLiteral("items")).toArray();
        if (members.size() > kMaxMembers) return invalid();
        QSet<QString> keys;
        for (const QJsonValue &value : members) {
            if (!value.isObject()) return invalid();
            const QJsonObject object = value.toObject();
            const QJsonValue rawReference = object.value(QStringLiteral("reference"));
            const QJsonValue rawNote = object.value(QStringLiteral("note"));
            QVariantMap reference;
            if (!exactKeys(object, {QStringLiteral("reference"), QStringLiteral("note")})
                || !rawReference.isObject() || !rawNote.isString()
                || !boundedText(rawNote.toString(), 500, true)
                || !normalizedReference(rawReference.toObject().toVariantMap(), &reference)
                || QJsonObject::fromVariantMap(reference) != rawReference.toObject()) return invalid();
            const QString key = referenceKey(reference);
            if (keys.contains(key) || (identities.contains(key)
                    && !sameIdentity(identities.value(key), reference))) return invalid();
            keys.insert(key);
            identities.insert(key, reference);
            list.members.append({reference, rawNote.toString()});
        }
        ids.insert(list.id);
        loaded.append(std::move(list));
    }
    *lists = std::move(loaded);
    return true;
}

bool NamedCollectionsStore::mergeForAdoption(const QByteArray &account, const QByteArray &local,
    QByteArray *merged, QString *error)
{
    if (!merged || account.size() > kMaxFileBytes || local.size() > kMaxFileBytes) {
        if (error) *error = tr("Collections exceed the local adoption limit. Both originals have been preserved.");
        return false;
    }
    QVector<List> combined, incoming;
    if (!parse(account, &combined, error) || !parse(local, &incoming, error)) return false;
    for (List list : incoming) {
        const int existing = indexForId(combined, list.id);
        if (existing >= 0 && serialize({combined.at(existing)}) == serialize({list})) continue;
        // Imported versions get a stable id even when only their display name
        // collides. Retrying the same source must not duplicate a renamed copy
        // or overwrite edits made to it since the previous successful import.
        list.id = QUuid::createUuidV5(QUuid(list.id),
            QCryptographicHash::hash(serialize({list}), QCryptographicHash::Sha256))
            .toString(QUuid::WithoutBraces);
        if (indexForId(combined, list.id) >= 0) continue;
        const QString originalName = list.name;
        for (int number = 1; hasName(combined, list.name); ++number) {
            const QString suffix = number == 1 ? tr(" (from device)")
                : tr(" (from device %1)").arg(number);
            QString prefix = originalName.left(100 - suffix.size()).trimmed();
            if (!prefix.isEmpty() && prefix.back().isHighSurrogate()) prefix.chop(1);
            list.name = normalName(prefix + suffix);
        }
        combined.append(std::move(list));
    }
    const QByteArray bytes = serialize(combined);
    QVector<List> verified;
    if (bytes.size() > kMaxFileBytes || !parse(bytes, &verified, error)) {
        if (error && error->isEmpty()) *error = tr("Collections could not be combined within local limits. Both originals have been preserved.");
        return false;
    }
    *merged = bytes;
    return true;
}

bool NamedCollectionsStore::commit(QVector<List> candidate)
{
    const QByteArray bytes = serialize(candidate);
    if (bytes.size() > kMaxFileBytes) return fail(tr("Collections exceed the local storage limit."));
    const bool noOp = bytes == serialize(m_lists);
    const QFileInfo info(m_filePath);
    if (!QDir().mkpath(info.absolutePath())) return fail(tr("The collections directory is not writable."));
    QLockFile lock(m_filePath + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return fail(tr("Collections are being saved elsewhere. Try again."));
    if (QFileInfo(m_filePath).isSymLink())
        return fail(tr("The collections file is a symbolic link. It has not been changed."));
    QFile current(m_filePath);
    if (current.exists() != m_fileExisted) return fail(tr("The collections file changed elsewhere. Reload it before saving."));
    if (m_fileExisted) {
        if (!current.open(QIODevice::ReadOnly) || current.size() > kMaxFileBytes)
            return fail(tr("The existing collections file could not be verified. It has not been changed."));
        const QByteArray currentBytes = current.read(kMaxFileBytes + 1);
        if (currentBytes.size() > kMaxFileBytes || current.error() != QFileDevice::NoError
            || QCryptographicHash::hash(currentBytes, QCryptographicHash::Sha256) != m_diskDigest)
            return fail(tr("The collections file changed elsewhere. Reload it before saving."));
        current.close();
    }
    if (noOp) { setError({}); return true; }
    QSaveFile file(m_filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(tr("Collections could not be saved: %1").arg(file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        return fail(tr("Collections could not be saved: %1").arg(message));
    }
    if (!file.commit()) return fail(tr("Collections could not be saved: %1").arg(file.errorString()));
    m_lists.swap(candidate);
    m_diskDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    m_fileExisted = true;
    ++m_revision;
    setError({});
    emit changed();
    return true;
}

bool NamedCollectionsStore::load(bool notify)
{
    const bool wasHealthy = m_loadHealthy;
    QString message;
    QVector<List> loaded;
    QByteArray bytes;
    QFile file(m_filePath);
    const bool exists = !m_filePath.isEmpty() && file.exists();
    bool valid = !m_filePath.isEmpty();
    if (!valid) message = tr("No collections profile is active.");
    if (valid && QFileInfo(m_filePath).isSymLink()) {
        valid = false;
        message = tr("The collections file is a symbolic link. It has been preserved.");
    }
    if (valid && exists) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxFileBytes) {
            valid = false;
            message = tr("The existing collections file could not be read. It has been preserved.");
        } else {
            bytes = file.read(kMaxFileBytes + 1);
            valid = bytes.size() <= kMaxFileBytes && file.error() == QFileDevice::NoError
                && parse(bytes, &loaded, &message);
            if (!valid && message.isEmpty()) message = tr("The collections file could not be read completely.");
        }
    }
    m_loadHealthy = valid;
    const bool errorChangedAlready = m_error != message;
    setError(message);
    if (wasHealthy != m_loadHealthy && !errorChangedAlready) emit errorChanged();
    if (!valid) return false;
    m_lists.swap(loaded);
    m_drafts.clear();
    m_fileExisted = exists;
    m_diskDigest = exists ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) : QByteArray();
    if (notify) { ++m_revision; emit changed(); }
    return true;
}

bool NamedCollectionsStore::reload()
{
    if (!m_drafts.isEmpty()) return fail(tr("Cancel the open collection editor before reloading."));
    return load(true);
}

bool NamedCollectionsStore::fail(const QString &message)
{
    setError(message);
    return false;
}

QVariantMap NamedCollectionsStore::result(bool ok, const QString &collectionId) const
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("collectionId"), ok ? collectionId : QString()},
            {QStringLiteral("error"), ok ? QString() : m_error}};
}

void NamedCollectionsStore::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}
