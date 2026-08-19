#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

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

    void prepareForQml(
        QQmlApplicationEngine *engine);

    void flushPersonalStores();
    void suspendPersonalStoresForMigration();

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

private:
    struct StoreSet;

    std::unique_ptr<StoreSet> createSealedStores(
        QString *error);
    std::unique_ptr<StoreSet> createLegacyStores() const;
    std::unique_ptr<StoreSet> createProfileStores(
        const ProfilePaths &paths,
        QString *error) const;

    void bindContextProperties();
    void clearContextProperties();
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
