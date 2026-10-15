#pragma once

#include "LegacyPersonalStateStorage.h"
#include "ProfileContext.h"

#include <QObject>
#include <QString>

#include <memory>

class QQmlApplicationEngine;
class QQmlContext;
class QTemporaryDir;
class CollectionStore;
class ProgressStore;
class HistoryStore;
class ProfilePreferencesStore;
class ActivityStore;
class RatingsReviewsStore;
class TrackerConnectionService;

class SearchHistoryStore;
class AudioPairingStore;

class ProfileStoreRuntime final : public QObject {
    Q_OBJECT

public:
    explicit ProfileStoreRuntime(
        QObject *parent = nullptr);

    ProfileStoreRuntime(
        const LegacyPersonalStateStorage &legacyStorage,
        const QString &appDataRoot,
        QObject *parent = nullptr);

    ~ProfileStoreRuntime() override;

    const ProfilePaths &activeProfile() const;
    const LegacyPersonalStateStorage &legacyStorage() const;

    CollectionStore *collectionStore() const;
    ProgressStore *progressStore() const;
    SearchHistoryStore *searchHistoryStore() const;
    AudioPairingStore *audioPairingStore() const;
    HistoryStore *historyStore() const;
    ProfilePreferencesStore *preferencesStore() const;
    ActivityStore *activityStore() const;
    RatingsReviewsStore *ratingsReviewsStore() const;
    TrackerConnectionService *trackerConnectionService() const;

    void prepareForQml(
        QQmlApplicationEngine *engine);

    void flushPersonalStores();
    bool suspendPersonalStoresForMigration(QString *error = nullptr);

    bool activateAccountProfile(
        const QString &accountId,
        QString *error = nullptr);

    bool activateLocalOnlyProfile(
        QString *error = nullptr);

    bool sealAccountProfile(
        const QString &accountId,
        QString *error = nullptr);

    bool reloadLegacyProfile(
        QString *error = nullptr);

signals:
    void storesAboutToChange();
    void storesChanged();
    // Synchronous two-phase boundary: players first snapshot their live
    // position, tracker closes must persist, then Activity ends only on commit.
    void profileDeactivationRequested();
    void profileDeactivationCommitted();

private:
    struct StoreSet;

    std::unique_ptr<StoreSet> createSealedStores(
        QString *error);
    std::unique_ptr<StoreSet> createLegacyStores(QString *error) const;
    std::unique_ptr<StoreSet> createProfileStores(
        const ProfilePaths &paths,
        QString *error) const;

    void bindContextProperties();
    void clearContextProperties();
    void configureRetentionPolicy(StoreSet *stores) const;
    bool prepareTrackerForDeactivation(QString *error);
    static bool setError(
        QString *error,
        const QString &message);

    LegacyPersonalStateStorage m_legacyStorage;
    QString m_appDataRoot;
    ProfileContext m_context;
    std::unique_ptr<QTemporaryDir> m_sealedRoot;
    std::unique_ptr<StoreSet> m_stores;
    QQmlContext *m_qmlContext = nullptr;
};
