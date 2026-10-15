#include "RatingsReviewsConversionMap.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtMath>

namespace {
struct DomainDescriptor final {
    QString providerId;
    QString domainId;
    int domainVersion = 0;
    QList<QJsonValue> legalValues;
    QList<QJsonValue> recommendation;
};

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

QList<QJsonValue> halfPointValues(bool includeZero) {
    QList<QJsonValue> values;
    const int first = includeZero ? 0 : 1;
    for (int i = first; i <= 20; ++i)
        values.append(QJsonValue(i * 0.5));
    return values;
}

QList<DomainDescriptor> syntheticDomains() {
    const QList<QJsonValue> full = halfPointValues(true);
    QList<QJsonValue> noZero;
    noZero.append(QJsonValue(QJsonValue::Null));
    const QList<QJsonValue> positive = halfPointValues(false);
    for (const QJsonValue &value : positive)
        noZero.append(value);

    return {
        {QStringLiteral("fixture-a"),
         QStringLiteral("fixture-halfpoint-v1"), 1, full, full},
        {QStringLiteral("fixture-b"),
         QStringLiteral("fixture-halfpoint-v1"), 1, full, full},
        {QStringLiteral("fixture-a"),
         QStringLiteral("fixture-no-zero-v1"), 1, positive, noZero}};
}

std::optional<DomainDescriptor> descriptorFor(
    const QString &providerId,
    const QString &domainId,
    int domainVersion,
    const RatingsReviewsConversionTestHook &hook) {
    if (!hook.syntheticDomainsEnabledForTests())
        return std::nullopt;
    for (const DomainDescriptor &descriptor : syntheticDomains()) {
        if (descriptor.providerId == providerId
            && descriptor.domainId == domainId
            && descriptor.domainVersion == domainVersion) {
            return descriptor;
        }
    }
    return std::nullopt;
}

int exactRank(
    const QList<QJsonValue> &legalValues,
    const QJsonValue &candidate) {
    for (int i = 0; i < legalValues.size(); ++i) {
        if (legalValues.at(i) == candidate)
            return i;
    }
    return -1;
}
}

RatingsReviewsConversionTestHook::RatingsReviewsConversionTestHook(
    bool syntheticDomains,
    const SettingsFailureGate &settingsFailureGate)
    : m_syntheticDomains(syntheticDomains),
      m_settingsFailureGate(settingsFailureGate) {}

RatingsReviewsConversionTestHook::SettingsFailureGate
RatingsReviewsConversionTestHook::settingsFailureGate() {
    return std::make_shared<bool>(false);
}

RatingsReviewsConversionTestHook
RatingsReviewsConversionTestHook::syntheticDomains(
    const SettingsFailureGate &settingsFailureGate) {
    return RatingsReviewsConversionTestHook(
        true,
        settingsFailureGate);
}

bool RatingsReviewsConversionTestHook::syntheticDomainsEnabledForTests() const {
    return m_syntheticDomains;
}

bool RatingsReviewsConversionTestHook::settingsFailureRequestedForTests() const {
    return m_settingsFailureGate && *m_settingsFailureGate;
}

QStringList RatingsReviewsConversionMap::canonicalProviderIds() {
    return {
        QStringLiteral("mal"),
        QStringLiteral("anilist"),
        QStringLiteral("trakt"),
        QStringLiteral("simkl"),
        QStringLiteral("imdb"),
        QStringLiteral("tmdb"),
        QStringLiteral("rotten_tomatoes"),
        QStringLiteral("metacritic")};
}

bool RatingsReviewsConversionMap::isCanonicalProviderId(
    const QString &providerId) {
    return canonicalProviderIds().contains(providerId);
}

bool RatingsReviewsConversionMap::validate(
    const RatingsReviewsConversionMap &map,
    const RatingsReviewsConversionTestHook &testHook,
    QString *error) {
    if (map.version != 1)
        return fail(error, QStringLiteral("conversion_version_invalid"));
    if (map.domainVersion <= 0)
        return fail(error, QStringLiteral("conversion_domain_version_invalid"));
    if (map.outputs.size() != 21)
        return fail(error, QStringLiteral("conversion_output_count_invalid"));

    const auto descriptor = descriptorFor(
        map.providerId, map.domainId, map.domainVersion, testHook);
    if (!descriptor.has_value())
        return fail(error, QStringLiteral("conversion_domain_not_admitted"));

    int previousRank = -1;
    for (const QJsonValue &output : map.outputs) {
        if (output.isNull())
            continue;
        if (output.isUndefined()
            || output.isArray()
            || output.isObject()) {
            return fail(error, QStringLiteral("conversion_output_not_scalar"));
        }
        const int rank = exactRank(descriptor->legalValues, output);
        if (rank < 0)
            return fail(error, QStringLiteral("conversion_output_not_in_domain"));
        if (previousRank > rank)
            return fail(error, QStringLiteral("conversion_outputs_nonmonotonic"));
        previousRank = rank;
    }

    if (error)
        error->clear();
    return true;
}

QJsonObject RatingsReviewsConversionMap::toJson() const {
    QJsonArray outputArray;
    for (const QJsonValue &value : outputs)
        outputArray.append(value);
    return QJsonObject{
        {QStringLiteral("version"), version},
        {QStringLiteral("provider_id"), providerId},
        {QStringLiteral("domain_id"), domainId},
        {QStringLiteral("domain_version"), domainVersion},
        {QStringLiteral("outputs"), outputArray}};
}

QString RatingsReviewsConversionMap::digest() const {
    QJsonArray digestArray;
    digestArray.append(1);
    digestArray.append(providerId);
    digestArray.append(domainId);
    digestArray.append(domainVersion);

    QJsonArray outputArray;
    for (const QJsonValue &value : outputs)
        outputArray.append(value);
    digestArray.append(outputArray);
    const QByteArray bytes =
        QJsonDocument(digestArray).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

std::optional<RatingsReviewsConversionMap>
RatingsReviewsConversionMap::fromJson(
    const QJsonObject &object,
    const RatingsReviewsConversionTestHook &testHook,
    QString *error) {
    if (object.size() != 5
        || !object.value(QStringLiteral("version")).isDouble()
        || !object.value(QStringLiteral("provider_id")).isString()
        || !object.value(QStringLiteral("domain_id")).isString()
        || !object.value(QStringLiteral("domain_version")).isDouble()
        || !object.value(QStringLiteral("outputs")).isArray()) {
        fail(error, QStringLiteral("conversion_map_malformed"));
        return std::nullopt;
    }

    const double versionNumber =
        object.value(QStringLiteral("version")).toDouble();
    const double domainVersionNumber =
        object.value(QStringLiteral("domain_version")).toDouble();
    if (versionNumber != qFloor(versionNumber)
        || domainVersionNumber != qFloor(domainVersionNumber)) {
        fail(error, QStringLiteral("conversion_integer_field_invalid"));
        return std::nullopt;
    }

    RatingsReviewsConversionMap map;
    map.version = static_cast<int>(versionNumber);
    map.providerId = object.value(QStringLiteral("provider_id")).toString();
    map.domainId = object.value(QStringLiteral("domain_id")).toString();
    map.domainVersion = static_cast<int>(domainVersionNumber);
    const QJsonArray array =
        object.value(QStringLiteral("outputs")).toArray();
    map.outputs.reserve(array.size());
    for (const QJsonValue &value : array)
        map.outputs.append(value);

    if (!validate(map, testHook, error))
        return std::nullopt;
    return map;
}

std::optional<RatingsReviewsConversionMap>
RatingsReviewsConversionMap::recommended(
    const QString &providerId,
    const QString &domainId,
    int domainVersion,
    const RatingsReviewsConversionTestHook &testHook,
    QString *error) {
    const auto descriptor = descriptorFor(
        providerId, domainId, domainVersion, testHook);
    if (!descriptor.has_value()) {
        fail(error, QStringLiteral("conversion_domain_not_admitted"));
        return std::nullopt;
    }

    RatingsReviewsConversionMap map;
    map.version = 1;
    map.providerId = descriptor->providerId;
    map.domainId = descriptor->domainId;
    map.domainVersion = descriptor->domainVersion;
    map.outputs = descriptor->recommendation;
    if (!validate(map, testHook, error))
        return std::nullopt;
    return map;
}

QJsonValue RatingsReviewsConversionMap::translatedOutput(
    double canonicalScore,
    bool *available,
    QString *error) const {
    if (available)
        *available = false;
    if (outputs.size() != 21
        || canonicalScore < 0.0
        || canonicalScore > 10.0) {
        fail(error, QStringLiteral("canonical_score_invalid"));
        return QJsonValue(QJsonValue::Null);
    }

    const double scaled = canonicalScore * 2.0;
    const int index = qRound(scaled);
    if (qAbs(scaled - index) > 0.0000001
        || index < 0
        || index >= outputs.size()) {
        fail(error, QStringLiteral("canonical_score_not_tick"));
        return QJsonValue(QJsonValue::Null);
    }

    if (error)
        error->clear();
    const QJsonValue value = outputs.at(index);
    if (available)
        *available = !value.isNull();
    return value;
}
