#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "RatingsReviewsConversionMap.h"

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class RatingsReviewsConversionSyncAdapter;

class ProfilePreferencesStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        bool showExplicit
        READ showExplicit
        WRITE setShowExplicit
        NOTIFY showExplicitChanged)
    Q_PROPERTY(
        int revision
        READ revision
        NOTIFY changed)
    Q_PROPERTY(bool rememberSearchHistory READ rememberSearchHistory WRITE setRememberSearchHistory NOTIFY rememberSearchHistoryChanged)
    Q_PROPERTY(bool keepActivityHistory READ keepActivityHistory WRITE setKeepActivityHistory NOTIFY keepActivityHistoryChanged)
    Q_PROPERTY(bool syncActivityHistory READ syncActivityHistory WRITE setSyncActivityHistory NOTIFY syncActivityHistoryChanged)

public:
    explicit ProfilePreferencesStore(
        QObject *parent = nullptr);

    explicit ProfilePreferencesStore(
        const QString &iniPath,
        QObject *parent = nullptr);

    explicit ProfilePreferencesStore(
        const RatingsReviewsConversionTestHook &testHook,
        QObject *parent = nullptr);

    ProfilePreferencesStore(
        const QString &iniPath,
        const RatingsReviewsConversionTestHook &testHook,
        QObject *parent = nullptr);

    bool showExplicit() const;
    bool hasShowExplicitValue() const;
    int revision() const;
    bool rememberSearchHistory() const;
    bool keepActivityHistory() const;
    bool syncActivityHistory() const;
    QString mainSyncProvider() const;

    QStringList ratingsReviewsProviderOrder() const;
    bool hasRatingsReviewsProviderOrder() const;
    bool setRatingsReviewsProviderOrder(const QStringList &providerIds);

    QStringList ratingsReviewsDefaultRatingDestinations() const;
    bool hasRatingsReviewsDefaultRatingDestinations() const;
    bool setRatingsReviewsDefaultRatingDestinations(const QStringList &providerIds);

    QStringList ratingsReviewsDefaultReviewDestinations() const;
    bool hasRatingsReviewsDefaultReviewDestinations() const;
    bool setRatingsReviewsDefaultReviewDestinations(const QStringList &providerIds);

    QList<RatingsReviewsConversionMap> ratingsReviewsConversionMaps() const;
    std::optional<RatingsReviewsConversionMap> ratingsReviewsConversionMap(
        const QString &providerId) const;
    bool setRatingsReviewsConversionMap(const RatingsReviewsConversionMap &map);
    bool clearRatingsReviewsConversionMap(const QString &providerId);

    bool applySyncedRatingsReviewsConversionMap(
        const RatingsReviewsConversionMap &map);
    bool clearSyncedRatingsReviewsConversionMap(const QString &providerId);
    bool ratingsReviewsConversionMapsHealthy(QString *error = nullptr) const;

    void setShowExplicit(bool showExplicit);
    Q_INVOKABLE void setRememberSearchHistory(bool enabled);
    Q_INVOKABLE void setKeepActivityHistory(bool enabled);
    Q_INVOKABLE void setSyncActivityHistory(bool enabled);
    bool setMainSyncProvider(const QString &provider);

    // Native remote-apply seam. Persists and notifies the shell, but does not
    // manufacture a new local sync mutation.
    bool applySyncedShowExplicit(bool showExplicit);
    bool clearSyncedShowExplicit();
    bool applySyncedMainSyncProvider(const QString &provider);
    bool clearSyncedMainSyncProvider();

signals:
    void showExplicitChanged();
    void rememberSearchHistoryChanged();
    void keepActivityHistoryChanged();
    void syncActivityHistoryChanged();
    void mainSyncProviderChanged();
    void ratingsReviewsPrivatePreferencesChanged();
    void ratingsReviewsConversionMapsChanged();
    void ratingsReviewsConversionSyncDirty();
    void changed();
    void syncDirty();
    void stremioLinkDirty();

private:
    friend class RatingsReviewsConversionSyncAdapter;

    bool commitShowExplicit(bool showExplicit, bool localMutation);
    bool commitMainSyncProvider(const QString &provider, bool localMutation);
    bool commitRatingsReviewsConversionMap(
        const RatingsReviewsConversionMap &map,
        bool localMutation);
    bool clearRatingsReviewsConversionMapInternal(
        const QString &providerId,
        bool localMutation);

    bool persistStringList(
        const QString &key,
        const QStringList &values);
    bool persistConversionMap(const RatingsReviewsConversionMap &map);
    bool removeConversionMap(const QString &providerId);

    static bool isExactCanonicalProviderList(const QStringList &providerIds);
    static QStringList normalizedProviderOrder(const QStringList &providerIds);
    bool isValidDestinationList(const QStringList &providerIds) const;

    void load();
    void loadRatingsReviewsPreferences();

    std::unique_ptr<QSettings> m_settings;
    RatingsReviewsConversionTestHook m_conversionTestHook;

    bool m_showExplicit = false;
    bool m_hasShowExplicitValue = false;
    bool m_rememberSearchHistory = true;
    bool m_keepActivityHistory = true;
    bool m_syncActivityHistory = true;
    QString m_mainSyncProvider;

    QStringList m_ratingsReviewsProviderOrder;
    bool m_hasRatingsReviewsProviderOrder = false;
    QStringList m_ratingsReviewsDefaultRatingDestinations;
    bool m_hasRatingsReviewsDefaultRatingDestinations = false;
    QStringList m_ratingsReviewsDefaultReviewDestinations;
    bool m_hasRatingsReviewsDefaultReviewDestinations = false;
    QHash<QString, RatingsReviewsConversionMap> m_ratingsReviewsConversionMaps;
    bool m_ratingsReviewsConversionMapsHealthy = true;
    QString m_ratingsReviewsConversionMapsError;

    int m_revision = 0;
};
