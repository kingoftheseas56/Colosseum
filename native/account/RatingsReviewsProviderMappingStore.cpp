#include "RatingsReviewsProviderMappingStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace {

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

RatingsReviewsProviderMappingStore::RatingsReviewsProviderMappingStore(
    const QString &path)
    : m_path(path)
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews mapping path is empty.");
    } else {
        load();
    }
}

bool RatingsReviewsProviderMappingStore::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_persistenceError;
    return m_healthy;
}

QString RatingsReviewsProviderMappingStore::persistenceError() const
{
    return m_persistenceError;
}

QString RatingsReviewsProviderMappingStore::storagePath() const
{
    return m_path;
}

std::optional<RatingsReviewsProviderMapping>
RatingsReviewsProviderMappingStore::mapping(
    const QString &providerId,
    const QString &canonicalKey) const
{
    for (const auto &entry : m_mappings) {
        if (entry.providerId == providerId && entry.canonicalKey == canonicalKey)
            return entry;
    }
    return std::nullopt;
}

QList<RatingsReviewsProviderMapping> RatingsReviewsProviderMappingStore::mappings() const
{
    return m_mappings;
}

bool RatingsReviewsProviderMappingStore::upsert(
    const RatingsReviewsProviderMapping &mapping,
    QString *error)
{
    if (!healthy(error))
        return false;
    if (mapping.status != QLatin1String("matched")
        && (!mapping.providerMediaId.isEmpty() || !mapping.resolution.isEmpty())) {
        return setError(error, QStringLiteral("Unmatched mapping contains writable media identity."));
    }
    if (!ratingsReviewsDeliveryMappingFromJson(
            ratingsReviewsDeliveryMappingToJson(mapping), error))
        return false;
    QList<RatingsReviewsProviderMapping> candidate = m_mappings;
    bool replaced = false;
    for (auto &entry : candidate) {
        if (entry.providerId == mapping.providerId
            && entry.canonicalKey == mapping.canonicalKey) {
            entry = mapping;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        candidate.append(mapping);
    if (!persist(candidate, error))
        return false;
    m_mappings = candidate;
    return true;
}

bool RatingsReviewsProviderMappingStore::replaceAll(
    const QList<RatingsReviewsProviderMapping> &mappings,
    QString *error)
{
    if (!healthy(error))
        return false;
    QSet<QString> keys;
    for (const auto &mapping : mappings) {
        QString parseError;
        if (!ratingsReviewsDeliveryMappingFromJson(
                ratingsReviewsDeliveryMappingToJson(mapping), &parseError)) {
            return setError(error, parseError);
        }
        const QString key = ratingsReviewsDeliveryMappingKey(
            mapping.providerId, mapping.canonicalKey);
        if (keys.contains(key))
            return setError(error, QStringLiteral("Duplicate Ratings/Reviews mapping binding."));
        keys.insert(key);
    }
    if (!persist(mappings, error))
        return false;
    m_mappings = mappings;
    return true;
}

bool RatingsReviewsProviderMappingStore::flush(QString *error) const
{
    if (!healthy(error))
        return false;
    return persist(m_mappings, error);
}

bool RatingsReviewsProviderMappingStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Could not open Ratings/Reviews mappings.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews mappings are malformed.");
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1
        || !root.value(QStringLiteral("mappings")).isArray()) {
        m_healthy = false;
        m_persistenceError = QStringLiteral("Ratings/Reviews mappings have an unsupported schema.");
        return false;
    }
    QSet<QString> keys;
    QList<RatingsReviewsProviderMapping> loaded;
    for (const QJsonValue &value : root.value(QStringLiteral("mappings")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_persistenceError = QStringLiteral("Ratings/Reviews mapping entry is malformed.");
            return false;
        }
        QString error;
        const auto mapping = ratingsReviewsDeliveryMappingFromJson(value.toObject(), &error);
        if (!mapping) {
            m_healthy = false;
            m_persistenceError = error;
            return false;
        }
        const QString key = ratingsReviewsDeliveryMappingKey(
            mapping->providerId, mapping->canonicalKey);
        if (keys.contains(key)) {
            m_healthy = false;
            m_persistenceError = QStringLiteral("Ratings/Reviews mappings contain duplicate bindings.");
            return false;
        }
        keys.insert(key);
        loaded.append(*mapping);
    }
    m_mappings = loaded;
    return true;
}

bool RatingsReviewsProviderMappingStore::persist(
    const QList<RatingsReviewsProviderMapping> &candidate,
    QString *error) const
{
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(error, QStringLiteral("Could not create Ratings/Reviews mapping directory."));
    QJsonArray array;
    for (const auto &mapping : candidate)
        array.append(ratingsReviewsDeliveryMappingToJson(mapping));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Could not write Ratings/Reviews mappings."));
    file.write(QJsonDocument({
        {QStringLiteral("version"), 1},
        {QStringLiteral("mappings"), array}}).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(error, QStringLiteral("Could not atomically save Ratings/Reviews mappings."));
    return true;
}
