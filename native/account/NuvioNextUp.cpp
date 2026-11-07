#include "NuvioNextUp.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
constexpr qsizetype kMaxKeys = 4096;
constexpr qsizetype kMaxKeyLength = 512;
constexpr qsizetype kMaxSettingsBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxLocalBytes = 8 * 1024 * 1024;
const auto kFeature = QStringLiteral("continue_watching_settings_payload");
const auto kDismissals = QStringLiteral("dismissedNextUpKeys");

bool validText(const QString &text) {
    return text.size() <= kMaxKeyLength && !text.trimmed().isEmpty()
        && std::none_of(text.cbegin(), text.cend(), [](QChar character) {
               return character.category() == QChar::Other_Control;
           });
}

bool validSeed(const QString &seed) {
    if (!validText(seed))
        return false;
    const QStringList parts = seed.split(QLatin1Char('|'));
    if (parts.size() != 3)
        return false;
    bool seasonOk = false;
    bool episodeOk = false;
    const int season = parts.at(1).toInt(&seasonOk);
    const int episode = parts.at(2).toInt(&episodeOk);
    return seasonOk && episodeOk && NuvioNextUp::key(parts.first(), season, episode) == seed;
}

bool validDismissalKey(const QString &key) {
    if (key.contains(QLatin1Char('|')))
        return validSeed(key);
    return key == key.trimmed() && !NuvioNextUp::key(key, -1, -1).isEmpty();
}

bool validTimestamp(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 64 || text.trimmed() != text)
        return false;
    const QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    return date.isValid() && date.timeSpec() != Qt::LocalTime;
}

bool readDismissals(const QJsonValue &value, QStringList *dismissed) {
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() > kMaxKeys)
        return false;
    for (const QJsonValue &entry : array) {
        const QString text = entry.toString();
        // Preserve provider values we do not recognize so an edit cannot erase them.
        if (!validText(text))
            return false;
        dismissed->append(text);
    }
    return true;
}

bool readSettings(const QJsonObject &settings, QJsonObject *features, QJsonObject *payload,
                  QStringList *dismissed) {
    if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > kMaxSettingsBytes)
        return false;
    const QJsonValue version = settings.value("version");
    if (!version.isUndefined() && version.toInt() <= 0)
        return false;
    const QJsonValue featuresValue = settings.value("features");
    if (featuresValue.isUndefined())
        return true;
    if (!featuresValue.isObject())
        return false;
    *features = featuresValue.toObject();
    const QJsonValue payloadValue = features->value(kFeature);
    if (payloadValue.isObject()) {
        *payload = payloadValue.toObject();
    } else if (payloadValue.isString()) {
        const QString text = payloadValue.toString().trimmed();
        if (!text.isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
            if (!document.isObject())
                return false;
            *payload = document.object();
        }
    } else if (!payloadValue.isUndefined() && !payloadValue.isNull()) {
        return false;
    }
    // Older clients used these names and sometimes stored the array directly
    // in features. Prefer the current payload so an undo cannot revive old data.
    for (const QString &field : {kDismissals, QStringLiteral("dismissedKeys"),
             QStringLiteral("dismissed_next_up_keys")}) {
        const QJsonValue keys = payload->value(field);
        if (!keys.isUndefined() && !keys.isNull())
            return readDismissals(keys, dismissed);
    }
    const QJsonValue keys = features->value(kDismissals);
    return keys.isUndefined() || keys.isNull() || readDismissals(keys, dismissed);
}
}

bool NuvioNextUp::activate(const QString &directory, const QString &accountId,
                           const QString &profileId, int profileIndex) {
    m_path.clear();
    m_scope.clear();
    m_profileIndex = 0;
    m_remote.clear();
    m_pending.clear();
    m_receipt = {};
    if (directory.isEmpty())
        return false;

    const QUuid account(accountId);
    const QUuid profile(profileId);
    if (account.isNull() || profile.isNull() || profileIndex < 1 || profileIndex > 6)
        return false;

    const QByteArray identity = account.toRfc4122() + profile.toRfc4122()
        + QByteArray::number(profileIndex);
    const QString scope = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString path = QDir(directory).filePath(QStringLiteral("next-up-v1-%1.json").arg(scope));
    QFile file(path);
    QStringList remote;
    QMap<QString, Edit> pending;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray bytes = file.read(kMaxLocalBytes + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > kMaxLocalBytes)
            return false;
        const QJsonObject stored = QJsonDocument::fromJson(bytes).object();
        const QJsonValue pendingValue = stored.value("pending");
        if (stored.value("version").toInt() != 1 || stored.value("scope").toString() != scope
            || !readDismissals(stored.value("remote"), &remote) || !pendingValue.isObject()) {
            return false;
        }
        const QJsonObject edits = pendingValue.toObject();
        if (edits.size() > kMaxKeys)
            return false;
        for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
            const QJsonObject edit = it.value().toObject();
            const QUuid id(edit.value("id").toString());
            const QJsonValue dismissed = edit.value("dismissed");
            if (!validDismissalKey(it.key()) || id.isNull() || !dismissed.isBool())
                return false;
            pending.insert(it.key(), {dismissed.toBool(), id});
        }
    }
    m_path = path;
    m_scope = scope;
    m_profileIndex = profileIndex;
    m_remote = std::move(remote);
    m_pending = std::move(pending);
    return true;
}

QString NuvioNextUp::key(const QString &contentId, int season, int episode) {
    const QString id = contentId.trimmed();
    if (!validText(id) || id.contains(QLatin1Char('|')) || season < -1 || episode < -1)
        return {};
    const QString lower = id.toLower();
    if (lower == QLatin1String("tmdb") || lower == QLatin1String("tmdb:")
        || lower == QLatin1String("imdb") || lower == QLatin1String("imdb:")
        || lower == QLatin1String("trakt") || lower == QLatin1String("trakt:")) {
        return {};
    }
    const QString seed = id + QLatin1Char('|') + QString::number(season)
        + QLatin1Char('|') + QString::number(episode);
    return seed.size() <= kMaxKeyLength ? seed : QString();
}

bool NuvioNextUp::isDismissed(const QString &key) const {
    if (!validDismissalKey(key))
        return false;
    const auto edit = m_pending.constFind(key);
    return edit != m_pending.cend() ? edit->dismissed : m_remote.contains(key);
}

QStringList NuvioNextUp::dismissedKeys() const {
    QStringList keys;
    for (const auto &key : m_remote) {
        const auto edit = m_pending.constFind(key);
        if (validDismissalKey(key) && (edit == m_pending.cend() || edit->dismissed))
            keys.append(key);
    }
    const QSet<QString> remote(m_remote.cbegin(), m_remote.cend());
    for (auto edit = m_pending.cbegin(); edit != m_pending.cend(); ++edit) {
        if (edit->dismissed && !remote.contains(edit.key()))
            keys.append(edit.key());
    }
    return keys;
}

bool NuvioNextUp::setDismissed(const QString &key, bool dismissed) {
    return setDismissed(QStringList{key}, dismissed);
}

bool NuvioNextUp::setDismissed(const QStringList &keys, bool dismissed) {
    if (m_path.isEmpty())
        return false;
    auto pending = m_pending;
    bool changed = false;
    for (const auto &key : keys) {
        if (!validDismissalKey(key))
            return false;
        const auto current = pending.constFind(key);
        if (current != pending.cend() && current->dismissed == dismissed)
            continue;
        pending.insert(key, {dismissed, QUuid::createUuid()});
        changed = true;
    }
    if (!changed)
        return true;
    if (pending.size() > kMaxKeys || !persist(m_remote, pending))
        return false;
    m_pending = std::move(pending);
    return true;
}

bool NuvioNextUp::applyPull(const QJsonValue &rows, std::optional<Push> *push) {
    if (!push)
        return false;
    push->reset();
    const QJsonArray array = rows.toArray();
    if (m_path.isEmpty() || !rows.isArray() || array.size() > 1)
        return false;

    QJsonObject settings;
    QJsonValue expected(QJsonValue::Null);
    if (!array.isEmpty()) {
        const QJsonObject row = array.first().toObject();
        const QJsonValue settingsValue = row.value("settings_json");
        expected = row.value("updated_at");
        if (row.value("profile_id").toInt() != m_profileIndex
            || !settingsValue.isObject() || !validTimestamp(expected)) {
            return false;
        }
        settings = settingsValue.toObject();
    }

    QJsonObject features;
    QJsonObject payload;
    QStringList remote;
    if (!readSettings(settings, &features, &payload, &remote))
        return false;

    std::optional<Push> candidate;
    if (hasPending()) {
        QStringList merged;
        for (const QString &key : remote) {
            const auto edit = m_pending.constFind(key);
            if (edit == m_pending.cend() || edit->dismissed)
                merged.append(key);
        }
        const QSet<QString> existing(merged.cbegin(), merged.cend());
        for (auto edit = m_pending.cbegin(); edit != m_pending.cend(); ++edit) {
            if (edit->dismissed && !existing.contains(edit.key()))
                merged.append(edit.key());
        }
        if (merged.size() > kMaxKeys)
            return false;
        if (merged != remote) {
            payload.insert(kDismissals, QJsonArray::fromStringList(merged));
            features.insert(kFeature, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            settings.insert(QStringLiteral("features"), features);
            if (!settings.contains("version"))
                settings.insert(QStringLiteral("version"), 4);
            if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > kMaxSettingsBytes)
                return false;
        }
        auto &plan = candidate.emplace();
        plan.settings = std::move(settings);
        plan.expectedUpdatedAt = expected;
        plan.changes = merged != remote;
        plan.receipt = QUuid::createUuid();
        plan.sent = m_pending;
        plan.dismissed = std::move(merged);
    }

    if (remote != m_remote && !persist(remote, m_pending))
        return false;
    m_remote = std::move(remote);
    m_receipt = candidate ? candidate->receipt : QUuid();
    *push = std::move(candidate);
    return true;
}

bool NuvioNextUp::acknowledge(const Push &push, const QJsonValue &updatedAt) {
    const bool observedAbsent = !push.changes && push.expectedUpdatedAt.isNull() && updatedAt.isNull();
    if (m_path.isEmpty() || m_receipt.isNull() || push.receipt != m_receipt
        || (!observedAbsent && !validTimestamp(updatedAt))) {
        return false;
    }
    auto pending = m_pending;
    for (auto sent = push.sent.cbegin(); sent != push.sent.cend(); ++sent) {
        const auto current = pending.constFind(sent.key());
        if (current != pending.cend() && current->id == sent->id
            && current->dismissed == sent->dismissed) {
            pending.remove(sent.key());
        }
    }
    if (!persist(push.dismissed, pending))
        return false;
    m_remote = push.dismissed;
    m_pending = std::move(pending);
    m_receipt = {};
    return true;
}

bool NuvioNextUp::persist(const QStringList &remote, const QMap<QString, Edit> &pending) const {
    QJsonObject edits;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        edits.insert(it.key(), QJsonObject{{"id", it->id.toString(QUuid::WithoutBraces)},
                                          {"dismissed", it->dismissed}});
    }
    const QJsonObject stored{{"version", 1}, {"scope", m_scope},
                             {"remote", QJsonArray::fromStringList(remote)}, {"pending", edits}};
    const QByteArray bytes = QJsonDocument(stored).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxLocalBytes || !QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return false;
    QSaveFile file(m_path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
