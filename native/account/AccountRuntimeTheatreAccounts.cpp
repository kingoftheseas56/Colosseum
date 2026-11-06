#include "AccountRuntime.h"

#include "CollectionStore.h"
#include "HistoryStore.h"
#include "ProfilePreferencesStore.h"
#include "ProgressStore.h"
#include "engine/ExtensionsStore.h"

#include <utility>

namespace {
using Category = TheatreAccountImport::Category;
using Disposition = TheatreAccountImport::Disposition;
using Item = TheatreAccountImport::Item;
const auto kIdle = QStringLiteral("idle");
const auto kLoading = QStringLiteral("loading");
const auto kPreview = QStringLiteral("preview");
const auto kApplying = QStringLiteral("applying");
const auto kDone = QStringLiteral("done");
const auto kStremio = QStringLiteral("stremio");
const auto kNuvio = QStringLiteral("nuvio");

Item completedStremioEpisode(const QVariantMap &progress) {
    const QString id = progress.value("id").toString();
    const QString title = progress.value("title").toString().trimmed();
    const qint64 at = progress.value("updatedAt").toLongLong();
    return {Category::Episode, {{"kind", QStringLiteral("episode")}, {"id", id},
        {"firstActivityAt", at}, {"lastActivityAt", at}, {"completedAt", at},
        {"source", kStremio}, {"displayId", id}, {"displayTitle", title.isEmpty() ? id : title},
        {"latestKnownAt", at}}};
}
}

struct AccountRuntime::StremioPreview {
    bool primary = false;
    bool firstRun = false;
    bool waitingForAuth = false;
    bool primaryCommitted = false;
    QList<StremioLibraryItem> remote;
    QJsonArray addons;
    QList<Item> plan;
    QList<Item> positions;
    qsizetype next = 0;
};

void AccountRuntime::activateNuvioProfile() {
    const ProfilePaths profile = m_profileStores.activeProfile();
    const bool usable = profile.kind() != ProfilePaths::Kind::Sealed;
    const QString profileRoot = profile.kind() == ProfilePaths::Kind::LegacyLocal
        ? m_profileStores.legacyStorage().devicePrivateProfileRoot() : profile.profileRoot();
    const QString cursors = profileRoot.isEmpty()
        ? QString() : profileRoot + QStringLiteral("/nuvio/cursors-v1.json");
    m_nuvioAccount.activate(usable ? profile.profileId() : QString(),
        m_profileStores.collectionStore(), m_profileStores.progressStore(),
        m_profileStores.historyStore(), m_profileStores.preferencesStore(), cursors);
    m_stremioPreviewImporter.bind(m_profileStores.collectionStore(), m_profileStores.progressStore(),
        m_profileStores.historyStore(), m_extensionsStore);
}

void AccountRuntime::resetTheatreAccountFlows() {
    m_stremioPrimaryActive = false;
    m_stremioPreview.reset();
    m_stremioPreviewImporter.bind(nullptr, nullptr, nullptr, nullptr);
    m_nuvioAccount.activate({}, nullptr, nullptr, nullptr, nullptr);
    m_stremioImportPreview.clear();
    setStremioImportStatus(kIdle);
}

void AccountRuntime::applyStremioProviderMode() {
    const auto *preferences = m_profileStores.preferencesStore();
    const QString primary = preferences ? preferences->mainSyncProvider() : QString();
    const bool isPrimary = primary == kStremio;
    const bool wasPrimary = std::exchange(m_stremioPrimaryActive, isPrimary);
    if (isPrimary) {
        m_stremioSync.setImportOnlyEnabled(false);
        m_stremioSync.setMarkerLinked(true);
        if (!wasPrimary && m_stremioTheatreImporter) {
            // Replay provider work only while the provider owns progress.
            m_stremioTheatreImporter->activate(m_profileStores.activeProfile().profileId());
            replayPendingStremioProviderImports();
        }
    } else {
        m_stremioSync.setImportOnlyEnabled(primary == kNuvio);
        m_stremioSync.setMarkerLinked(false);
        if (wasPrimary) {
            ++m_stremioProfileIncarnation;
            ++m_visibleSyncGeneration;
            m_stremioActiveImportCount = 0;
            m_stremioReconcileScheduled = false;
            m_stremioReconcileDeferred = false;
            m_stremioAddonReconcileScheduled = false;
            m_stremioAddonApplyInProgress = false;
            m_stremioAddonReconcileDeferred = false;
            if (m_stremioTheatreImporter)
                m_stremioTheatreImporter->deactivate();
            if (m_stremioPreview && m_stremioPreview->primaryCommitted) {
                m_stremioPreview.reset();
                m_stremioPreviewImporter.cancel();
                m_stremioImportPreview.clear();
                setStremioImportStatus(kIdle, tr("The primary sync service changed."));
            }
        }
    }
}

bool AccountRuntime::stremioImportBecomesPrimary() const {
    return m_stremioPreview && m_stremioPreview->primary;
}

bool AccountRuntime::previewStremioImport() {
    return startStremioPreview(false, false);
}

bool AccountRuntime::makeStremioPrimary() {
    return startStremioPreview(true, false);
}

bool AccountRuntime::beginStremioFirstRun() {
    const auto kind = m_profileStores.activeProfile().kind();
    if (!m_controller.onboardingRequired() || !m_controller.accountFeaturesAvailable()
        || (kind != ProfilePaths::Kind::LegacyLocal && kind != ProfilePaths::Kind::LocalOnly)) {
        return false;
    }
    return startStremioPreview(true, true);
}

bool AccountRuntime::startStremioPreview(bool primary, bool firstRun) {
    const auto *preferences = m_profileStores.preferencesStore();
    if (!preferences || !m_profileStores.collectionStore() || !m_profileStores.progressStore()
        || (m_stremioImportPhase != kIdle && m_stremioImportPhase != kDone)
        || (!firstRun && (preferences->mainSyncProvider() != kNuvio
            || !m_stremioSync.linkedAccount() || m_stremioSync.status() != QLatin1String("synced")))) {
        return false;
    }
    auto preview = std::make_shared<StremioPreview>();
    preview->primary = primary;
    preview->firstRun = firstRun;
    m_stremioPreview = preview;
    m_stremioImportPreview.clear();
    setStremioImportStatus(kLoading, firstRun
        ? tr("Finish signing in to Stremio in the browser.") : tr("Bringing in your library…"));
    if (!firstRun) {
        pullStremioPreview(preview);
        return true;
    }

    QString error;
    if (!m_stremioSync.startStagedAuthentication(&error)) {
        m_stremioPreview.reset();
        setStremioImportStatus(kIdle, tr("Stremio sign-in could not start. Try again."));
        return false;
    }
    preview->waitingForAuth = true;
    onStremioPreviewAuthChanged();
    return true;
}

void AccountRuntime::onStremioPreviewAuthChanged() {
    const auto preview = m_stremioPreview;
    if (!preview || !preview->waitingForAuth || m_stremioImportPhase != kLoading)
        return;
    if (m_stremioSync.stagedAuthenticationReady()) {
        preview->waitingForAuth = false;
        setStremioImportStatus(kLoading, tr("Bringing in your library…"));
        pullStremioPreview(preview);
    } else if (m_stremioSync.status() != QLatin1String("connecting")) {
        m_stremioPreview.reset();
        setStremioImportStatus(kIdle, tr("Stremio sign-in did not finish. Try again."));
    }
}

void AccountRuntime::pullStremioPreview(const std::shared_ptr<StremioPreview> &preview) {
    m_stremioSync.pullLibraryItemsForPreview([this, preview](bool ok, QList<StremioLibraryItem> items) {
        if (preview != m_stremioPreview)
            return;
        if (!ok) {
            cancelStremioImport();
            setStremioImportStatus(kIdle, tr("Stremio's library could not be read. Try again."));
            return;
        }
        preview->remote = std::move(items);
        m_stremioSync.pullAddonCollectionForPreview([this, preview](bool read, QJsonArray addons) {
            if (preview != m_stremioPreview)
                return;
            if (!read) {
                cancelStremioImport();
                setStremioImportStatus(kIdle, tr("Stremio's addons could not be read. Try again."));
                return;
            }
            preview->addons = std::move(addons);
            projectNextStremioPreviewItem(preview);
        });
    });
}

void AccountRuntime::projectNextStremioPreviewItem(const std::shared_ptr<StremioPreview> &preview) {
    if (preview != m_stremioPreview)
        return;
    while (preview->next < preview->remote.size()) {
        const StremioLibraryItem &item = preview->remote.at(preview->next++);
        const auto projected = StremioCodec::projectTheatreItem(item);
        if (!projected.valid) {
            preview->plan.append({Category::Library, {}, Disposition::Skipped});
            continue;
        }
        if (projected.hasCollection)
            preview->plan.append({Category::Library, projected.collection});
        if (preview->primary && projected.hasProgress)
            preview->positions.append({Category::Progress, projected.progress});
        if (projected.hasHistory) {
            preview->plan.append({Category::Movie, projected.history});
        } else if (projected.hasWatchState && projected.watched) {
            preview->plan.append({Category::Movie, {}, Disposition::Skipped});
        }
        if (item.type != QLatin1String("series"))
            continue;
        const QString bits = item.raw.value("state").toObject().value("watched").toString();
        if (bits.isEmpty())
            continue;
        // A Stremio series bitfield is indexed by exact provider metadata.
        // It does not date each bit. Only a completed, dated checkpoint for
        // the exact same video can be recorded as a dated episode fact.
        m_stremioSync.requestEpisodeMetadataForPreview(item.id,
            [this, preview, bits, progress = projected.progress](bool resolved,
                const QList<StremioEpisodeIdentity> &videos) {
                if (preview != m_stremioPreview)
                    return;
                QSet<QString> watched;
                if (!resolved || !StremioCodec::decodeWatchedEpisodes(bits, videos, &watched)) {
                    preview->plan.append({Category::Episode, {}, Disposition::Unresolved});
                } else {
                    for (const QString &id : std::as_const(watched)) {
                        if (id == progress.value("id").toString() && progress.value("updatedAt").toLongLong() > 0
                            && progress.value("progress").toDouble() >= 0.90) {
                            preview->plan.append(completedStremioEpisode(progress));
                        } else {
                            preview->plan.append({Category::Episode, {}, Disposition::Skipped});
                        }
                    }
                }
                QTimer::singleShot(0, this, [this, preview] { projectNextStremioPreviewItem(preview); });
            });
        return;
    }
    for (const QJsonValue &value : std::as_const(preview->addons)) {
        const QJsonObject addon = value.toObject();
        preview->plan.append({Category::Addon, {{"url", addon.value("transportUrl").toString()},
            {"enabled", addon.value("enabled").toBool(true)}}});
    }
    preview->plan.append(preview->positions);
    preview->remote.clear();
    preview->positions.clear();
    preview->addons = QJsonArray();
    m_stremioImportPreview = m_stremioPreviewImporter.preview(preview->plan);
    setStremioImportStatus(kPreview);
}

void AccountRuntime::applyStremioImport(bool firstRun) {
    const auto preview = m_stremioPreview;
    if (!preview || m_stremioImportPhase != kPreview || preview->firstRun != firstRun)
        return;
    setStremioImportStatus(kApplying, tr("Bringing in your library…"));
    if (firstRun && m_stremioSync.stagedAuthenticationReady()) {
        m_stremioSync.confirmStagedAuthentication([this, preview](bool committed) {
            if (preview != m_stremioPreview)
                return;
            if (!committed) {
                m_stremioPreview.reset();
                m_stremioImportPreview.clear();
                setStremioImportStatus(kIdle, tr("Stremio's connection could not be saved securely."));
                return;
            }
            commitStremioPreview(preview);
        });
        return;
    }
    commitStremioPreview(preview);
}

void AccountRuntime::commitStremioPreview(const std::shared_ptr<StremioPreview> &preview) {
    auto *preferences = m_profileStores.preferencesStore();
    const QString primary = preferences ? preferences->mainSyncProvider() : QString();
    if (preview->primary && primary == kNuvio && !m_stremioSync.importOnlyPromotionReady()) {
        // Save the new primary only after the old deletion intents have been
        // retired on disk. A restart must not recover them as writable work.
        setStremioImportStatus(kPreview, tr("Stremio is still saving the previous switch. Try Apply again."));
        return;
    }
    if (!preferences || !m_stremioSync.linkedAccount()
        || (!preview->primary && primary != kNuvio)
        || (preview->primary && !preferences->setMainSyncProvider(kStremio))) {
        setStremioImportStatus(kPreview, tr("The selected Stremio connection could not be applied."));
        return;
    }
    if (preview != m_stremioPreview)
        return;
    preview->primaryCommitted = preview->primary;
    const bool started = m_stremioPreviewImporter.apply(preview->plan,
        [this, preview](TheatreAccountImport::Result result) {
            if (preview != m_stremioPreview)
                return;
            if (!result.committed) {
                setStremioImportStatus(kPreview, tr("Import could not finish. You can retry the remaining items."));
                return;
            }
            checkpointTheatreImport([this, preview, result](bool committed) {
                if (preview != m_stremioPreview)
                    return;
                if (!committed) {
                    setStremioImportStatus(kPreview, tr("Imported data was saved locally. Account sync needs another attempt."));
                    return;
                }
                const bool wasPrimary = preview->primary;
                m_stremioPreview.reset();
                setStremioImportStatus(kDone, result.addonFailures == 0
                    ? tr("%n change(s) saved to Colosseum.", nullptr, result.changed)
                    : tr("%1 changes saved. %2 addons could not be installed.")
                        .arg(result.changed).arg(result.addonFailures));
                if (wasPrimary) {
                    const quint64 incarnation = m_stremioProfileIncarnation;
                    QTimer::singleShot(0, this, [this, incarnation] {
                        if (incarnation == m_stremioProfileIncarnation)
                            runStremioSync(false);
                    });
                } else {
                    m_nuvioAccount.scheduleSync();
                }
            });
        });
    if (!started)
        setStremioImportStatus(kPreview, tr("The active Theatre stores could not start the import."));
}

void AccountRuntime::cancelStremioImport() {
    if (m_stremioImportPhase == kApplying)
        return;
    const auto preview = std::exchange(m_stremioPreview, {});
    if (preview && preview->firstRun)
        m_stremioSync.cancelAuthentication();
    m_stremioImportPreview.clear();
    setStremioImportStatus(kIdle);
}

void AccountRuntime::setStremioImportStatus(const QString &phase, const QString &message) {
    m_stremioImportPhase = phase;
    m_stremioImportMessage = message;
    emit stremioImportChanged();
}

void AccountRuntime::checkpointTheatreImport(std::function<void(bool)> completion) {
    if (!m_syncEngine.active()) {
        completion(true);
        return;
    }
    auto pending = std::make_shared<std::function<void(bool)>>(std::move(completion));
    const auto settle = [pending](bool ok) {
        if (auto done = std::exchange(*pending, {}))
            done(ok);
    };
    if (!m_syncEngine.checkpointProviderImport([settle](bool committed, const QString &) {
            settle(committed);
        })) {
        settle(false);
    }
}
