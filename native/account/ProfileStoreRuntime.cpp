// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfileStoreRuntime.h"

#include "ActivityStore.h"
#include "HistoryStore.h"
#include "ProfilePreferencesStore.h"

#include "AudioPairingStore.h"
#include "CollectionStore.h"
#include "ProgressStore.h"
#include "SearchHistoryStore.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>

struct ProfileStoreRuntime::StoreSet {
    std::unique_ptr<ProgressStore> progress;
    std::unique_ptr<CollectionStore> collection;
    std::unique_ptr<SearchHistoryStore> searchHistory;
    std::unique_ptr<AudioPairingStore> audioPairing;
    std::unique_ptr<ProfilePreferencesStore> preferences;
    std::unique_ptr<HistoryStore> history;
    // ActivityStore joins the StoreSet for every profile mode (sealed,
    // legacy-local, explicit local, account — CPP-PORT-CONTRACT §2/§17).
    // Declared last so it is destroyed FIRST (member dtors run in reverse
    // declaration order): its SQL connection closes cleanly before any
    // sibling store or the surrounding directory (m_sealedRoot) is torn
    // down. Construction never fails/throws (ActivityStore's own contract),
    // so an unhealthy activity DB never blocks profile bring-up — activity
    // is observational, per CPP-PORT-CONTRACT §25.
    std::unique_ptr<ActivityStore> activity;
};

ProfileStoreRuntime::ProfileStoreRuntime(
    QObject *parent)
    : ProfileStoreRuntime(
          LegacyPersonalStateStorage::forCurrentInstallation(),
          QString(),
          parent) {}

ProfileStoreRuntime::ProfileStoreRuntime(
    const LegacyPersonalStateStorage &legacyStorage,
    const QString &appDataRoot,
    QObject *parent)
    : QObject(parent),
      m_legacyStorage(legacyStorage),
      m_appDataRoot(appDataRoot) {
    setObjectName(QStringLiteral("profileStoreRuntime"));
    m_context.activateSealed(m_appDataRoot);
    m_stores = createSealedStores(nullptr);
}

ProfileStoreRuntime::~ProfileStoreRuntime() {
    flushPersonalStores();
}

const ProfilePaths &ProfileStoreRuntime::activeProfile() const {
    return m_context.activeProfile();
}

const LegacyPersonalStateStorage &
ProfileStoreRuntime::legacyStorage() const {
    return m_legacyStorage;
}


CollectionStore *
ProfileStoreRuntime::collectionStore() const {
    return m_stores
        ? m_stores->collection.get()
        : nullptr;
}

SearchHistoryStore *
ProfileStoreRuntime::searchHistoryStore() const {
    return m_stores
        ? m_stores->searchHistory.get()
        : nullptr;
}

AudioPairingStore *
ProfileStoreRuntime::audioPairingStore() const {
    return m_stores
        ? m_stores->audioPairing.get()
        : nullptr;
}

ProgressStore *
ProfileStoreRuntime::progressStore() const {
    return m_stores
        ? m_stores->progress.get()
        : nullptr;
}

HistoryStore *
ProfileStoreRuntime::historyStore() const {
    return m_stores
        ? m_stores->history.get()
        : nullptr;
}

ProfilePreferencesStore *
ProfileStoreRuntime::preferencesStore() const {
    return m_stores
        ? m_stores->preferences.get()
        : nullptr;
}

ActivityStore *
ProfileStoreRuntime::activityStore() const {
    return m_stores
        ? m_stores->activity.get()
        : nullptr;
}

void ProfileStoreRuntime::prepareForQml(
    QQmlApplicationEngine *engine) {
    Q_ASSERT(engine);
    if (!engine)
        return;

    QQmlContext *context =
        engine->rootContext();
    if (!context)
        return;

    m_qmlContext = context;
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfileContext"),
        &m_context);

    if (!m_stores
        && m_context.activeProfile().kind()
            == ProfilePaths::Kind::Sealed) {
        m_stores = createSealedStores(nullptr);
    }
    bindContextProperties();
}

void ProfileStoreRuntime::flushPersonalStores() {
    if (m_stores && m_stores->progress)
        m_stores->progress->flush();
    // Best-effort WAL merge, not required for correctness (every activity
    // fact already commits transactionally on insert) — just keeps the
    // on-disk .sqlite file current for anything that reads it directly
    // (adoption's file-safe copy). A failure here is silently ignored:
    // activity is observational and must never block store bring-up/flush.
    if (m_stores && m_stores->activity)
        m_stores->activity->checkpointForSafeCopy(nullptr);
}

void ProfileStoreRuntime::suspendPersonalStoresForMigration() {
    if (!m_stores)
        return;

    flushPersonalStores();
    emit storesAboutToChange();
    clearContextProperties();
    m_stores.reset();
    m_sealedRoot.reset();
    emit storesChanged();
}

bool ProfileStoreRuntime::activateAccountProfile(
    const QString &accountId,
    QString *error) {
    const auto paths =
        ProfilePaths::account(
            accountId,
            m_appDataRoot);
    if (!paths.has_value()) {
        return setError(
            error,
            QStringLiteral(
                "The account profile identifier is invalid."));
    }

    const ProfilePaths &current =
        m_context.activeProfile();

    if (current.kind() == ProfilePaths::Kind::Account) {
        if (current.profileId()
            == paths->profileId()) {
            return true;
        }

        return setError(
            error,
            QStringLiteral(
                "The active account profile must be sealed before another account opens."));
    }

    if (!QFileInfo::exists(paths->profileRoot())) {
        return setError(
            error,
            QStringLiteral(
                "The account profile has not been prepared."));
    }

    std::unique_ptr<StoreSet> next =
        createProfileStores(*paths, error);
    if (!next)
        return false;

    flushPersonalStores();
    emit storesAboutToChange();

    std::unique_ptr<StoreSet> previous =
        std::move(m_stores);
    m_stores = std::move(next);
    bindContextProperties();
    previous.reset();
    m_sealedRoot.reset();

    if (!m_context.activateAccount(
            accountId,
            m_appDataRoot)) {
        return setError(
            error,
            QStringLiteral(
                "The account profile could not be activated."));
    }

    emit storesChanged();
    return true;
}

bool ProfileStoreRuntime::activateLocalOnlyProfile(
    QString *error) {
    const ProfilePaths paths =
        ProfilePaths::localOnly(
            m_appDataRoot);

    if (!QDir().mkpath(
            paths.profileRoot())) {
        return setError(
            error,
            QStringLiteral(
                "The local-only profile directory could not be created."));
    }

    std::unique_ptr<StoreSet> next =
        createProfileStores(
            paths,
            error);
    if (!next)
        return false;

    flushPersonalStores();
    emit storesAboutToChange();

    std::unique_ptr<StoreSet> previous =
        std::move(m_stores);
    m_stores = std::move(next);
    bindContextProperties();
    previous.reset();
    m_sealedRoot.reset();
    m_context.activateLocalOnly(
        m_appDataRoot);
    emit storesChanged();
    return true;
}

bool ProfileStoreRuntime::sealAccountProfile(
    const QString &accountId,
    QString *error) {
    const auto expected =
        ProfilePaths::account(
            accountId,
            m_appDataRoot);
    if (!expected.has_value()) {
        return setError(
            error,
            QStringLiteral(
                "The account profile identifier is invalid."));
    }

    const ProfilePaths &current =
        m_context.activeProfile();

    if (current.kind() == ProfilePaths::Kind::Sealed)
        return true;

    if (current.kind() != ProfilePaths::Kind::Account
        || current.profileId()
            != expected->profileId()) {
        return setError(
            error,
            QStringLiteral(
                "The active personal profile does not match the account being sealed."));
    }

    std::unique_ptr<StoreSet> sealed =
        createSealedStores(error);
    if (!sealed)
        return false;

    flushPersonalStores();
    emit storesAboutToChange();

    std::unique_ptr<StoreSet> previous =
        std::move(m_stores);
    m_stores = std::move(sealed);
    bindContextProperties();
    previous.reset();

    m_context.activateSealed(
        m_appDataRoot);
    emit storesChanged();
    return true;
}

bool ProfileStoreRuntime::reloadLegacyProfile(
    QString *error) {
    if (m_stores) {
        return setError(
            error,
            QStringLiteral(
                "Personal store owners must be suspended before legacy state is reloaded."));
    }

    std::unique_ptr<StoreSet> next =
        createLegacyStores();
    if (!next) {
        return setError(
            error,
            QStringLiteral(
                "The restored local personal stores could not be reopened."));
    }

    emit storesAboutToChange();
    m_stores = std::move(next);
    bindContextProperties();
    m_sealedRoot.reset();
    m_context.activateLegacyLocal();
    emit storesChanged();
    return true;
}

std::unique_ptr<ProfileStoreRuntime::StoreSet>
ProfileStoreRuntime::createSealedStores(
    QString *error) {
    const QString appDataRoot =
        ProfilePaths::sealed(
            m_appDataRoot)
            .appDataRoot();
    const QString sessionRoot =
        QDir(appDataRoot).filePath(
            QStringLiteral("profile-session"));

    if (!QDir().mkpath(sessionRoot)) {
        setError(
            error,
            QStringLiteral(
                "The sealed profile session directory could not be created."));
        return {};
    }

    auto sealedRoot =
        std::make_unique<QTemporaryDir>(
            QDir(sessionRoot).filePath(
                QStringLiteral("sealed-XXXXXX")));
    if (!sealedRoot->isValid()) {
        setError(
            error,
            QStringLiteral(
                "The sealed profile placeholder could not be created."));
        return {};
    }

    const QString root =
        sealedRoot->path();

    auto stores =
        std::make_unique<StoreSet>();
    stores->progress =
        std::make_unique<ProgressStore>(
            QDir(root).filePath(
                QStringLiteral("progress.ini")));
    stores->collection =
        std::make_unique<CollectionStore>(
            QDir(root).filePath(
                QStringLiteral("collection.ini")));
    stores->searchHistory =
        std::make_unique<SearchHistoryStore>(
            QDir(root).filePath(
                QStringLiteral("search-history.ini")));
    stores->audioPairing =
        std::make_unique<AudioPairingStore>(
            QDir(root).filePath(
                QStringLiteral("audio-pairing.ini")));
    stores->preferences =
        std::make_unique<ProfilePreferencesStore>(
            QDir(root).filePath(
                QStringLiteral("preferences.ini")));
    stores->history =
        std::make_unique<HistoryStore>(
            QDir(root).filePath(
                QStringLiteral("history.ini")));
    stores->activity =
        std::make_unique<ActivityStore>(
            QDir(root).filePath(
                QStringLiteral("activity.sqlite")));

    m_sealedRoot =
        std::move(sealedRoot);
    return stores;
}

std::unique_ptr<ProfileStoreRuntime::StoreSet>
ProfileStoreRuntime::createLegacyStores() const {
    auto stores =
        std::make_unique<StoreSet>();

    stores->progress =
        m_legacyStorage.progressUsesExplicitIni()
        ? std::make_unique<ProgressStore>(
              m_legacyStorage.progressIniPath())
        : std::make_unique<ProgressStore>();

    stores->collection =
        m_legacyStorage.collectionUsesExplicitIni()
        ? std::make_unique<CollectionStore>(
              m_legacyStorage.collectionIniPath())
        : std::make_unique<CollectionStore>();

    stores->searchHistory =
        m_legacyStorage.searchHistoryUsesExplicitIni()
        ? std::make_unique<SearchHistoryStore>(
              m_legacyStorage.searchHistoryIniPath())
        : std::make_unique<SearchHistoryStore>();

    stores->audioPairing =
        m_legacyStorage.audioPairingUsesExplicitIni()
        ? std::make_unique<AudioPairingStore>(
              m_legacyStorage.audioPairingIniPath())
        : std::make_unique<AudioPairingStore>();

    stores->preferences =
        m_legacyStorage.preferencesUseExplicitIni()
        ? std::make_unique<ProfilePreferencesStore>(
              m_legacyStorage.preferencesIniPath())
        : std::make_unique<ProfilePreferencesStore>();

    stores->history =
        m_legacyStorage.historyUsesExplicitIni()
        ? std::make_unique<HistoryStore>(
              m_legacyStorage.historyIniPath())
        : std::make_unique<HistoryStore>();

    // activity.sqlite has no QSettings-registry backend to fall back to —
    // LegacyPersonalStateStorage always resolves an explicit durable path for
    // it (CPP-PORT-CONTRACT §17 "Legacy-local mode").
    stores->activity =
        std::make_unique<ActivityStore>(
            m_legacyStorage.activityDbPath());

    return stores;
}

std::unique_ptr<ProfileStoreRuntime::StoreSet>
ProfileStoreRuntime::createProfileStores(
    const ProfilePaths &paths,
    QString *error) const {
    if (paths.kind()
        == ProfilePaths::Kind::Sealed
        || paths.kind()
            == ProfilePaths::Kind::LegacyLocal) {
        setError(
            error,
            QStringLiteral(
                "Sealed and legacy-local persistence are not explicit personal profiles."));
        return {};
    }

    if (!QDir().mkpath(paths.profileRoot())) {
        setError(
            error,
            QStringLiteral(
                "The personal profile directory could not be opened."));
        return {};
    }

    auto stores =
        std::make_unique<StoreSet>();

    stores->progress =
        std::make_unique<ProgressStore>(
            paths.progressIniPath());
    stores->collection =
        std::make_unique<CollectionStore>(
            paths.collectionIniPath());
    stores->searchHistory =
        std::make_unique<SearchHistoryStore>(
            paths.searchHistoryIniPath());
    stores->audioPairing =
        std::make_unique<AudioPairingStore>(
            paths.audioPairingIniPath());
    stores->preferences =
        std::make_unique<ProfilePreferencesStore>(
            paths.preferencesIniPath());
    stores->history =
        std::make_unique<HistoryStore>(
            paths.historyIniPath());
    stores->activity =
        std::make_unique<ActivityStore>(
            paths.activityDbPath());

    return stores;
}

void ProfileStoreRuntime::bindContextProperties() {
    if (!m_qmlContext || !m_stores)
        return;

    m_qmlContext->setContextProperty(
        QStringLiteral("Progress"),
        m_stores->progress.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("Collection"),
        m_stores->collection.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("SearchHistory"),
        m_stores->searchHistory.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("AudioPairing"),
        m_stores->audioPairing.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfilePreferences"),
        m_stores->preferences.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfileHistory"),
        m_stores->history.get());
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfileActivity"),
        m_stores->activity.get());
}

void ProfileStoreRuntime::clearContextProperties() {
    if (!m_qmlContext)
        return;

    m_qmlContext->setContextProperty(
        QStringLiteral("Progress"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("Collection"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("SearchHistory"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("AudioPairing"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfilePreferences"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfileHistory"),
        static_cast<QObject *>(nullptr));
    m_qmlContext->setContextProperty(
        QStringLiteral("ProfileActivity"),
        static_cast<QObject *>(nullptr));
}

bool ProfileStoreRuntime::setError(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
