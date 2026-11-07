#include "NuvioAccount.h"

#include "CollectionStore.h"
#include "HistoryStore.h"
#include "ProfilePreferencesStore.h"
#include "ProgressStore.h"
#include "engine/ExtensionsStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
const auto kIdle = QStringLiteral("idle");
const auto kSignIn = QStringLiteral("signIn");
const auto kProfiles = QStringLiteral("profiles");
const auto kPreview = QStringLiteral("preview");
const auto kDone = QStringLiteral("done");
const auto kNuvio = QStringLiteral("nuvio");
const auto kTheatre = QStringLiteral("theatre");
constexpr int kPageSize = 500;
constexpr int kProgressLimit = 200;
constexpr int kMaxRows = 20000;
constexpr int kMaxAddons = 64;
constexpr int kWriteBatch = 100;
constexpr qsizetype kMaxRemovals = 4096;
constexpr qint64 kRemovalRetentionMs = 30LL * 24 * 60 * 60 * 1000;
using RemovalKind = NuvioTheatre::RemovalKind;
using Removal = NuvioTheatre::Removal;
constexpr int kLibrary = 0;
constexpr int kWatched = 1;
constexpr int kProgress = 2;

constexpr std::array<const char *, 3> snapshotCalls{
    "sync_pull_library", "sync_pull_watched_items", "sync_pull_watch_progress"};
constexpr std::array<const char *, 3> cursorCalls{
    "sync_get_library_delta_cursor", "sync_get_watched_items_delta_cursor",
    "sync_get_watch_progress_delta_cursor"};
constexpr std::array<const char *, 3> deltaCalls{
    "sync_pull_library_delta", "sync_pull_watched_items_delta", "sync_pull_watch_progress_delta"};

const NuvioTheatre::Profile *selectedProfile(const QList<NuvioTheatre::Profile> &profiles,
    int index, const QString &id = {}) {
    const auto found = std::find_if(profiles.cbegin(), profiles.cend(), [&](const auto &profile) {
        return profile.index == index && (id.isEmpty() || profile.id == id);
    });
    return found == profiles.cend() ? nullptr : &*found;
}

QJsonArray &sectionRows(NuvioTheatre::Snapshot &snapshot, int section) {
    if (section == kLibrary)
        return snapshot.library;
    if (section == kWatched)
        return snapshot.watched;
    return snapshot.progress;
}

QString rowKey(const QJsonObject &row, int section) {
    const QString content = row.value("content_id").toString();
    const QString type = row.value("content_type").toString();
    if (content.isEmpty() || content.size() > 512 || content.contains(QChar::Null)
        || (type != QLatin1String("movie") && type != QLatin1String("series"))) {
        return {};
    }
    const QString key = type + QChar::Null + content;
    if (section == kLibrary || type == QLatin1String("movie"))
        return key;
    const int season = row.value("season").toInt(-1);
    const int episode = row.value("episode").toInt(-1);
    return season < 0 || episode < 0 ? QString()
        : key + QChar::Null + QString::number(season) + QLatin1Char(':') + QString::number(episode);
}

QString seriesRoot(const QString &id) {
    const qsizetype episodeSeparator = id.lastIndexOf(QLatin1Char(':'));
    const qsizetype seasonSeparator = id.lastIndexOf(QLatin1Char(':'), episodeSeparator - 1);
    return seasonSeparator > 0 && episodeSeparator > seasonSeparator
        ? id.left(seasonSeparator) : id;
}

QJsonObject localFact(RemovalKind kind, const QVariantMap &record) {
    const QString id = record.value("id").toString();
    const QString kindName = record.value("kind").toString();
    const QString root = seriesRoot(id);
    const QString type = kind == RemovalKind::Library ? record.value("type").toString()
        : (id != root || kindName == QLatin1String("episode")) ? QStringLiteral("series")
        : QStringLiteral("movie");
    const char *timeField = kind == RemovalKind::Library ? "added_at"
        : kind == RemovalKind::Watched ? "watched_at" : "last_watched";
    const qint64 at = kind == RemovalKind::Library ? record.value("addedAt").toLongLong()
        : kind == RemovalKind::Watched
            ? qMax(record.value("completedAt").toLongLong(), record.value("latestKnownAt").toLongLong())
            : record.value("updatedAt").toLongLong();
    QJsonObject row{{"content_id", kind == RemovalKind::Library ? id : root},
        {"content_type", type}, {QLatin1String(timeField), at}};
    if (id != root) {
        const auto coords = id.sliced(root.size() + 1).split(QLatin1Char(':'));
        row.insert("season", coords.value(0).toInt());
        row.insert("episode", coords.value(1).toInt());
    }
    return row;
}

bool belongsTo(const QJsonObject &row, const QString &accountId, int profileIndex) {
    return (!row.contains("user_id") || row.value("user_id").toString() == accountId)
        && (!row.contains("profile_id") || row.value("profile_id").toInt() == profileIndex);
}
}

struct NuvioAccount::Pull {
    quint64 generation = 0;
    NuvioTheatre::Profile profile;
    RemoteState result;
    PullCompletion completion;
    bool primary = false;
    bool incremental = false;
    std::optional<std::array<qint64, 3>> savedCursors;
    int addonProfileIndex = 0;
    int section = kLibrary;
    int page = 0;
    int deltaCount = 0;
    QHash<QString, qsizetype> rowIndexes;
    QHash<QString, qsizetype> changeIndexes;
};

struct NuvioAccount::Push {
    struct Operation {
        QString name;
        QString parameter;
        QJsonArray rows;
        std::optional<Removal> removal;
        std::optional<RemovalKind> upsertKind;
        bool prepared = true;
    };
    quint64 generation = 0;
    NuvioTheatre::Profile profile;
    QList<Operation> operations;
    qsizetype section = 0;
    qsizetype next = 0;
    int progressAlias = 0;
    int progressOffset = 0;
    int progressReadCount = 0;
};

NuvioAccount::NuvioAccount(AccountCredentialStore *vault, QObject *parent, NuvioApi::Transport transport)
    : QObject(parent), m_vault(vault), m_api(vault, this, std::move(transport)), m_import(this) {
    m_periodicTimer.setInterval(15 * 60 * 1000);
    m_dirtyTimer.setInterval(30000);
    m_dirtyTimer.setSingleShot(true);
    connect(&m_periodicTimer, &QTimer::timeout, this, &NuvioAccount::syncNow);
    connect(&m_dirtyTimer, &QTimer::timeout, this, &NuvioAccount::syncNow);
}

void NuvioAccount::activate(const QString &profileId, CollectionStore *collection, ProgressStore *progress,
    HistoryStore *history, ProfilePreferencesStore *preferences, const QString &cursorPath) {
    resetFlow();
    m_periodicTimer.stop();
    m_dirtyTimer.stop();
    if (m_collection)
        m_collection->setLocalRemovalGuard({});
    if (m_progress)
        m_progress->setLocalVideoRemovalGuard({});
    for (const auto &connection : std::as_const(m_ownerConnections))
        QObject::disconnect(connection);
    m_ownerConnections.clear();
    m_primarySince = 0;
    m_cursorPath = cursorPath;
    m_available = !profileId.isEmpty() && collection && progress && history && preferences
        && m_vault && m_vault->isAvailable();
    m_collection = collection;
    m_progress = progress;
    m_history = history;
    m_preferences = preferences;
    m_baseline.reset();
    m_api.activate(m_available ? profileId : QString());
    m_removalPath.clear();
    bindRemovalScope();
    m_import.bind(collection, progress, history, m_extensions);
    if (m_available) {
        const QPointer<NuvioAccount> self(this);
        collection->setLocalRemovalGuard([self](const QVariantMap &record) {
            return !self || record.value("world") != kTheatre
                || self->queueRemoval(RemovalKind::Library, record.value("id").toString(),
                    record.value("type").toString());
        });
        progress->setLocalVideoRemovalGuard([self](const QString &id, bool whole, const QVariantMap &entry) {
            const QString root = seriesRoot(id);
            const QString storedId = entry.value("id").toString();
            const bool series = root != id || seriesRoot(storedId) != storedId
                || entry.value("type") == QLatin1String("series")
                || entry.value("resume").toMap().value("subType") == QLatin1String("series");
            const QString type = series ? QStringLiteral("series") : QStringLiteral("movie");
            return !self || self->queueRemoval(RemovalKind::Progress, whole ? root : id, type,
                whole && type == QLatin1String("series"));
        });
        m_ownerConnections = {
            connect(collection, &CollectionStore::syncDirty, this, &NuvioAccount::scheduleSync),
            connect(progress, &ProgressStore::syncDirty, this, &NuvioAccount::scheduleSync),
            connect(progress, &ProgressStore::watchStateChanged, this, &NuvioAccount::scheduleSync),
            connect(history, &HistoryStore::changed, this, &NuvioAccount::scheduleSync),
            connect(preferences, &ProfilePreferencesStore::mainSyncProviderChanged,
                this, &NuvioAccount::primaryChanged)};
    }
    m_phase = kIdle;
    m_message.clear();
    primaryChanged();
}

void NuvioAccount::setExtensions(ExtensionsStore *extensions) {
    m_extensions = extensions;
    m_import.setExtensions(extensions);
}

void NuvioAccount::setCheckpoint(Checkpoint callback) {
    m_checkpoint = std::move(callback);
}

bool NuvioAccount::needsReconnect() const {
    return m_api.needsReconnect() || (m_available && primaryProvider() == kNuvio && !connected());
}

QString NuvioAccount::primaryProvider() const {
    return m_preferences ? m_preferences->mainSyncProvider() : QString();
}

QVariantList NuvioAccount::profiles() const {
    QVariantList rows;
    for (const auto &profile : m_profiles)
        rows.append(QVariantMap{{"index", profile.index}, {"name", profile.name}, {"locked", profile.locked}});
    return rows;
}

void NuvioAccount::setStatus(const QString &phase, bool busy, const QString &message) {
    m_phase = phase;
    m_busy = busy;
    m_message = message;
    emit changed();
}

void NuvioAccount::resetFlow() {
    ++m_generation;
    m_import.cancel();
    m_profiles.clear();
    m_selection = {};
    m_reviewed.reset();
    m_preview.clear();
    m_manual = false;
    m_commitStarted = false;
    m_primaryPreview = false;
    m_syncing = false;
    m_syncRequested = false;
    m_busy = false;
}

void NuvioAccount::signIn(const QString &email, const QString &password) {
    if (!m_available || m_busy)
        return;
    resetFlow();
    m_manual = true;
    setStatus(kSignIn, true);
    m_api.signIn(email, password, [this](const NuvioApi::Reply &reply) {
        if (reply.status != 200) {
            setStatus(kSignIn, false, reply.status == 400 || reply.status == 401
                ? tr("Nuvio could not sign in with those details.") : failure(reply.status));
            return;
        }
        loadProfiles();
    });
}

void NuvioAccount::loadProfiles() {
    if (!m_available || (m_busy && m_phase != kSignIn) || m_api.accountId().isEmpty())
        return;
    m_manual = true;
    setStatus(kProfiles, true);
    readProfiles([this](QList<NuvioTheatre::Profile> rows, int status) {
        m_profiles = std::move(rows);
        setStatus(m_profiles.isEmpty() ? kSignIn : kProfiles, false,
            status == 200 ? (m_profiles.isEmpty() ? tr("No Nuvio profiles are available.") : QString())
                          : failure(status));
    });
}

void NuvioAccount::previewProfile(int index, bool firstRun) {
    if (!m_manual || m_phase != kProfiles || m_busy)
        return;
    const auto *profile = selectedProfile(m_profiles, index);
    if (!profile || profile->locked) {
        setStatus(kProfiles, false, tr("This Nuvio profile is locked or unavailable."));
        return;
    }
    previewSelection(*profile, firstRun || m_primaryPreview || primaryProvider() == kNuvio);
}

void NuvioAccount::previewSelection(const NuvioTheatre::Profile &profile, bool primary) {
    m_selection = profile;
    m_primaryPreview = primary;
    if (m_removalAccount == m_api.accountId() && m_removalProfile == profile.id && !loadRemovals()) {
        setStatus(kProfiles, false, tr("Saved Nuvio removals could not be restored."));
        return;
    }
    setStatus(kProfiles, true, tr("Bringing in your library…"));
    readSnapshot(profile, primary, true, false, [this](std::optional<RemoteState> result, int status) {
        if (!result) {
            setStatus(kProfiles, false, failure(status));
            return;
        }
        m_reviewed = std::move(result);
        auto snapshot = m_reviewed->snapshot;
        NuvioTheatre::suppressRemoved(snapshot, applicableRemovals(m_reviewed->profileId));
        m_preview = m_import.preview(NuvioTheatre::project(snapshot, local()));
        setStatus(kPreview, false);
    });
}

void NuvioAccount::apply(bool firstRun) {
    if (!m_manual || m_busy || m_phase != kPreview || !m_reviewed
        || (firstRun && !m_primaryPreview) || !m_preferences) {
        return;
    }
    m_commitStarted = true;
    setStatus(kPreview, true, tr("Bringing in your library…"));
    readProfiles([this](const QList<NuvioTheatre::Profile> &rows, int status) {
        const auto *profile = selectedProfile(rows, m_selection.index, m_selection.id);
        if (!profile || profile->locked) {
            m_commitStarted = false;
            setStatus(kPreview, false, status == 200
                ? tr("The selected Nuvio profile is locked or has changed.") : failure(status));
            return;
        }
        const quint64 generation = m_generation;
        const bool saved = m_api.saveSignIn(profile->id, profile->index, [this] {
            return !m_primaryPreview || m_preferences->setMainSyncProvider(kNuvio);
        });
        if (generation != m_generation)
            return;
        if (!saved) {
            m_commitStarted = false;
            setStatus(kPreview, false, tr("The Nuvio connection or sync setting could not be saved."));
            return;
        }
        bindRemovalScope();
        if (!loadRemovals()) {
            m_commitStarted = false;
            setStatus(kPreview, false, tr("Saved Nuvio removals could not be restored."));
            return;
        }
        auto snapshot = m_reviewed->snapshot;
        NuvioTheatre::suppressRemoved(snapshot, applicableRemovals(m_reviewed->profileId));
        const auto items = NuvioTheatre::project(snapshot, local());
        const bool started = m_import.apply(items, [this, generation](TheatreAccountImport::Result result) {
            if (generation != m_generation)
                return;
            if (!result.committed) {
                m_commitStarted = false;
                setStatus(kPreview, false, tr("Import could not finish. You can retry the remaining items."));
                return;
            }
            checkpoint([this, generation, result](bool committed) {
                if (generation != m_generation)
                    return;
                m_commitStarted = false;
                if (!committed) {
                    setStatus(kPreview, false, tr("Imported data was saved locally. Account sync needs another attempt."));
                    return;
                }
                if (m_primaryPreview) {
                    if (!saveCursors(*m_reviewed)) {
                        setStatus(kPreview, false, tr("Imported data was saved. Nuvio's sync position could not be saved."));
                        return;
                    }
                    m_baseline = std::move(m_reviewed);
                    m_baseline->snapshot.addons = QJsonArray();
                }
                m_reviewed.reset();
                m_manual = false;
                emit canonicalChanged();
                setStatus(kDone, false, result.addonFailures == 0
                    ? tr("%n change(s) saved to Colosseum.", nullptr, result.changed)
                    : tr("%1 changes saved. %2 addons could not be installed.")
                        .arg(result.changed).arg(result.addonFailures));
                scheduleSync();
            });
        });
        if (!started) {
            m_commitStarted = false;
            setStatus(kPreview, false, tr("The active Theatre stores could not start the import."));
        }
    });
}

void NuvioAccount::cancel() {
    if (!m_manual || m_commitStarted)
        return;
    resetFlow();
    m_api.cancelSignIn();
    setStatus(kIdle, false);
    scheduleSync();
}

void NuvioAccount::disconnect() {
    if (!m_available || m_busy)
        return;
    if (!m_api.disconnectAccount()) {
        setStatus(m_phase, false, tr("The saved Nuvio credential could not be removed."));
        return;
    }
    resetFlow();
    m_baseline.reset();
    bindRemovalScope();
    const bool preferenceSaved = primaryProvider() != kNuvio
        || m_preferences->setMainSyncProvider({});
    setStatus(kIdle, false, preferenceSaved
        ? tr("Nuvio disconnected. Your Colosseum data is still here.")
        : tr("Nuvio disconnected. The primary setting could not be cleared."));
}

void NuvioAccount::makePrimary() {
    if (!m_available || !connected() || m_busy || m_manual || primaryProvider() == kNuvio)
        return;
    m_manual = true;
    setStatus(kProfiles, true, tr("Checking the selected Nuvio profile…"));
    readProfiles([this](QList<NuvioTheatre::Profile> rows, int status) {
        m_profiles = std::move(rows);
        const auto *profile = selectedProfile(m_profiles, m_api.selectedProfileIndex(), m_api.selectedProfileId());
        if (!profile || profile->locked) {
            setStatus(kProfiles, false, status == 200
                ? tr("Choose an unlocked Nuvio profile to use as primary.") : failure(status));
            m_primaryPreview = true;
            return;
        }
        previewSelection(*profile, true);
    });
}

void NuvioAccount::readProfiles(ProfilesCompletion completion) {
    m_api.rpc(QStringLiteral("sync_pull_profiles"), {},
        [this, completion = std::move(completion)](const NuvioApi::Reply &reply) {
            const auto rows = reply.status == 200 && reply.data.isArray()
                ? NuvioTheatre::profiles(reply.data.toArray(), m_api.accountId())
                : QList<NuvioTheatre::Profile>();
            completion(rows, reply.status);
        });
}

void NuvioAccount::readSnapshot(const NuvioTheatre::Profile &profile, bool primary,
    bool addons, bool incremental, PullCompletion completion) {
    auto pull = std::make_shared<Pull>();
    pull->generation = m_generation;
    pull->profile = profile;
    pull->primary = primary;
    pull->completion = std::move(completion);
    pull->incremental = incremental && m_baseline && m_baseline->profileId == profile.id;
    if (pull->incremental) {
        pull->result = *m_baseline;
    } else if (incremental) {
        pull->savedCursors = loadCursors(profile.id);
    }
    if (pull->incremental || pull->savedCursors)
        pull->result.changes.emplace();
    pull->result.profileId = profile.id;
    const auto *first = selectedProfile(m_profiles, 1);
    if (addons && !profile.usesPrimaryAddons)
        pull->addonProfileIndex = profile.index;
    else if (addons && first && !first->locked)
        pull->addonProfileIndex = 1;
    readSection(pull);
}

void NuvioAccount::readSection(const std::shared_ptr<Pull> &pull) {
    if (pull->generation != m_generation)
        return;
    if (pull->section > (pull->primary ? kProgress : kWatched)) {
        if (pull->addonProfileIndex == 0) {
            pull->completion(std::move(pull->result), 200);
            return;
        }
        m_api.addons(pull->addonProfileIndex, [this, pull](const NuvioApi::Reply &reply) {
            if (reply.status != 200 || !reply.data.isArray() || reply.data.toArray().size() > kMaxAddons) {
                failPull(pull, reply.status);
                return;
            }
            for (const QJsonValue &value : reply.data.toArray()) {
                if (!belongsTo(value.toObject(), m_api.accountId(), pull->addonProfileIndex)) {
                    failPull(pull, 0);
                    return;
                }
            }
            pull->result.snapshot.addons = reply.data.toArray();
            pull->completion(std::move(pull->result), 200);
        });
        return;
    }

    pull->page = 0;
    pull->deltaCount = 0;
    pull->rowIndexes.clear();
    pull->changeIndexes.clear();
    if (pull->incremental) {
        readDelta(pull);
        return;
    }
    if (!pull->primary) {
        readPage(pull);
        return;
    }
    m_api.rpc(QString::fromLatin1(cursorCalls[pull->section]), {{"p_profile_id", pull->profile.index}},
        [this, pull](const NuvioApi::Reply &reply) {
            const qint64 cursor = reply.data.toInteger(-1);
            if (reply.status != 200 || cursor < 0) {
                failPull(pull, reply.status);
                return;
            }
            const qint64 from = pull->savedCursors ? pull->savedCursors->at(pull->section) : cursor;
            if (from > cursor) {
                failPull(pull, 0);
                return;
            }
            pull->result.cursors[pull->section] = from;
            readPage(pull);
        });
}

void NuvioAccount::readPage(const std::shared_ptr<Pull> &pull) {
    if (pull->generation != m_generation)
        return;
    const int section = pull->section;
    const int limit = section == kProgress ? kProgressLimit : kPageSize;
    QJsonObject body{{"p_profile_id", pull->profile.index}};
    if (section == kLibrary) {
        body.insert("p_limit", limit);
        body.insert("p_offset", pull->page * limit);
    } else if (section == kWatched) {
        body.insert("p_page_size", limit);
        body.insert("p_page", pull->page + 1);
    } else {
        body.insert("p_limit", limit);
    }
    m_api.rpc(QString::fromLatin1(snapshotCalls[section]), body,
        [this, pull, limit, section](const NuvioApi::Reply &reply) {
            QJsonArray &rows = sectionRows(pull->result.snapshot, section);
            const QJsonArray page = reply.data.toArray();
            if (reply.status != 200 || !reply.data.isArray() || page.size() > limit
                || rows.size() + page.size() > kMaxRows) {
                failPull(pull, reply.status);
                return;
            }
            for (const QJsonValue &value : page) {
                if (!belongsTo(value.toObject(), m_api.accountId(), pull->profile.index)) {
                    failPull(pull, 0);
                    return;
                }
                rows.append(value);
            }
            if (section != kProgress && page.size() == limit) {
                ++pull->page;
                QTimer::singleShot(0, this, [this, pull] { readPage(pull); });
                return;
            }
            if (section == kProgress && page.size() == limit) {
                const qint64 oldest = page.last().toObject().value("last_watched").toInteger();
                pull->result.snapshot.progressWindowFloor = oldest > 0
                    ? oldest : std::numeric_limits<qint64>::max();
            }
            if (pull->primary)
                readDelta(pull);
            else
                finishSection(pull);
        });
}

void NuvioAccount::readDelta(const std::shared_ptr<Pull> &pull) {
    if (pull->generation != m_generation)
        return;
    const int section = pull->section;
    QJsonArray &rows = sectionRows(pull->result.snapshot, section);
    if (pull->deltaCount == 0) {
        for (qsizetype index = 0; index < rows.size(); ++index) {
            const QString key = rowKey(rows.at(index).toObject(), section);
            if (!key.isEmpty())
                pull->rowIndexes.insert(key, index);
        }
    }
    m_api.rpc(QString::fromLatin1(deltaCalls[section]),
        {{"p_profile_id", pull->profile.index}, {"p_since_event_id", pull->result.cursors[section]},
         {"p_limit", kPageSize}},
        [this, pull, section](const NuvioApi::Reply &reply) {
            const QJsonArray events = reply.data.toArray();
            if (reply.status != 200 || !reply.data.isArray() || events.size() > kPageSize
                || pull->deltaCount + events.size() > kMaxRows) {
                failPull(pull, reply.status);
                return;
            }
            auto &rows = sectionRows(pull->result.snapshot, section);
            qint64 &cursor = pull->result.cursors[section];
            for (const QJsonValue &value : events) {
                const QJsonObject row = value.toObject();
                const qint64 event = row.value("event_id").toInteger(-1);
                const QString operation = row.value("operation").toString();
                const QString key = rowKey(row, section);
                if (event <= cursor || (operation != QLatin1String("upsert") && operation != QLatin1String("delete"))
                    || !belongsTo(row, m_api.accountId(), pull->profile.index)) {
                    failPull(pull, 0);
                    return;
                }
                cursor = event;
                const auto existing = pull->rowIndexes.constFind(key);
                if (!key.isEmpty() && existing != pull->rowIndexes.constEnd()) {
                    rows.replace(*existing, value);
                } else {
                    if (rows.size() >= kMaxRows) {
                        failPull(pull, 0);
                        return;
                    }
                    if (!key.isEmpty())
                        pull->rowIndexes.insert(key, rows.size());
                    rows.append(value);
                }
                if (pull->result.changes) {
                    auto &changes = sectionRows(*pull->result.changes, section);
                    const auto changed = pull->changeIndexes.constFind(key);
                    if (!key.isEmpty() && changed != pull->changeIndexes.constEnd()) {
                        changes.replace(*changed, value);
                    } else {
                        if (!key.isEmpty())
                            pull->changeIndexes.insert(key, changes.size());
                        changes.append(value);
                    }
                }
            }
            pull->deltaCount += events.size();
            if (events.size() == kPageSize) {
                QTimer::singleShot(0, this, [this, pull] { readDelta(pull); });
                return;
            }
            finishSection(pull);
        });
}

void NuvioAccount::finishSection(const std::shared_ptr<Pull> &pull) {
    ++pull->section;
    QTimer::singleShot(0, this, [this, pull] { readSection(pull); });
}

void NuvioAccount::failPull(const std::shared_ptr<Pull> &pull, int status) {
    if (pull->generation == m_generation)
        pull->completion(std::nullopt, status);
}

void NuvioAccount::primaryChanged() {
    if (primaryProvider() == kNuvio && m_available) {
        if (m_primarySince == 0)
            m_primarySince = QDateTime::currentMSecsSinceEpoch();
        m_periodicTimer.start();
        if (!m_manual)
            QTimer::singleShot(0, this, &NuvioAccount::syncNow);
    } else {
        m_periodicTimer.stop();
        m_dirtyTimer.stop();
        m_baseline.reset();
        const bool leavingPrimary = std::exchange(m_primarySince, 0) > 0;
        if (m_syncing || (leavingPrimary && m_commitStarted && m_primaryPreview)) {
            resetFlow();
            m_api.cancelSignIn();
            m_phase = kIdle;
            m_message.clear();
        }
    }
    emit changed();
}

void NuvioAccount::scheduleSync() {
    if (primaryProvider() != kNuvio || !connected() || m_api.needsReconnect() || m_manual)
        return;
    if (m_syncing)
        m_syncRequested = true;
    else if (!m_dirtyTimer.isActive())
        m_dirtyTimer.start();
}

void NuvioAccount::syncNow() {
    if (!m_available || m_busy || m_manual || primaryProvider() != kNuvio
        || !connected() || needsReconnect()) {
        return;
    }
    m_dirtyTimer.stop();
    if (!loadRemovals() || !applyLocalRemovals()) {
        setStatus(kIdle, false, tr("Saved Nuvio removals could not be restored."));
        return;
    }
    m_syncing = true;
    setStatus(kIdle, true, tr("Syncing with Nuvio…"));
    readProfiles([this](const QList<NuvioTheatre::Profile> &rows, int status) {
        const auto *profile = selectedProfile(rows, m_api.selectedProfileIndex(), m_api.selectedProfileId());
        if (!profile || profile->locked) {
            finishSync(false, status);
            if (status == 200)
                setStatus(kIdle, false, tr("The selected Nuvio profile is locked or has changed."));
            return;
        }
        readSnapshot(*profile, true, false, true,
            [this, profile = *profile](std::optional<RemoteState> result, int code) {
                if (!result) {
                    m_baseline.reset();
                    finishSync(false, code);
                    return;
                }
                syncApply(profile, std::move(*result));
            });
    });
}

void NuvioAccount::syncApply(const NuvioTheatre::Profile &profile, RemoteState state) {
    // An unchanged remote snapshot is not an incoming mutation. Reapplying
    // it on every poll would recreate entries removed locally between polls.
    auto incoming = state.changes ? *state.changes : state.snapshot;
    NuvioTheatre::suppressRemoved(incoming, applicableRemovals(state.profileId));
    const auto items = NuvioTheatre::project(incoming, local());
    const quint64 generation = m_generation;
    const bool started = m_import.apply(items,
        [this, profile, generation, state = std::move(state)](TheatreAccountImport::Result result) mutable {
            if (generation != m_generation || primaryProvider() != kNuvio)
                return;
            if (!result.committed) {
                finishSync(false);
                return;
            }
            checkpoint([this, profile, generation, state = std::move(state)](bool committed) mutable {
                if (generation != m_generation || primaryProvider() != kNuvio)
                    return;
                if (!committed) {
                    finishSync(false);
                    return;
                }
                if (!saveCursors(state)) {
                    finishSync(false);
                    setStatus(kIdle, false, tr("Nuvio's sync position could not be saved."));
                    return;
                }
                // A numbered remote profile can be replaced while a pull is
                // running. Confirm the UUID is still at that index before writes.
                readProfiles([this, profile, generation, state = std::move(state)]
                    (const QList<NuvioTheatre::Profile> &rows, int status) mutable {
                        const auto *current = selectedProfile(rows, profile.index, profile.id);
                        if (!current || current->locked) {
                            finishSync(false, status);
                            return;
                        }
                        const auto writes = NuvioTheatre::reconcile(state.snapshot, local());
                        state.changes.reset();
                        m_baseline = std::move(state);
                        auto push = std::make_shared<Push>();
                        push->generation = generation;
                        push->profile = profile;
                        const auto removals = applicableRemovals(profile.id);
                        for (const auto &removal : removals) {
                            if (removal.confirmedAt > 0)
                                continue;
                            switch (removal.kind) {
                            case RemovalKind::Library:
                                push->operations.append({QStringLiteral("sync_delete_library_items"),
                                    QStringLiteral("p_keys"),
                                    NuvioTheatre::deletionKeys(removal, m_baseline->snapshot.library), removal});
                                break;
                            case RemovalKind::Watched:
                                push->operations.append({QStringLiteral("sync_delete_watched_items"),
                                    QStringLiteral("p_keys"),
                                    NuvioTheatre::deletionKeys(removal, m_baseline->snapshot.watched), removal});
                                break;
                            case RemovalKind::Progress:
                                // The import snapshot is capped at 200. Fetch the exact title's
                                // older rows before acknowledging a whole-title deletion.
                                push->operations.append({QStringLiteral("sync_delete_watch_progress"),
                                    QStringLiteral("p_keys"), {}, removal, std::nullopt, false});
                                break;
                            }
                        }
                        push->operations += QList<Push::Operation>{
                            {QStringLiteral("sync_push_library_items"), QStringLiteral("p_items"), writes.library,
                                std::nullopt, RemovalKind::Library},
                            {QStringLiteral("sync_push_watch_progress"), QStringLiteral("p_entries"), writes.progress,
                                std::nullopt, RemovalKind::Progress},
                            {QStringLiteral("sync_push_watched_items"), QStringLiteral("p_items"), writes.watched,
                                std::nullopt, RemovalKind::Watched},
                            {QStringLiteral("sync_delete_watched_items"), QStringLiteral("p_keys"), writes.unwatchMovies}};
                        writeNext(push);
                    });
            });
        });
    if (!started)
        finishSync(false);
}

void NuvioAccount::writeNext(const std::shared_ptr<Push> &push) {
    if (push->generation != m_generation || primaryProvider() != kNuvio)
        return;
    while (push->section < push->operations.size()) {
        const auto &operation = push->operations.at(push->section);
        if (!operation.prepared) {
            readProgressDeletionKeys(push);
            return;
        }
        if (push->next < operation.rows.size())
            break;
        if (operation.removal && !acknowledgeRemoval(*operation.removal)) {
            finishSync(false);
            setStatus(kIdle, false, tr("Nuvio confirmed a removal, but its receipt could not be saved."));
            return;
        }
        ++push->section;
        push->next = 0;
    }
    if (push->section >= push->operations.size()) {
        finishSync(true);
        return;
    }
    const Push::Operation &operation = push->operations.at(push->section);
    QJsonArray batch;
    while (batch.size() < kWriteBatch && push->next < operation.rows.size()) {
        const QJsonValue value = operation.rows.at(push->next++);
        if (value.isString()) {
            batch.append(value);
            continue;
        }
        QJsonObject row = value.toObject();
        if (operation.upsertKind && NuvioTheatre::isSuppressed(*operation.upsertKind, row, m_removals))
            continue;
        // A completion progress upsert also marks the episode watched on Nuvio.
        // A pending unwatch must not be undone by that side effect.
        if (operation.upsertKind == RemovalKind::Progress
            && row.value("duration").toInteger() >= 60000
            && row.value("position").toDouble() >= 0.9 * row.value("duration").toDouble()) {
            auto watched = row;
            watched.insert("watched_at", row.value("last_watched"));
            if (NuvioTheatre::isSuppressed(RemovalKind::Watched, watched, m_removals))
                continue;
        }
        if (row.contains("local_id")) {
            const QString localId = row.take("local_id").toString();
            const qint64 action = row.take("action_at").toInteger();
            if (!m_progress || m_progress->watchedMark(localId) != -1
                || m_progress->watchedMarkActionAt(localId) != action) {
                continue;
            }
        }
        batch.append(row);
    }
    if (batch.isEmpty()) {
        QTimer::singleShot(0, this, [this, push] { writeNext(push); });
        return;
    }
    m_api.rpc(operation.name, {{"p_profile_id", push->profile.index}, {operation.parameter, batch}},
        [this, push](const NuvioApi::Reply &reply) {
            if (push->generation != m_generation || primaryProvider() != kNuvio)
                return;
            if (reply.status < 200 || reply.status >= 300) {
                m_baseline.reset();
                finishSync(false, reply.status);
                return;
            }
            QTimer::singleShot(0, this, [this, push] { writeNext(push); });
        });
}

void NuvioAccount::readProgressDeletionKeys(const std::shared_ptr<Push> &push) {
    auto &operation = push->operations[push->section];
    const auto &removal = *operation.removal;
    if (push->progressAlias >= removal.contentIds.size()) {
        operation.prepared = true;
        push->progressAlias = 0;
        push->progressOffset = 0;
        push->progressReadCount = 0;
        writeNext(push);
        return;
    }
    m_api.progressForRemoval(push->profile.index, removal.contentIds.at(push->progressAlias),
        removal.type, removal.at, push->progressOffset,
        [this, push](const NuvioApi::Reply &reply) {
            if (push->generation != m_generation || primaryProvider() != kNuvio)
                return;
            auto &operation = push->operations[push->section];
            const auto page = reply.data.toArray();
            if (reply.status != 200 || !reply.data.isArray() || page.size() > kPageSize
                || push->progressReadCount + page.size() > kMaxRows) {
                finishSync(false, reply.status);
                return;
            }
            for (const QJsonValue &value : page) {
                const QJsonObject row = value.toObject();
                if (!belongsTo(row, m_api.accountId(), push->profile.index)
                    || row.value("content_id").toString() != operation.removal->contentIds.at(push->progressAlias)
                    || row.value("content_type").toString() != operation.removal->type
                    || row.value("last_watched").toInteger(-1) < 0
                    || row.value("last_watched").toInteger() > operation.removal->at
                    || row.value("progress_key").toString().isEmpty()) {
                    finishSync(false);
                    return;
                }
            }
            for (const QJsonValue &key : NuvioTheatre::deletionKeys(*operation.removal, page))
                operation.rows.append(key);
            push->progressReadCount += page.size();
            if (page.size() == kPageSize) {
                push->progressOffset += kPageSize;
            } else {
                ++push->progressAlias;
                push->progressOffset = 0;
            }
            QTimer::singleShot(0, this, [this, push] { writeNext(push); });
        });
}

void NuvioAccount::bindRemovalScope() {
    const QString account = connected() ? m_api.accountId() : QString();
    const QString profile = connected() ? m_api.selectedProfileId() : QString();
    if (m_removalAccount == account && m_removalProfile == profile && !m_removalPath.isEmpty()
        && QFileInfo(m_removalPath).dir() == QFileInfo(m_cursorPath).dir()) {
        return;
    }
    m_removalAccount = account;
    m_removalProfile = profile;
    m_removalsLoaded = false;
    m_removals.clear();
    m_removalPath.clear();
    if (!account.isEmpty() && !profile.isEmpty() && !m_cursorPath.isEmpty()) {
        const QByteArray digest = QCryptographicHash::hash((account + QLatin1Char('/') + profile).toUtf8(),
            QCryptographicHash::Sha256).toHex();
        m_removalPath = QFileInfo(m_cursorPath).dir().filePath(
            QStringLiteral("removals-v1-%1.json").arg(QString::fromLatin1(digest)));
        loadRemovals();
    }
    ++m_removalRevision;
    emit removalsChanged();
}

bool NuvioAccount::loadRemovals() {
    if (m_removalsLoaded || m_removalPath.isEmpty())
        return true;
    QFile file(m_removalPath);
    if (!file.exists()) {
        m_removalsLoaded = true;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
        return false;
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object();
    const auto rows = envelope.value("rows");
    if (envelope.value("version").toInt() != 1 || !rows.isArray() || rows.toArray().size() > kMaxRemovals)
        return false;
    QList<Removal> decoded;
    for (const QJsonValue &value : rows.toArray()) {
        auto removal = Removal::deserialize(value.toObject());
        if (!removal)
            return false;
        decoded.append(std::move(*removal));
    }
    m_removals = std::move(decoded);
    m_removalsLoaded = true;
    return true;
}

bool NuvioAccount::saveRemovals(QList<Removal> next) {
    if (m_removalPath.isEmpty() || next.size() > kMaxRemovals
        || !QDir().mkpath(QFileInfo(m_removalPath).absolutePath())) {
        return false;
    }
    QJsonArray rows;
    for (const auto &entry : next)
        rows.append(entry.serialize());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"rows", rows}})
        .toJson(QJsonDocument::Compact);
    QSaveFile file(m_removalPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return false;
    m_removals = std::move(next);
    ++m_removalRevision;
    emit removalsChanged();
    return true;
}

bool NuvioAccount::queueRemoval(RemovalKind kind, const QString &id, const QString &type, bool wholeTitle) {
    if (primaryProvider() != kNuvio || m_removalAccount.isEmpty() || m_removalProfile.isEmpty())
        return true;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto removal = NuvioTheatre::makeRemoval(kind, id, type, wholeTitle, now, local());
    if (!removal)
        return true; // The item has no portable Nuvio identity.
    if (!loadRemovals())
        return false;
    auto next = m_removals;
    next.removeIf([&](const auto &old) {
        return old.sameTarget(*removal)
            || (old.confirmedAt > 0 && old.confirmedAt <= now - kRemovalRetentionMs);
    });
    next.append(std::move(*removal));
    if (!saveRemovals(std::move(next))) {
        m_message = tr("The Nuvio removal could not be saved.");
        emit changed();
        return false;
    }
    scheduleSync();
    return true;
}

QList<Removal> NuvioAccount::applicableRemovals(const QString &profileId) const {
    return m_api.accountId() == m_removalAccount && profileId == m_removalProfile
        ? m_removals : QList<Removal>();
}

bool NuvioAccount::acknowledgeRemoval(const Removal &removal) {
    auto next = m_removals;
    auto it = std::find_if(next.begin(), next.end(), [&](const auto &entry) {
        return entry.sameTarget(removal) && entry.at == removal.at;
    });
    if (it == next.end() || it->confirmedAt > 0)
        return true;
    it->confirmedAt = QDateTime::currentMSecsSinceEpoch();
    return saveRemovals(std::move(next));
}

bool NuvioAccount::applyLocalRemovals() {
    if (m_removals.isEmpty())
        return true;
    const qint64 pruneBefore = QDateTime::currentMSecsSinceEpoch() - kRemovalRetentionMs;
    auto retained = m_removals;
    retained.removeIf([pruneBefore](const auto &entry) {
        return entry.confirmedAt > 0 && entry.confirmedAt <= pruneBefore;
    });
    if (retained.size() != m_removals.size() && !saveRemovals(std::move(retained)))
        return false;

    // A crash can happen after the removal intent was committed but before
    // the owner finished its write. Replay only values older than the intent.
    const auto snapshot = local();
    for (const auto &value : snapshot.library) {
        const auto row = value.toMap();
        if (NuvioTheatre::isSuppressed(RemovalKind::Library, localFact(RemovalKind::Library, row), m_removals)
            && !m_collection->removeSyncedEntry(kTheatre, row.value("id").toString())) {
            return false;
        }
    }
    for (const auto &value : snapshot.progress) {
        const auto row = value.toMap();
        if (row.value("kind") == QLatin1String("video")
            && NuvioTheatre::isSuppressed(RemovalKind::Progress, localFact(RemovalKind::Progress, row), m_removals)
            && !m_progress->removeSyncedEntry(QStringLiteral("video"), row.value("id").toString())) {
            return false;
        }
    }
    for (const auto &value : snapshot.history) {
        const auto row = value.toMap();
        if (row.value("kind") == QLatin1String("episode")
            && NuvioTheatre::isSuppressed(RemovalKind::Watched, localFact(RemovalKind::Watched, row), m_removals)
            && !m_history->removeSyncedRecord(QStringLiteral("episode"), row.value("id").toString())) {
            return false;
        }
    }
    return true;
}

bool NuvioAccount::canUnwatchEpisode(const QString &id) const {
    return primaryProvider() == kNuvio && connected() && m_history && m_history->healthy()
        && NuvioTheatre::makeRemoval(RemovalKind::Watched, id, QStringLiteral("series"), false, 1, {}).has_value();
}

bool NuvioAccount::unwatchEpisode(const QString &id) {
    return canUnwatchEpisode(id) && queueRemoval(RemovalKind::Watched, id, QStringLiteral("series"))
        && m_history->remove(QStringLiteral("episode"), id);
}

bool NuvioAccount::episodeUnwatched(const QString &id) const {
    if (primaryProvider() != kNuvio || !m_history || !m_progress)
        return false;
    auto episode = m_history->get(QStringLiteral("episode"), id);
    episode.insert("id", id);
    episode.insert("kind", QStringLiteral("episode"));
    auto row = localFact(RemovalKind::Watched, episode);
    row.insert("watched_at", qMax(row.value("watched_at").toInteger(),
        m_progress->get(QStringLiteral("video"), id).value("updatedAt").toLongLong()));
    const qint64 removed = m_history->removedAt(QStringLiteral("episode"), id);
    return (removed > 0 && row.value("watched_at").toInteger() <= removed)
        || NuvioTheatre::isSuppressed(RemovalKind::Watched, row, m_removals);
}

void NuvioAccount::finishSync(bool ok, int status) {
    m_syncing = false;
    setStatus(kIdle, false, ok ? tr("Nuvio sync complete.") : failure(status));
    if (std::exchange(m_syncRequested, false)
        || (!ok && (status == 0 || status == 429 || status >= 500)
            && std::any_of(m_removals.cbegin(), m_removals.cend(), [](const Removal &removal) {
                return removal.confirmedAt == 0;
            }))) {
        scheduleSync();
    }
}

void NuvioAccount::checkpoint(std::function<void(bool)> completion) {
    if (m_checkpoint) {
        m_checkpoint([self = QPointer<NuvioAccount>(this), completion = std::move(completion)](bool committed) {
            if (self)
                completion(committed);
        });
    } else {
        completion(true);
    }
}

std::optional<std::array<qint64, 3>> NuvioAccount::loadCursors(const QString &sourceProfile) const {
    if (m_cursorPath.isEmpty())
        return std::nullopt;
    QFile file(m_cursorPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return std::nullopt;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("version").toInt() != 1
        || object.value("account").toString() != m_api.accountId()
        || object.value("profile").toString() != sourceProfile) {
        return std::nullopt;
    }
    const auto values = object.value("cursors").toArray();
    std::array<qint64, 3> result;
    if (values.size() != static_cast<qsizetype>(result.size()))
        return std::nullopt;
    for (qsizetype index = 0; index < values.size(); ++index) {
        result[index] = values.at(index).toInteger(-1);
        if (result[index] < 0)
            return std::nullopt;
    }
    return result;
}

bool NuvioAccount::saveCursors(const RemoteState &state) const {
    if (m_cursorPath.isEmpty())
        return true;
    if (!QDir().mkpath(QFileInfo(m_cursorPath).absolutePath()))
        return false;
    QJsonArray cursors;
    for (const qint64 cursor : state.cursors)
        cursors.append(QJsonValue(cursor));
    // The cursor tracks which remote events reached durable owners. It is
    // device-private and bound to the exact selected account/profile pair.
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {"version", 1}, {"account", m_api.accountId()}, {"profile", state.profileId},
        {"cursors", cursors}}).toJson(QJsonDocument::Compact);
    QSaveFile file(m_cursorPath);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

NuvioTheatre::Local NuvioAccount::local() const {
    return {m_collection ? m_collection->items(kTheatre) : QVariantList(),
        m_progress ? m_progress->syncEntries() : QVariantList(),
        m_history ? m_history->syncEntries() : QVariantList(),
        m_progress ? m_progress->syncWatchedMarks() : QHash<QString, int>(),
        m_progress ? m_progress->syncWatchedMarkActionTimes() : QHash<QString, qint64>(),
        m_primarySince,
        m_history ? m_history->episodeRemovalTimes() : QHash<QString, qint64>()};
}

QString NuvioAccount::failure(int status) const {
    if (m_api.needsReconnect())
        return tr("Nuvio needs you to reconnect.");
    if (status == 429)
        return tr("Nuvio asked Colosseum to wait before trying again.");
    return tr("Nuvio could not finish this request. Try again in a moment.");
}
