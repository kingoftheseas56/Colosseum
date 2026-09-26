#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class RatingsReviewsConversionTestHook final {
public:
    using SettingsFailureGate = std::shared_ptr<bool>;

    RatingsReviewsConversionTestHook() = default;

    static SettingsFailureGate settingsFailureGate();
    static RatingsReviewsConversionTestHook syntheticDomains(
        const SettingsFailureGate &settingsFailureGate = {});

    bool syntheticDomainsEnabledForTests() const;
    bool settingsFailureRequestedForTests() const;

private:
    explicit RatingsReviewsConversionTestHook(
        bool syntheticDomains,
        const SettingsFailureGate &settingsFailureGate);

    bool m_syntheticDomains = false;
    SettingsFailureGate m_settingsFailureGate;
};

struct RatingsReviewsConversionMap final {
    int version = 1;
    QString providerId;
    QString domainId;
    int domainVersion = 0;
    QList<QJsonValue> outputs;

    QJsonObject toJson() const;
    QString digest() const;

    QJsonValue translatedOutput(
        double canonicalScore,
        bool *available = nullptr,
        QString *error = nullptr) const;

    static QStringList canonicalProviderIds();
    static bool isCanonicalProviderId(const QString &providerId);

    static bool validate(
        const RatingsReviewsConversionMap &map,
        const RatingsReviewsConversionTestHook &testHook = {},
        QString *error = nullptr);

    static std::optional<RatingsReviewsConversionMap> fromJson(
        const QJsonObject &object,
        const RatingsReviewsConversionTestHook &testHook = {},
        QString *error = nullptr);

    static std::optional<RatingsReviewsConversionMap> recommended(
        const QString &providerId,
        const QString &domainId,
        int domainVersion,
        const RatingsReviewsConversionTestHook &testHook = {},
        QString *error = nullptr);
};
