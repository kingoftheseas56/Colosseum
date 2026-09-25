#pragma once

// Progress — the continue / resume backbone exposed to QML as `Progress`.
// It is one small thing: a persisted note of "what you opened and how far you got."
// The player writes it as you watch; the manga reader writes it as you read; every
// Continue row reads it back. No network, no scraping — just memory + disk, like a
// bookmark file. Persisted via QSettings (the same lightweight-state mechanism the
// manga reader already uses for prefs), so it survives a restart.
//
// PERSISTENCE THREADING (2026-07-29 video-stutter fix). Serializing the whole Continue map and
// calling QSettings::sync() ran on the GUI/render thread every 5s — blocking disk work on the
// thread that paints frames, which is a hazard on principle. Both now run on a dedicated
// background writer (ProgressDiskWriter): the GUI thread only mutates the in-memory map and posts
// a snapshot; the worker serializes + syncs on its own thread. Every posted snapshot is written
// (there is no coalescing — see ProgressDiskWriter). The latest snapshot is flushed synchronously
// at shutdown (aboutToQuit) so the final resume point always lands.
// NOT claimed: that moving the disk write off-thread measurably reduced dropped frames. It did
// not — see WRITE POLICY below. The cascade fix (recordSilent) is the change that moved the
// needle; this one removes a GUI-thread block that should not have been there regardless.
//
// WRITE POLICY (Option B — 5s off-thread writes, retained 2026-07-29). The 5s playback tick
// persists via the off-thread writer (crash-resume within 5s). A lifecycle-only variant (Option
// A, memory-only tick) was built and measured but gave no smoothness gain: once the changed()
// cascade is silenced, residual output drops are variance-dominated by background poster
// fetching / system load (same-window runs spanned 0-112 regardless of write policy), not by the
// disk path. Option B is therefore kept for its better crash-resume granularity at equal cost.
//
// QML side (the only contract):
//   Progress.record({ id, kind, caption, title, sub, cover, c1, c2, progress, resume })
//   Progress.recordSilent({...})    // 5s playback tick: persist WITHOUT refreshing the row
//   Progress.recent(kind, limit)   // kind "" = all (the unified home row); newest first
//   Progress.forget(kind, id)   // drops the whole Continue tile: for a series, every
//                               //   episode of that show, not just the id passed in
//   Progress.revision              // bump on every change — name it in a binding to make
//                                  // recent()-based bindings re-evaluate reactively.

#include <QObject>
#include <QSettings>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVariantHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDateTime>
#include <QPointer>
#include <QThread>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <memory>
#include <algorithm>
#include <cmath>
#include <functional>

namespace ProgressStoreDetail {
// Isolation gate (2026-08-14 fix). ProgressStore/CollectionStore historically hardcoded the
// QSettings two-arg constructor (org="Brotherhood", app="Colosseum"), which resolves straight
// to the Windows registry at HKCU\Software\Brotherhood\Colosseum — the REAL user's hive — no
// matter what QCoreApplication::applicationName() the process was given. That bypassed
// COLOSSEUM_APPDATA_TAG entirely: a tagged/isolated Lanista test session still read AND wrote
// the real Continue map (proven: a test journey wrote a manga entry into the real registry).
// main.cpp's tag already re-roots QStandardPaths::AppDataLocation to a disposable per-tag
// folder (main.cpp, COLOSSEUM_APPDATA_TAG block, ~line 559) — this just routes the store's
// persistence there too, as a file, instead of the shared registry. Untagged: returns an
// empty string, meaning "use the registry" — the daily app is byte-for-byte unaffected.
// Named per-store (progressStore...) rather than shared: ProgressStore.h and
// CollectionStore.h are both #included into main.cpp, so their helper symbols
// must remain distinct in that translation unit.
inline QString progressStoreTaggedIniPath(const QString &storeFileName) {
    if (!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"))
        return QString();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + storeFileName;
}

inline QString trackerImportReceiptSnapshotKey() {
    return QStringLiteral("__colosseum_tracker_import_receipts_v1");
}

inline QString trackerProgressSourceRemovalSnapshotKey() {
    return QStringLiteral("__colosseum_tracker_progress_removed_sources_v1");
}

inline QString trackerProgressSourceIdentityKey(const QString &providerKey,
                                                const QString &remoteAccountId) {
    QJsonArray identity;
    identity.append(providerKey);
    identity.append(remoteAccountId);
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(identity).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

inline QVariantMap withoutTrackerImportSourceMetadata(QVariantMap entry) {
    entry.remove(QStringLiteral("_trackerProviderKey"));
    entry.remove(QStringLiteral("_trackerRemoteAccountId"));
    entry.remove(QStringLiteral("_trackerSources"));
    entry.remove(QStringLiteral("_trackerBasePresent"));
    entry.remove(QStringLiteral("_trackerBaseEntry"));
    return entry;
}

inline QVariantMap withoutTrackerPrivateMetadata(QVariantMap entry) {
    entry = withoutTrackerImportSourceMetadata(std::move(entry));
    entry.remove(QStringLiteral("_trackerOrigin"));
    return entry;
}

inline QVariantList trackerProgressSources(const QVariantMap &entry) {
    const QVariantList stored = entry.value(QStringLiteral("_trackerSources")).toList();
    if (!stored.isEmpty())
        return stored;

    // Read rows written before source stacking was introduced. The old row
    // carries exactly one provider/account attribution and no recoverable base.
    if (entry.value(QStringLiteral("_trackerOrigin")).toString()
            != QLatin1String("tracker_import")) {
        return {};
    }
    const QString providerKey = entry.value(QStringLiteral("_trackerProviderKey")).toString();
    const QString remoteAccountId = entry.value(QStringLiteral("_trackerRemoteAccountId")).toString();
    if (providerKey.isEmpty() || remoteAccountId.isEmpty())
        return {};
    QVariantMap sourceEntry = withoutTrackerImportSourceMetadata(entry);
    sourceEntry.remove(QStringLiteral("_trackerOrigin"));
    return {QVariantMap{{QStringLiteral("providerKey"), providerKey},
                        {QStringLiteral("remoteAccountId"), remoteAccountId},
                        {QStringLiteral("entry"), sourceEntry}}};
}

inline bool trackerProgressHasSource(const QVariantMap &entry,
                                     const QString &providerKey,
                                     const QString &remoteAccountId) {
    const QVariantList sources = trackerProgressSources(entry);
    return std::any_of(sources.cbegin(), sources.cend(),
        [&providerKey, &remoteAccountId](const QVariant &value) {
            const QVariantMap source = value.toMap();
            return source.value(QStringLiteral("providerKey")).toString() == providerKey
                && source.value(QStringLiteral("remoteAccountId")).toString() == remoteAccountId;
        });
}
} // namespace ProgressStoreDetail

// Background disk writer for the Continue map. Owns the QSettings that persists `continue/
// entries` and performs ALL JSON serialization + QSettings::sync() on its OWN thread, so the
// GUI/render thread is never blocked by disk during playback. Each posted snapshot is written
// directly on the worker thread (there is NO debounce/coalescing: every scheduleSave() enqueues
// one full snapshot; the cost is a cheap shared QVariantHash copy, and bursts are rare because
// the player ticks at 5s and lifecycle writes are user-driven).
//
// THREAD AFFINITY: the QSettings is created LAZILY inside writeSnapshot() (the worker slot),
// NOT in this constructor. This object is constructed on the GUI thread and then moveToThread()'d
// to the worker; a QSettings constructed as a member here would pin GUI-thread affinity (member
// objects do not move with moveToThread — only the QObject itself and its QObject children do),
// and then be touched from the worker slot, which is a cross-thread QObject violation. By
// deferring construction to the first slot invocation, the QSettings is born on the worker thread
// and stays there. (Qt: "QObject affinity follows construction unless the object or its parent is
// moved" — https://doc.qt.io/qt-6/threads-qobject.html.)
class ProgressDiskWriter : public QObject {
    Q_OBJECT
public:
    explicit ProgressDiskWriter(const QString &org, const QString &app, QObject *parent = nullptr)
        : QObject(parent), m_org(org), m_app(app), m_useIni(false) {}
    explicit ProgressDiskWriter(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent), m_iniPath(iniPath), m_useIni(true) {}

public slots:
    void writeSnapshot(const QVariantHash &snapshot) {
        QString error;
        const bool committed = writeSnapshotInternal(snapshot, &error);
        if (committed)
            emit snapshotWritten(snapshot);
        else
            emit writeFailed(error);
    }

    void writeSnapshotWithReceipt(quint64 requestId,
                                  const QVariantHash &snapshot) {
        QString error;
        const bool committed = writeSnapshotInternal(snapshot, &error);
        if (committed)
            emit snapshotWritten(snapshot);
        emit snapshotFinished(requestId, committed, error);
    }
#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
    void failNextReceiptWriteForTesting() { m_failNextReceiptWrite = true; }
#endif
    void flushSync() {}   // shutdown/flush barrier (see ProgressStore::flush)

signals:
    void snapshotWritten(const QVariantHash &snapshot);
    void snapshotFinished(quint64 requestId, bool committed,
                           const QString &error);
    void writeFailed(const QString &error);

private:
    bool writeSnapshotInternal(const QVariantHash &snapshot, QString *error) {
#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
        if (m_failNextReceiptWrite) {
            m_failNextReceiptWrite = false;
            if (error)
                *error = QStringLiteral("Injected tracker Progress writer failure.");
            return false;
        }
#endif
        ensureSettings();   // created on the worker thread on first use (correct affinity)
        QJsonObject obj;
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings->setValue(QStringLiteral("continue/entries"),
                             QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;
        if (error)
            *error = QStringLiteral("The Continue/progress store could not be committed.");
        return false;
    }

    // Constructed lazily inside a worker slot (see ensureSettings) so its thread affinity is the
    // worker thread, never the GUI thread that built this object.
    void ensureSettings() {
        if (m_settings)
            return;
        if (m_useIni)
            m_settings = std::make_unique<QSettings>(m_iniPath, QSettings::IniFormat);
        else
            m_settings = std::make_unique<QSettings>(m_org, m_app);
    }
    std::unique_ptr<QSettings> m_settings;
    QString m_org, m_app, m_iniPath;
    bool m_useIni = false;
#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
    bool m_failNextReceiptWrite = false;
#endif
};

class ProgressStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY healthChanged)
public:
    using RemoteCommitCallback = std::function<void(bool, const QString &)>;
    using TrackerProgressRemovalCallback =
        std::function<void(bool, int, const QString &)>;
    enum class TrackerImportApplyStatus : quint8 {
        Applied,
        AlreadyApplied,
        Stale,
        Failed
    };
    using TrackerImportCommitCallback = std::function<void(
        TrackerImportApplyStatus, const QVariantMap &, const QString &)>;

    explicit ProgressStore(QObject *parent = nullptr)
        : QObject(parent) {
        // Tagged (isolated Lanista test) sessions divert to a file under the tag's own
        // AppData root; untagged (the daily app) is unchanged — registry, same keys.
        const QString tagged = ProgressStoreDetail::progressStoreTaggedIniPath(
            QStringLiteral("progress-store.ini"));
        if (tagged.isEmpty()) {
            m_settings = std::make_unique<QSettings>(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"));
            m_writer = new ProgressDiskWriter(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"));
        } else {
            m_settings = std::make_unique<QSettings>(tagged, QSettings::IniFormat);
            m_writer = new ProgressDiskWriter(tagged);
        }
        load();
        m_lastPersistedSnapshot = snapshotHash();
        m_hasPersistedSnapshot = healthy();
        setupWriter();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file instead of
    // the app's registry scope, so harnesses stay hermetic and never touch the user's
    // real Continue data. Mirrors SearchHistoryStore's path constructor.
    explicit ProgressStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {
        load();
        m_lastPersistedSnapshot = snapshotHash();
        m_hasPersistedSnapshot = healthy();
        m_writer = new ProgressDiskWriter(iniPath);
        setupWriter();
    }

    ~ProgressStore() {
        // aboutToQuit usually flushed + stopped the writer thread first. If it did not (e.g. a
        // test with no app event loop, or an early tear-down), flush once more so destruction
        // implies a persisted disk (deterministic for tests that reload over the same INI), then
        // stop the thread. BlockingQueuedConnection inside flush() would deadlock against a
        // stopped thread, so flush() guards on isRunning(); the stop is separate and unconditional.
        flush();
        if (m_writerThread.isRunning()) {
            m_writerThread.quit();
            m_writerThread.wait();
        }
    }

    // Synchronously drain every queued write so the on-disk state reflects the current in-memory
    // map. Used by shutdown (aboutToQuit), the destructor, and any caller that needs read-your-
    // writes consistency against a fresh store over the same persistence location (e.g. tests).
    // No-op when the writer is absent or the thread is already stopped (post-shutdown teardown).
    void flush() {
        if (!m_writer || !m_writerThread.isRunning())
            return;
        // Post the latest in-memory map, then block until every queued writeSnapshot (FIFO,
        // this one included) has run on the worker thread. flushSync is an empty slot whose only
        // purpose is to be the BlockingQueuedConnection drain point after the queued writes.
        scheduleSave();
        QMetaObject::invokeMethod(m_writer, "flushSync", Qt::BlockingQueuedConnection);
    }

    int revision() const { return m_revision; }
    bool healthy(QString *error = nullptr) const {
        if (error)
            *error = m_loadError.isEmpty() ? m_persistenceError : m_loadError;
        return m_loadError.isEmpty() && m_persistenceError.isEmpty();
    }
    QString persistenceError() const {
        return m_loadError.isEmpty() ? m_persistenceError : m_loadError;
    }

    // Non-blocking local-owner receipt. Callers that need to hand an already
    // mutated Continue record to another durable subsystem use this instead
    // of `flush()`: it follows the existing writer FIFO and calls back only
    // when the latest in-memory snapshot is the one QSettings has committed.
    bool requestDurableReceipt(RemoteCommitCallback callback) {
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return false;
        }
        const quint64 requestId = m_nextRemoteRequest++;
        const QVariantHash snapshot = snapshotHash();
        m_pendingLocalReceipts.insert(requestId, snapshot);
        m_localReceiptCallbacks.insert(requestId, std::move(callback));
        postLocalReceiptSnapshot(requestId, snapshot);
        return true;
    }

#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
    // Test-only failure injection for the infrequent QSettings-owned
    // watched/last-season path. Continue writes remain on the normal writer.
    void forceWatchStatePersistenceFailureForTesting(bool enabled) {
        m_forceWatchStatePersistenceFailure = enabled;
    }
    void forceNextTrackerImportWriteFailureForTesting() {
        if (m_writer)
            QMetaObject::invokeMethod(m_writer, "failNextReceiptWriteForTesting",
                                      Qt::QueuedConnection);
    }
#endif

    // Native sync seam: complete raw Continue/progress state. Unlike recent(),
    // this does NOT group/dedupe series episodes, because sync identity is one
    // logical progress record per kind/id.
    QVariantList syncEntries() const {
        QStringList keys = m_map.keys();
        keys.sort();
        QVariantList out;
        out.reserve(keys.size());
        for (const QString &key : keys) {
            out.append(ProgressStoreDetail::withoutTrackerPrivateMetadata(
                m_map.value(key).toMap()));
        }
        return out;
    }

    // Local-only owner view for tracker delivery. Provenance remains inside
    // ProgressStore and is never included in portable account-sync rows.
    QVariantList deliveryEntries() const {
        QStringList keys = m_map.keys();
        keys.sort();
        QVariantList out;
        out.reserve(keys.size());
        for (const QString &key : keys)
            out.append(m_map.value(key).toMap());
        return out;
    }

    QVariantMap deliveryEntry(const QString &kind, const QString &id) const {
        return m_map.value(mapKey(kind, id)).toMap();
    }

    int trackerImportedProgressCount(const QString &providerKey,
                                    const QString &remoteAccountId) const {
        int count = 0;
        for (auto it = m_map.cbegin(); it != m_map.cend(); ++it) {
            const QVariantMap entry = it.value().toMap();
            if (ProgressStoreDetail::trackerProgressHasSource(
                    entry, providerKey, remoteAccountId)) {
                ++count;
            }
        }
        return count;
    }

    QVariantList trackerImportedProgressRemovalPreview(
        const QString &providerKey, const QString &remoteAccountId) const {
        QVariantList preview;
        if (providerKey.trimmed().isEmpty() || remoteAccountId.trimmed().isEmpty())
            return preview;

        QStringList keys = m_map.keys();
        keys.sort();
        for (const QString &key : keys) {
            const QVariantMap current = m_map.value(key).toMap();
            const QVariantList sources = ProgressStoreDetail::trackerProgressSources(current);
            QVariantMap removedSource;
            int remainingSourceCount = 0;
            for (const QVariant &value : sources) {
                const QVariantMap source = value.toMap();
                if (source.value(QStringLiteral("providerKey")).toString() == providerKey
                    && source.value(QStringLiteral("remoteAccountId")).toString()
                        == remoteAccountId) {
                    removedSource = source;
                } else {
                    ++remainingSourceCount;
                }
            }
            if (removedSource.isEmpty())
                continue;

            const bool localBasePresent = current.contains(
                    QStringLiteral("_trackerBasePresent"))
                ? current.value(QStringLiteral("_trackerBasePresent")).toBool()
                : current.value(QStringLiteral("_trackerOrigin")).toString()
                    != QLatin1String("tracker_import");
            const QVariantMap sourceEntry = removedSource.value(
                QStringLiteral("entry")).toMap();
            QString title = sourceEntry.value(QStringLiteral("title")).toString().trimmed();
            if (title.isEmpty())
                title = sourceEntry.value(QStringLiteral("caption")).toString().trimmed();
            if (title.isEmpty())
                title = QStringLiteral("Untitled media");

            const QString consequence = remainingSourceCount > 0
                ? QStringLiteral("Other tracker data remains; local Progress stays preserved.")
                : (localBasePresent
                    ? QStringLiteral("Your local Progress will be restored.")
                    : QStringLiteral("This tracker-imported Progress will be removed."));
            preview.append(QVariantMap{
                {QStringLiteral("title"), title.left(256)},
                {QStringLiteral("dataKind"), QStringLiteral("Progress")},
                {QStringLiteral("consequence"), consequence}});
        }
        return preview;
    }

    bool trackerProgressSourceRemovalSuppressed(const QString &providerKey,
                                                const QString &remoteAccountId) const {
        if (providerKey.isEmpty() || remoteAccountId.isEmpty())
            return false;
        return m_trackerProgressSuppressedSources.contains(
            ProgressStoreDetail::trackerProgressSourceIdentityKey(
                providerKey, remoteAccountId));
    }

    bool removeTrackerImportedProgressAsync(const QString &providerKey,
                                            const QString &remoteAccountId,
                                            TrackerProgressRemovalCallback callback) {
        const auto finish = [&callback](bool removed, int count, const QString &error) {
            if (callback)
                callback(removed, count, error);
        };
        if (!healthy()) {
            finish(false, 0, persistenceError());
            return false;
        }
        if (providerKey.trimmed().isEmpty() || remoteAccountId.trimmed().isEmpty()
            || providerKey != providerKey.trimmed()
            || remoteAccountId != remoteAccountId.trimmed()
            || providerKey.contains(QLatin1Char('\0'))
            || remoteAccountId.contains(QLatin1Char('\0'))) {
            finish(false, 0, QStringLiteral("Tracker Progress source identity is invalid."));
            return false;
        }
        if (m_trackerProgressRemovalPending || !m_pendingRemote.isEmpty()
            || !m_pendingTrackerImportOperations.isEmpty()) {
            finish(false, 0, QStringLiteral(
                "Tracker Progress is still changing; retry removal after its current write completes."));
            return false;
        }

        struct RemovalChange {
            QString key;
            QVariantMap previous;
            QVariantMap replacement;
            bool remove = false;
        };
        QList<RemovalChange> changes;
        int removedCount = 0;
        for (auto it = m_map.cbegin(); it != m_map.cend(); ++it) {
            const QVariantMap entry = it.value().toMap();
            const QVariantList sources = ProgressStoreDetail::trackerProgressSources(entry);
            if (!ProgressStoreDetail::trackerProgressHasSource(
                    entry, providerKey, remoteAccountId)) {
                continue;
            }
            ++removedCount;

            QVariantList remainingSources;
            bool removedActiveSource = false;
            for (int index = 0; index < sources.size(); ++index) {
                const QVariantMap source = sources.at(index).toMap();
                const bool matches = source.value(QStringLiteral("providerKey")).toString()
                        == providerKey
                    && source.value(QStringLiteral("remoteAccountId")).toString()
                        == remoteAccountId;
                if (matches) {
                    removedActiveSource = removedActiveSource || index == sources.size() - 1;
                    continue;
                }
                remainingSources.append(source);
            }

            RemovalChange change;
            change.key = it.key();
            change.previous = entry;
            if (!remainingSources.isEmpty()) {
                QVariantMap replacement = removedActiveSource
                    ? remainingSources.last().toMap().value(QStringLiteral("entry")).toMap()
                    : entry;
                replacement = ProgressStoreDetail::withoutTrackerImportSourceMetadata(
                    std::move(replacement));
                const QVariantMap activeSource = remainingSources.last().toMap();
                replacement.insert(QStringLiteral("_trackerOrigin"),
                                   QStringLiteral("tracker_import"));
                replacement.insert(QStringLiteral("_trackerProviderKey"),
                    activeSource.value(QStringLiteral("providerKey")).toString());
                replacement.insert(QStringLiteral("_trackerRemoteAccountId"),
                    activeSource.value(QStringLiteral("remoteAccountId")).toString());
                replacement.insert(QStringLiteral("_trackerSources"), remainingSources);
                const bool basePresent = entry.value(QStringLiteral("_trackerBasePresent")).toBool();
                replacement.insert(QStringLiteral("_trackerBasePresent"), basePresent);
                if (basePresent)
                    replacement.insert(QStringLiteral("_trackerBaseEntry"),
                        entry.value(QStringLiteral("_trackerBaseEntry")).toMap());
                else
                    replacement.remove(QStringLiteral("_trackerBaseEntry"));
                change.replacement = replacement;
            } else {
                const bool basePresent = entry.contains(QStringLiteral("_trackerBasePresent"))
                    ? entry.value(QStringLiteral("_trackerBasePresent")).toBool()
                    : entry.value(QStringLiteral("_trackerOrigin")).toString()
                        != QLatin1String("tracker_import");
                if (basePresent) {
                    const QVariantMap base = entry.value(QStringLiteral("_trackerBaseEntry")).toMap();
                    if (base.isEmpty()
                        || base.value(QStringLiteral("kind")).toString()
                            != entry.value(QStringLiteral("kind")).toString()
                        || base.value(QStringLiteral("id")).toString()
                            != entry.value(QStringLiteral("id")).toString()) {
                        finish(false, 0, QStringLiteral(
                            "The local Progress beneath this tracker import could not be restored safely."));
                        return false;
                    }
                    change.replacement = ProgressStoreDetail::withoutTrackerImportSourceMetadata(
                        base);
                } else {
                    change.remove = true;
                }
            }
            changes.append(change);
        }
        const QString suppressionKey =
            ProgressStoreDetail::trackerProgressSourceIdentityKey(providerKey,
                                                                   remoteAccountId);
        const bool wasSuppressed = m_trackerProgressSuppressedSources.contains(suppressionKey);
        if (changes.isEmpty() && wasSuppressed) {
            finish(true, 0, QString());
            return true;
        }

        m_trackerProgressSuppressedSources.insert(suppressionKey, QVariantMap{
            {QStringLiteral("providerKey"), providerKey},
            {QStringLiteral("remoteAccountId"), remoteAccountId}});
        m_trackerProgressRemovalPending = true;
        for (const RemovalChange &change : changes) {
            if (change.remove)
                m_map.remove(change.key);
            else
                m_map.insert(change.key, change.replacement);
        }
        ++m_revision;
        emit changed();
        emit localMutationChanged();

        QPointer<ProgressStore> progress(this);
        requestDurableReceipt(
            [progress, changes = std::move(changes), removedCount, suppressionKey,
             wasSuppressed,
             callback = std::move(callback)](bool committed, const QString &error) mutable {
                if (!progress) {
                    if (callback)
                        callback(false, 0, QStringLiteral(
                            "The active profile changed before tracker Progress removal completed."));
                    return;
                }
                progress->m_trackerProgressRemovalPending = false;
                if (!committed) {
                    for (const RemovalChange &change : changes) {
                        const bool replacementStillPresent = change.remove
                            ? !progress->m_map.contains(change.key)
                            : progress->m_map.value(change.key).toMap() == change.replacement;
                        if (replacementStillPresent)
                            progress->m_map.insert(change.key, change.previous);
                    }
                    if (!wasSuppressed)
                        progress->m_trackerProgressSuppressedSources.remove(suppressionKey);
                    ++progress->m_revision;
                    emit progress->changed();
                    emit progress->localMutationChanged();
                    if (callback)
                        callback(false, 0, error);
                    return;
                }
                if (callback)
                    callback(true, removedCount, QString());
            });
        return true;
    }

    static QString trackerImportRevisionToken(const QVariantMap &entry) {
        const QByteArray canonical = QJsonDocument(
            QJsonObject::fromVariantMap(entry)).toJson(QJsonDocument::Compact);
        return QString::fromLatin1(QCryptographicHash::hash(
            canonical, QCryptographicHash::Sha256).toHex());
    }

    bool deliveryEntryDurable(const QString &kind, const QString &id) const {
        if (!healthy() || !m_hasPersistedSnapshot)
            return false;
        const QString key = mapKey(kind, id);
        const bool currentPresent = m_map.contains(key);
        if (currentPresent != m_lastPersistedSnapshot.contains(key))
            return false;
        return !currentPresent
            || m_map.value(key).toMap() == m_lastPersistedSnapshot.value(key).toMap();
    }

    bool deliverySnapshotDurable() const {
        return healthy() && m_hasPersistedSnapshot
            && m_lastPersistedSnapshot == snapshotHash();
    }

    QHash<QString, int> syncWatchedMarks() const {
        const QString prefix =
            QStringLiteral("video/watchedMark/");
        QHash<QString, int> out;

        const QStringList keys = m_settings->allKeys();
        for (const QString &key : keys) {
            if (!key.startsWith(prefix))
                continue;

            const QString id = key.mid(prefix.size());
            if (id.isEmpty())
                continue;

            bool ok = false;
            const int mark =
                m_settings->value(key).toInt(&ok);
            if (ok && (mark == -1 || mark == 1))
                out.insert(id, mark);
        }

        return out;
    }

    QHash<QString, qint64> syncWatchedMarkActionTimes() const {
        const QString prefix =
            QStringLiteral("video/watchedMarkActionAt/");
        QHash<QString, qint64> out;

        const QStringList keys = m_settings->allKeys();
        for (const QString &key : keys) {
            if (!key.startsWith(prefix))
                continue;

            const QString id = key.mid(prefix.size());
            bool ok = false;
            const qint64 actionAtMs = m_settings->value(key).toLongLong(&ok);
            if (!id.isEmpty() && ok && actionAtMs > 0)
                out.insert(id, actionAtMs);
        }

        return out;
    }

    QHash<QString, bool> syncWatchedMarkManualStates() const {
        const QHash<QString, int> marks = syncWatchedMarks();
        QHash<QString, bool> out;
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
            out.insert(it.key(), watchedMarkIsManual(it.key()));
        return out;
    }

    QHash<QString, int> syncLastSeasons() const {
        const QString prefix =
            QStringLiteral("video/lastSeason/");
        QHash<QString, int> out;

        const QStringList keys = m_settings->allKeys();
        for (const QString &key : keys) {
            if (!key.startsWith(prefix))
                continue;

            const QString seriesId =
                key.mid(prefix.size());
            if (seriesId.isEmpty())
                continue;

            bool ok = false;
            const int season =
                m_settings->value(key).toInt(&ok);
            if (ok && season > 0)
                out.insert(seriesId, season);
        }

        return out;
    }

    // Upsert one resume entry, persist it, and refresh the Continue row. Use for lifecycle
    // writes (open / stop / forget, and the player's stop / stream-death / playback-failure /
    // episode-advance / EOF sites) where the visible Continue data genuinely changes. The payload
    // comes from QML as a plain JS object; `id` + `kind` identify it. Reading progress is
    // per-chapter and never auto-dropped (finishing a chapter ≠ finishing the series — use
    // forget() for an explicit "remove from Continue").
    Q_INVOKABLE void record(const QVariantMap &entry) {
        if (persist(entry)) {
            bump();
            emit localMutationChanged();
        }
    }
    // Persist progress for crash-resume WITHOUT refreshing the Continue row. The player's 5s
    // playback tick calls this: emitting changed() every 5s re-rendered every Continue tile
    // (recent() re-sorts/re-dedupes the whole map on each revision bump) and was the proven
    // video-stutter source (2026-07-29 ProgressStore isolation A/B: +100 output drops/60s with
    // changed() firing -> +3 with it suppressed, eyes smooth). The Continue row refreshes on the
    // next lifecycle write (stop / stream-death / playback-failure / episode-advance / EOF), which
    // is when its visible data actually moves.
    // The write itself runs on the background writer thread (see WRITE POLICY above).
    Q_INVOKABLE void recordSilent(const QVariantMap &entry) {
        persist(entry);
    }

    // Recent entries, newest first. kind "" → all kinds; limit <= 0 → no cap.
    Q_INVOKABLE QVariantList recent(const QString &kind = QString(), int limit = 0) const {
        QVariantList out;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            const QVariantMap rec = it.value().toMap();
            if (!kind.isEmpty() && rec.value(QStringLiteral("kind")).toString() != kind)
                continue;
            out.append(ProgressStoreDetail::withoutTrackerPrivateMetadata(rec));
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("updatedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("updatedAt")).toLongLong();
        });

        QHash<QString, int> grouped;
        QVariantList deduped;
        for (const QVariant &entry : out) {
            const QVariantMap rec = entry.toMap();
            const QString group = continueGroupKey(rec);
            if (!grouped.contains(group)) {
                grouped.insert(group, deduped.size());
                deduped.append(entry);
                continue;
            }

            const int index = grouped.value(group);
            if (shouldPreferContinueCandidate(deduped.at(index).toMap(), rec))
                deduped[index] = entry;
        }
        std::sort(deduped.begin(), deduped.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("updatedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("updatedAt")).toLongLong();
        });
        out = deduped;

        if (limit > 0 && out.size() > limit)
            out = out.mid(0, limit);
        return out;
    }

    // Explicit removal — the "remove from Continue" affordance (the ✕ on a tile).
    // A Continue tile is one row PER GROUP: recent() collapses every episode of a series
    // into a single tile (see continueGroupKey). So forgetting a tile must drop the WHOLE
    // group — every episode of that series — not just the one episode the tile happened to
    // display. Removing a single episode would leave its siblings behind, and the next
    // recent() would re-surface an earlier one, so the show reappears. For movies and manga
    // the group IS the entry itself, so this stays a one-for-one removal.
    Q_INVOKABLE void forget(const QString &kind, const QString &id) {
        if (kind.isEmpty() || id.isEmpty())
            return;
        if (!healthy())
            return;
        QVariantMap probe;
        probe.insert(QStringLiteral("kind"), kind);
        probe.insert(QStringLiteral("id"), id);
        const QString targetGroup = continueGroupKey(probe);

        QStringList doomed;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            if (continueGroupKey(it.value().toMap()) == targetGroup)
                doomed.append(it.key());
        }
        if (doomed.isEmpty())
            return;
        const QString watchedId = seriesRootId(id);
        const QString watchedKey = QStringLiteral("video/watchedMark/") + watchedId;
        const bool hadWatchedMark = m_settings->contains(watchedKey);
        bool watchedMarkRemoved = true;
        if (hadWatchedMark) {
            watchedMarkRemoved = removeWatchMarkSettings(watchedId);
            if (!watchedMarkRemoved)
                return;
        }
        for (const QString &key : doomed)
            m_map.remove(key);
        scheduleSave();
        emit syncDirty();
        bump();
        if (hadWatchedMark && watchedMarkRemoved)
            emit watchStateChanged();
        emit localMutationChanged();
    }

    Q_INVOKABLE QVariantMap get(const QString &kind, const QString &id) const {
        return ProgressStoreDetail::withoutTrackerPrivateMetadata(
            m_map.value(mapKey(kind, id)).toMap());
    }

    // Whole-kind purge (catalogue-independence Slice 5, 2026-08-20): unlike forget(),
    // which drops one series' GROUP (kind+id) as the "remove from Continue" affordance,
    // this drops EVERY record of a kind outright, no grouping. Built for
    // TankobanChapterMigration's one-time removal of every kind:"manga" chapter-progress
    // record when the WC-era chapter lane is deleted; no other caller is expected to
    // need a whole-kind purge. Returns the count removed.
    Q_INVOKABLE int purgeKind(const QString &kind) {
        if (kind.isEmpty())
            return 0;
        if (!healthy())
            return 0;
        QStringList doomed;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            if (it.value().toMap().value(QStringLiteral("kind")).toString() == kind)
                doomed.append(it.key());
        }
        if (doomed.isEmpty())
            return 0;
        for (const QString &key : doomed)
            m_map.remove(key);
        scheduleSave();
        emit syncDirty();
        bump();
        emit localMutationChanged();
        return doomed.size();
    }

    Q_INVOKABLE int lastSeason(const QString &seriesId) const {
        if (seriesId.isEmpty())
            return -1;
        return m_settings->value(QStringLiteral("video/lastSeason/") + seriesId, -1).toInt();
    }

    Q_INVOKABLE void rememberLastSeason(const QString &seriesId, int season) {
        if (seriesId.isEmpty() || season <= 0 || !healthy())
            return;
        const QString key = QStringLiteral("video/lastSeason/") + seriesId;
        if (m_settings->value(key, -1).toInt() == season)
            return;
        if (syncWatchSetting(key, season, false)) {
            bump();
            emit watchStateChanged();
        }
    }

    // ---- manual watched override (Library stage 2, spec §4.3) ----
    // Tri-state per series-root (or movie id): 1 = marked watched, -1 = marked
    // UNwatched (manual always wins over auto), 0 = no mark (auto rules apply).
    // Persisted beside lastSeason as plain settings keys; forget() clears it so
    // "remove from Continue" never leaves a ghost mark.
    Q_INVOKABLE int watchedMark(const QString &id) const {
        if (id.isEmpty()) return 0;
        return m_settings->value(QStringLiteral("video/watchedMark/") + seriesRootId(id), 0).toInt();
    }
    Q_INVOKABLE qint64 watchedMarkActionAt(const QString &id) const {
        if (id.isEmpty()) return 0;
        bool ok = false;
        const qint64 actionAtMs = m_settings->value(
            watchedMarkActionKey(seriesRootId(id))).toLongLong(&ok);
        return ok && actionAtMs > 0 ? actionAtMs : 0;
    }
    Q_INVOKABLE bool watchedMarkIsManual(const QString &id) const {
        if (id.isEmpty()) return false;
        const QString watchedId = seriesRootId(id);
        if (!m_settings->contains(QStringLiteral("video/watchedMark/") + watchedId))
            return false;
        // Existing user marks predate provenance and remain manual. Only a
        // provider-imported current state writes an explicit false value.
        return m_settings->value(watchedMarkManualKey(watchedId), true).toBool();
    }
    Q_INVOKABLE void setWatchedMark(const QString &id, bool watched) {
        if (id.isEmpty() || !healthy()) return;
        const QString watchedId = seriesRootId(id);
        const QString key = QStringLiteral("video/watchedMark/") + watchedId;
        const int mark = watched ? 1 : -1;
        if (m_settings->value(key, 0).toInt() == mark
            && watchedMarkIsManual(watchedId))
            return;
        if (syncWatchMarkSettings(
                watchedId,
                mark,
                QDateTime::currentMSecsSinceEpoch(),
                true)) {
            bump();
            emit watchStateChanged();
        }
    }
    Q_INVOKABLE void clearWatchedMark(const QString &id) {
        if (id.isEmpty() || !healthy()) return;
        const QString watchedId = seriesRootId(id);
        const QString key = QStringLiteral("video/watchedMark/") + watchedId;
        if (!m_settings->contains(key))
            return;
        if (removeWatchMarkSettings(watchedId)) {
            bump();
            emit watchStateChanged();
        }
    }

    bool applySyncedWatchedMark(
        const QString &id,
        int mark,
        qint64 actionAtMs = 0,
        bool resolvedRemoteWinner = false,
        bool manual = false) {
        if (!healthy()
            || id.isEmpty()
            || (mark != -1 && mark != 1)
            || actionAtMs < 0) {
            return false;
        }

        const QString watchedId = seriesRootId(id);
        const QString key = QStringLiteral("video/watchedMark/") + watchedId;

        bool ok = false;
        const int current =
            m_settings->value(key).toInt(&ok);
        bool currentActionOk = false;
        const qint64 currentActionAtMs = m_settings->value(
            watchedMarkActionKey(watchedId)).toLongLong(&currentActionOk);

        // Watch state is an ordinary mutable sync record, but its payload
        // carries the real user action time. A newer arrival/HLC must not
        // reverse a later watched decision, and a legacy record with no
        // action time cannot manufacture an order over a known action. Equal
        // action times are a stable tie so two devices converge without a
        // ping-pong write. Deletes remain a separate, authoritative reset.
        if (ok && (current == -1 || current == 1)) {
            if (currentActionOk && currentActionAtMs > 0) {
                // A selected Core record settles an equal action-time (or
                // legacy unknown-time) tie, but it must not let an older
                // real action undo the owner state already made durable.
                if (actionAtMs == 0 || actionAtMs < currentActionAtMs
                    || (!resolvedRemoteWinner && actionAtMs == currentActionAtMs))
                    return true;
            } else if (actionAtMs == 0 && !resolvedRemoteWinner) {
                return true;
            }
        }
        if (!resolvedRemoteWinner && ok && current == mark
            && ((actionAtMs > 0 && currentActionOk && currentActionAtMs == actionAtMs)
                || (actionAtMs == 0 && !currentActionOk)))
            return true;

        if (!syncWatchMarkSettings(watchedId, mark, actionAtMs, manual))
            return false;
        bump();
        return true;
    }

    bool removeSyncedWatchedMark(
        const QString &id) {
        if (!healthy() || id.isEmpty())
            return false;

        const QString watchedId = seriesRootId(id);
        const QString key = QStringLiteral("video/watchedMark/") + watchedId;
        if (!m_settings->contains(key)
            && !m_settings->contains(watchedMarkActionKey(watchedId)))
            return true;

        bool ok = false;
        const int current =
            m_settings->value(key).toInt(&ok);
        if (!removeWatchMarkSettings(watchedId))
            return false;

        if (ok && (current == -1 || current == 1))
            bump();
        return true;
    }

    bool applySyncedLastSeason(
        const QString &seriesId,
        int season) {
        if (!healthy() || seriesId.isEmpty() || season <= 0)
            return false;

        const QString key =
            QStringLiteral("video/lastSeason/")
            + seriesId;

        bool ok = false;
        const int current =
            m_settings->value(key).toInt(&ok);
        if (ok && current == season)
            return true;

        if (!syncWatchSetting(key, season, false))
            return false;
        bump();
        return true;
    }

    bool removeSyncedLastSeason(
        const QString &seriesId) {
        if (!healthy() || seriesId.isEmpty())
            return false;

        const QString key =
            QStringLiteral("video/lastSeason/")
            + seriesId;
        if (!m_settings->contains(key))
            return true;

        bool ok = false;
        const int current =
            m_settings->value(key).toInt(&ok);
        if (!syncWatchSetting(key, QVariant(), true))
            return false;

        if (ok && current > 0)
            bump();
        return true;
    }

    // ---- native remote-import seam (sync) ----
    // These preserve the incoming durable record exactly instead of routing through
    // record(), which intentionally stamps a fresh updatedAt for a NEW local user
    // action. They emit changed() so existing Continue/QML bindings react once, but
    // deliberately do not emit syncDirty(): remote import is not a new local mutation.
    bool applySyncedEntry(const QVariantMap &entry) {
        if (!healthy())
            return false;
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QString id   = entry.value(QStringLiteral("id")).toString();
        if (kind.isEmpty() || id.isEmpty())
            return false;

        QVariantMap exact = ProgressStoreDetail::withoutTrackerImportSourceMetadata(entry);
        exact.insert(QStringLiteral("kind"), kind);
        exact.insert(QStringLiteral("id"), id);
        exact.insert(QStringLiteral("_trackerOrigin"), QStringLiteral("account_sync"));

        const QString key = mapKey(kind, id);
        if (m_map.value(key).toMap() == exact)
            return true;

        m_map.insert(key, exact);
        scheduleSave();
        bump();
        emit syncedEntryApplied(kind, id);
        return true;
    }

    // Asynchronous remote-owner seam. The target snapshot is written by the
    // dedicated writer and the callback fires only after QSettings::sync()
    // reports success. The in-memory owner is published at that receipt, so a
    // sync cursor never acknowledges an owner that only exists in RAM.
    bool applySyncedEntryAsync(const QVariantMap &entry,
                               RemoteCommitCallback callback) {
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return false;
        }

        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (kind.isEmpty() || id.isEmpty()) {
            if (callback)
                callback(false, QStringLiteral("The Continue/progress record identity is invalid."));
            return false;
        }

        const QString key = mapKey(kind, id);
        const QVariantHash baseSnapshot = snapshotHash();
        QVariantHash target = snapshotHash();
        QVariantMap exact = ProgressStoreDetail::withoutTrackerImportSourceMetadata(entry);
        exact.insert(QStringLiteral("kind"), kind);
        exact.insert(QStringLiteral("id"), id);
        exact.insert(QStringLiteral("_trackerOrigin"), QStringLiteral("account_sync"));
        target.insert(key, exact);

        // Even an idempotent remote winner goes through the writer receipt:
        // the sync layer must verify the backing store before it advances its
        // cursor, including when the in-memory owner already matches.
        PendingRemote pending;
        pending.requestId = m_nextRemoteRequest++;
        pending.key = key;
        pending.kind = kind;
        pending.id = id;
        pending.target = target;
        pending.remoteEntry = exact;
        pending.baseSnapshot = baseSnapshot;
        pending.baseKeyPresent = m_map.contains(key);
        if (pending.baseKeyPresent)
            pending.baseEntry = m_map.value(key).toMap();
        pending.callback = std::move(callback);
        m_pendingRemote.insert(pending.requestId, pending);
        postRemoteSnapshot(pending.requestId, pending.target);
        return true;
    }

    // Tracker import has a separate, effect-bound receipt in the same persisted
    // Continue blob as the exact Progress row. It never stamps account_sync,
    // emits syncDirty(), or crosses the native completion/History boundary.
    bool applyTrackerImportedEntryAsync(
        const QString &operationId,
        const QString &effectDigest,
        const QString &kind,
        const QString &id,
        bool expectedPresent,
        const QString &expectedRevisionToken,
        qint64 expectedUpdatedAt,
        double expectedProgress,
        bool expectedWatched,
        const QVariantMap &importedEntry,
        TrackerImportCommitCallback callback)
    {
        const auto finish = [&callback](TrackerImportApplyStatus status,
                                        const QVariantMap &entry,
                                        const QString &error) {
            if (callback)
                callback(status, entry, error);
        };
        if (!healthy()) {
            finish(TrackerImportApplyStatus::Failed, {}, persistenceError());
            return false;
        }
        if (m_trackerProgressRemovalPending) {
            finish(TrackerImportApplyStatus::Stale, {},
                   QStringLiteral("Tracker Progress removal is still being committed."));
            return false;
        }
        if (operationId.trimmed().isEmpty() || effectDigest.size() != 64
            || kind.trimmed().isEmpty() || id.trimmed().isEmpty()
            || (expectedPresent && expectedRevisionToken.size() != 64)
            || (!expectedPresent && !expectedRevisionToken.isEmpty())
            || expectedUpdatedAt < 0 || !std::isfinite(expectedProgress)
            || expectedProgress < 0.0 || expectedProgress > 1.0
            || importedEntry.value(QStringLiteral("kind")).toString() != kind
            || importedEntry.value(QStringLiteral("id")).toString() != id
            || importedEntry.value(QStringLiteral("_trackerProviderKey")).toString().trimmed().isEmpty()
            || importedEntry.value(QStringLiteral("_trackerProviderKey")).toString()
                != importedEntry.value(QStringLiteral("_trackerProviderKey")).toString().trimmed()
            || importedEntry.value(QStringLiteral("_trackerRemoteAccountId")).toString().trimmed().isEmpty()
            || importedEntry.value(QStringLiteral("_trackerRemoteAccountId")).toString()
                != importedEntry.value(QStringLiteral("_trackerRemoteAccountId")).toString().trimmed()
            || importedEntry.value(QStringLiteral("_trackerProviderKey")).toString().contains(QLatin1Char('\0'))
            || importedEntry.value(QStringLiteral("_trackerRemoteAccountId")).toString().contains(QLatin1Char('\0'))
            || !std::isfinite(importedEntry.value(QStringLiteral("progress")).toDouble())
            || importedEntry.value(QStringLiteral("progress")).toDouble() < 0.0
            || importedEntry.value(QStringLiteral("progress")).toDouble() > 1.0) {
            finish(TrackerImportApplyStatus::Failed, {},
                   QStringLiteral("The exact tracker Progress effect is invalid."));
            return false;
        }

        const auto receipt = m_trackerImportReceipts.constFind(operationId);
        if (receipt != m_trackerImportReceipts.cend()) {
            if (receipt->effectDigest != effectDigest || receipt->kind != kind
                || receipt->id != id) {
                finish(TrackerImportApplyStatus::Failed, {},
                       QStringLiteral("The tracker import operation ID is bound to another effect."));
                return false;
            }
            finish(TrackerImportApplyStatus::AlreadyApplied, receipt->resultingEntry, QString());
            return true;
        }

        const auto pendingOperation = m_pendingTrackerImportOperations.constFind(operationId);
        if (pendingOperation != m_pendingTrackerImportOperations.cend()) {
            finish(TrackerImportApplyStatus::Failed, {},
                   pendingOperation->second == effectDigest
                       ? QStringLiteral("This tracker Progress operation is already being written.")
                       : QStringLiteral("The tracker import operation ID is already bound to another effect."));
            return false;
        }

        const QString key = mapKey(kind, id);
        const bool currentPresent = m_map.contains(key);
        const QVariantMap current = m_map.value(key).toMap();
        const double currentProgress = current.value(QStringLiteral("progress")).toDouble();
        const bool expectedMatches = currentPresent == expectedPresent
            && (!expectedPresent
                || (trackerImportRevisionToken(current) == expectedRevisionToken
                    && current.value(QStringLiteral("updatedAt")).toLongLong() == expectedUpdatedAt
                    && qFuzzyCompare(currentProgress + 1.0, expectedProgress + 1.0)
                    && current.value(QStringLiteral("watched")).toBool() == expectedWatched));
        if (!expectedMatches) {
            finish(TrackerImportApplyStatus::Stale, {},
                   QStringLiteral("Colosseum Progress changed after the tracker preview."));
            return true;
        }

        const QString providerKey = importedEntry.value(
            QStringLiteral("_trackerProviderKey")).toString();
        const QString remoteAccountId = importedEntry.value(
            QStringLiteral("_trackerRemoteAccountId")).toString();
        QVariantList sources = ProgressStoreDetail::trackerProgressSources(current);
        const bool currentIsTrackerImport = current.value(QStringLiteral("_trackerOrigin")).toString()
            == QLatin1String("tracker_import");
        if (currentPresent && currentIsTrackerImport && sources.isEmpty()) {
            finish(TrackerImportApplyStatus::Failed, {}, QStringLiteral(
                "The existing tracker Progress source attribution is incomplete."));
            return false;
        }

        bool basePresent = false;
        QVariantMap baseEntry;
        if (currentPresent && currentIsTrackerImport) {
            basePresent = current.contains(QStringLiteral("_trackerBasePresent"))
                && current.value(QStringLiteral("_trackerBasePresent")).toBool();
            baseEntry = current.value(QStringLiteral("_trackerBaseEntry")).toMap();
            if (basePresent
                && (baseEntry.isEmpty()
                    || baseEntry.value(QStringLiteral("kind")).toString() != kind
                    || baseEntry.value(QStringLiteral("id")).toString() != id)) {
                finish(TrackerImportApplyStatus::Failed, {}, QStringLiteral(
                    "The local Progress beneath this tracker import is incomplete."));
                return false;
            }
        } else if (currentPresent) {
            basePresent = true;
            baseEntry = ProgressStoreDetail::withoutTrackerImportSourceMetadata(current);
        }

        QVariantList retainedSources;
        for (const QVariant &sourceValue : sources) {
            const QVariantMap source = sourceValue.toMap();
            if (source.value(QStringLiteral("providerKey")).toString() == providerKey
                && source.value(QStringLiteral("remoteAccountId")).toString() == remoteAccountId) {
                continue;
            }
            retainedSources.append(source);
        }
        QVariantMap sourceEntry = ProgressStoreDetail::withoutTrackerImportSourceMetadata(
            importedEntry);
        sourceEntry.insert(QStringLiteral("kind"), kind);
        sourceEntry.insert(QStringLiteral("id"), id);
        sourceEntry.remove(QStringLiteral("_trackerOrigin"));
        retainedSources.append(QVariantMap{
            {QStringLiteral("providerKey"), providerKey},
            {QStringLiteral("remoteAccountId"), remoteAccountId},
            {QStringLiteral("entry"), sourceEntry}});

        QVariantMap exact = sourceEntry;
        const QVariantMap activeSource = retainedSources.last().toMap();
        exact.insert(QStringLiteral("_trackerOrigin"), QStringLiteral("tracker_import"));
        exact.insert(QStringLiteral("_trackerProviderKey"),
                     activeSource.value(QStringLiteral("providerKey")).toString());
        exact.insert(QStringLiteral("_trackerRemoteAccountId"),
                     activeSource.value(QStringLiteral("remoteAccountId")).toString());
        exact.insert(QStringLiteral("_trackerSources"), retainedSources);
        exact.insert(QStringLiteral("_trackerBasePresent"), basePresent);
        if (basePresent)
            exact.insert(QStringLiteral("_trackerBaseEntry"), baseEntry);
        QVariantHash target = snapshotHash();
        target.insert(key, exact);
        TrackerImportReceipt persistedReceipt{effectDigest, kind, id, exact};
        QVariantMap receipts = target.value(
            ProgressStoreDetail::trackerImportReceiptSnapshotKey()).toMap();
        receipts.insert(operationId, receiptToVariant(persistedReceipt));
        target.insert(ProgressStoreDetail::trackerImportReceiptSnapshotKey(), receipts);

        PendingRemote pending;
        pending.requestId = m_nextRemoteRequest++;
        pending.key = key;
        pending.kind = kind;
        pending.id = id;
        pending.target = target;
        pending.remoteEntry = exact;
        pending.baseSnapshot = snapshotHash();
        pending.baseKeyPresent = currentPresent;
        pending.baseEntry = current;
        pending.trackerImport = true;
        pending.trackerImportOperationId = operationId;
        pending.trackerImportReceipt = persistedReceipt;
        pending.trackerImportCallback = std::move(callback);
        m_pendingTrackerImportOperations.insert(
            operationId, qMakePair(pending.requestId, effectDigest));
        m_pendingRemote.insert(pending.requestId, pending);
        postRemoteSnapshot(pending.requestId, pending.target);
        return true;
    }

    bool removeSyncedEntry(const QString &kind, const QString &id) {
        if (!healthy())
            return false;
        if (kind.isEmpty() || id.isEmpty())
            return false;

        const QString key = mapKey(kind, id);
        if (!m_map.remove(key))
            return true;

        scheduleSave();
        bump();
        return true;
    }

    bool removeSyncedEntryAsync(const QString &kind, const QString &id,
                                RemoteCommitCallback callback) {
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return false;
        }
        if (kind.isEmpty() || id.isEmpty()) {
            if (callback)
                callback(false, QStringLiteral("The Continue/progress record identity is invalid."));
            return false;
        }

        const QString key = mapKey(kind, id);
        const QVariantHash baseSnapshot = snapshotHash();
        QVariantHash target = snapshotHash();
        target.remove(key);

        // Verify the backing store even when the tombstone is already absent;
        // an in-memory no-op is not a durable owner acknowledgement.
        PendingRemote pending;
        pending.requestId = m_nextRemoteRequest++;
        pending.key = key;
        pending.kind = kind;
        pending.id = id;
        pending.target = target;
        pending.baseSnapshot = baseSnapshot;
        pending.baseKeyPresent = m_map.contains(key);
        if (pending.baseKeyPresent)
            pending.baseEntry = m_map.value(key).toMap();
        pending.callback = std::move(callback);
        m_pendingRemote.insert(pending.requestId, pending);
        postRemoteSnapshot(pending.requestId, pending.target);
        return true;
    }

signals:
    void changed();
    void durableSnapshotCurrent();
    void durableEntryCurrent(const QString &kind, const QString &id);
    void healthChanged();
    void persistenceFailed(const QString &error);
    void completionCrossed(const QString &kind, const QString &id, qint64 completedAtMs,
                           const QString &activityEventId, const QString &activitySessionId);
    // Remote-only import notification. Active readers may react to a synced
    // winner without treating ordinary local progress writes as imported resume.
    // Idempotent replay of the same winner does not emit it.
    void syncedEntryApplied(const QString &kind, const QString &id);
    // Fires for every LOCAL Continue/progress mutation, including the 5s
    // recordSilent() playback tick. It intentionally does not alter revision
    // or changed(), preserving the proven no-rerender silent path.
    void syncDirty();
    // Fires for local mutations that refresh the visible Continue row. Remote
    // owner imports intentionally emit no local signal.
    void localMutationChanged();
    // Narrow local signal for the portable watched/last-season owner. The
    // generic changed() signal also covers Continue progress and would make a
    // watch-state adapter export on every playback tick.
    void watchStateChanged();

private:
    struct TrackerImportReceipt {
        QString effectDigest;
        QString kind;
        QString id;
        QVariantMap resultingEntry;
    };

    struct PendingRemote {
        quint64 requestId = 0;
        QString key;
        QString kind;
        QString id;
        QVariantHash target;
        QVariantMap remoteEntry;
        QVariantHash baseSnapshot;
        bool baseKeyPresent = false;
        QVariantMap baseEntry;
        RemoteCommitCallback callback;
        bool trackerImport = false;
        bool trackerImportStale = false;
        QString trackerImportOperationId;
        TrackerImportReceipt trackerImportReceipt;
        TrackerImportCommitCallback trackerImportCallback;
    };

    static QString mapKey(const QString &kind, const QString &id) {
        return kind + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }

    static QString watchedMarkActionKey(const QString &id) {
        return QStringLiteral("video/watchedMarkActionAt/") + id;
    }

    static QString watchedMarkManualKey(const QString &id) {
        return QStringLiteral("video/watchedMarkManual/") + id;
    }

    bool syncWatchMarkSettings(
        const QString &id,
        int mark,
        qint64 actionAtMs,
        bool manual) {
        if (!m_settings || id.isEmpty() || (mark != -1 && mark != 1)
            || actionAtMs < 0) {
            return false;
        }

#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
        if (m_forceWatchStatePersistenceFailure) {
            handleWriterFailure(
                QStringLiteral("The watched/last-season owner could not be committed."));
            return false;
        }
#endif

        const QString markKey = QStringLiteral("video/watchedMark/") + id;
        const QString actionKey = watchedMarkActionKey(id);
        const QString manualKey = watchedMarkManualKey(id);
        const bool hadMark = m_settings->contains(markKey);
        const QVariant previousMark = m_settings->value(markKey);
        const bool hadAction = m_settings->contains(actionKey);
        const QVariant previousAction = m_settings->value(actionKey);
        const bool hadManual = m_settings->contains(manualKey);
        const QVariant previousManual = m_settings->value(manualKey);

        m_settings->setValue(markKey, mark);
        if (actionAtMs > 0)
            m_settings->setValue(actionKey, actionAtMs);
        else
            m_settings->remove(actionKey);
        m_settings->setValue(manualKey, manual);
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;

        if (hadMark)
            m_settings->setValue(markKey, previousMark);
        else
            m_settings->remove(markKey);
        if (hadAction)
            m_settings->setValue(actionKey, previousAction);
        else
            m_settings->remove(actionKey);
        if (hadManual)
            m_settings->setValue(manualKey, previousManual);
        else
            m_settings->remove(manualKey);
        m_settings->sync();
        handleWriterFailure(
            QStringLiteral("The watched/last-season owner could not be committed."));
        return false;
    }

    bool removeWatchMarkSettings(const QString &id) {
        if (!m_settings || id.isEmpty())
            return false;

#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
        if (m_forceWatchStatePersistenceFailure) {
            handleWriterFailure(
                QStringLiteral("The watched/last-season owner could not be committed."));
            return false;
        }
#endif

        const QString markKey = QStringLiteral("video/watchedMark/") + id;
        const QString actionKey = watchedMarkActionKey(id);
        const QString manualKey = watchedMarkManualKey(id);
        const bool hadMark = m_settings->contains(markKey);
        const QVariant previousMark = m_settings->value(markKey);
        const bool hadAction = m_settings->contains(actionKey);
        const QVariant previousAction = m_settings->value(actionKey);
        const bool hadManual = m_settings->contains(manualKey);
        const QVariant previousManual = m_settings->value(manualKey);

        m_settings->remove(markKey);
        m_settings->remove(actionKey);
        m_settings->remove(manualKey);
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;

        if (hadMark)
            m_settings->setValue(markKey, previousMark);
        if (hadAction)
            m_settings->setValue(actionKey, previousAction);
        if (hadManual)
            m_settings->setValue(manualKey, previousManual);
        m_settings->sync();
        handleWriterFailure(
            QStringLiteral("The watched/last-season owner could not be committed."));
        return false;
    }

    bool syncWatchSetting(const QString &key, const QVariant &value,
                          bool remove) {
        if (!m_settings)
            return false;

#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
        if (m_forceWatchStatePersistenceFailure) {
            handleWriterFailure(
                QStringLiteral("The watched/last-season owner could not be committed."));
            return false;
        }
#endif

        const bool hadPrevious = m_settings->contains(key);
        const QVariant previous = m_settings->value(key);
        if (remove)
            m_settings->remove(key);
        else
            m_settings->setValue(key, value);
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;

        // A remote owner apply must never publish an in-memory value when
        // the backing QSettings write failed. Restore the pre-apply value so
        // a retry or a reopened store sees the same owner state.
        if (hadPrevious)
            m_settings->setValue(key, previous);
        else
            m_settings->remove(key);
        handleWriterFailure(
            QStringLiteral("The watched/last-season owner could not be committed."));
        return false;
    }
    static QString seriesRootId(const QString &id) {
        if (id.count(QLatin1Char(':')) < 2)
            return id;
        const QStringList parts = id.split(QLatin1Char(':'));
        if (id.startsWith(QStringLiteral("tt")))
            return parts.value(0);
        return parts.value(0) + QLatin1Char(':') + parts.value(1);
    }
    static QString continueGroupKey(const QVariantMap &rec) {
        const QString kind = rec.value(QStringLiteral("kind")).toString();
        const QString id = rec.value(QStringLiteral("id")).toString();
        const QString groupId = kind == QStringLiteral("video") ? seriesRootId(id) : id;
        return mapKey(kind, groupId);
    }
    static bool shouldPreferContinueCandidate(const QVariantMap &current, const QVariantMap &candidate) {
        const bool currentWatched = current.value(QStringLiteral("watched")).toBool();
        const bool candidateWatched = candidate.value(QStringLiteral("watched")).toBool();
        if (currentWatched != candidateWatched)
            return !candidateWatched;
        return candidate.value(QStringLiteral("updatedAt")).toLongLong()
             > current.value(QStringLiteral("updatedAt")).toLongLong();
    }
    void bump() { ++m_revision; emit changed(); }

    // Post the current map to the background writer (non-blocking on this thread). The worker
    // serializes + syncs on its own thread. There is NO debounce/coalescing: every call enqueues
    // one full snapshot (a cheap shared QVariantHash copy). Bursts are rare (5s player tick + a
    // few user-driven lifecycle writes), so the cost is acceptable and each write is independent.
    // The GUI/render thread does no serialization and no QSettings::sync().
    void scheduleSave() {
        if (m_writer && healthy()) {
            QMetaObject::invokeMethod(m_writer, "writeSnapshot", Qt::QueuedConnection,
                                      Q_ARG(QVariantHash, snapshotHash()));
        }
    }

    QVariantHash snapshotHash() const {
        QVariantHash snapshot;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            snapshot.insert(it.key(), it.value().toMap());
        if (!m_trackerImportReceipts.isEmpty()) {
            QVariantMap receipts;
            for (auto it = m_trackerImportReceipts.constBegin();
                 it != m_trackerImportReceipts.constEnd(); ++it) {
                receipts.insert(it.key(), receiptToVariant(it.value()));
            }
            snapshot.insert(ProgressStoreDetail::trackerImportReceiptSnapshotKey(), receipts);
        }
        if (!m_trackerProgressSuppressedSources.isEmpty()) {
            QVariantMap removedSources;
            for (auto it = m_trackerProgressSuppressedSources.constBegin();
                 it != m_trackerProgressSuppressedSources.constEnd(); ++it) {
                removedSources.insert(it.key(), it.value());
            }
            snapshot.insert(ProgressStoreDetail::trackerProgressSourceRemovalSnapshotKey(),
                            removedSources);
        }
        return snapshot;
    }

    static QVariantMap receiptToVariant(const TrackerImportReceipt &receipt) {
        return {{QStringLiteral("effectDigest"), receipt.effectDigest},
                {QStringLiteral("kind"), receipt.kind},
                {QStringLiteral("id"), receipt.id},
                {QStringLiteral("resultingEntry"), receipt.resultingEntry}};
    }

    static std::optional<TrackerImportReceipt> receiptFromVariant(const QVariant &value) {
        if (!value.canConvert<QVariantMap>())
            return std::nullopt;
        const QVariantMap map = value.toMap();
        const QString digest = map.value(QStringLiteral("effectDigest")).toString();
        const QString kind = map.value(QStringLiteral("kind")).toString();
        const QString id = map.value(QStringLiteral("id")).toString();
        const QVariantMap entry = map.value(QStringLiteral("resultingEntry")).toMap();
        if (digest.size() != 64 || kind.isEmpty() || id.isEmpty()
            || entry.value(QStringLiteral("kind")).toString() != kind
            || entry.value(QStringLiteral("id")).toString() != id
            || entry.value(QStringLiteral("_trackerOrigin")).toString()
                != QLatin1String("tracker_import")) {
            return std::nullopt;
        }
        return TrackerImportReceipt{digest, kind, id, entry};
    }

    void replaceSnapshot(const QVariantHash &snapshot) {
        m_map.clear();
        m_trackerImportReceipts.clear();
        m_trackerProgressSuppressedSources.clear();
        const QString receiptKey = ProgressStoreDetail::trackerImportReceiptSnapshotKey();
        const QString removedSourcesKey =
            ProgressStoreDetail::trackerProgressSourceRemovalSnapshotKey();
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
            if (it.key() == receiptKey) {
                const QVariantMap receipts = it.value().toMap();
                for (auto receiptIt = receipts.constBegin();
                     receiptIt != receipts.constEnd(); ++receiptIt) {
                    const auto receipt = receiptFromVariant(receiptIt.value());
                    if (receipt)
                        m_trackerImportReceipts.insert(receiptIt.key(), *receipt);
                }
                continue;
            }
            if (it.key() == removedSourcesKey) {
                const QVariantMap removedSources = it.value().toMap();
                for (auto sourceIt = removedSources.constBegin();
                     sourceIt != removedSources.constEnd(); ++sourceIt) {
                    const QVariantMap source = sourceIt.value().toMap();
                    const QString providerKey = source.value(QStringLiteral("providerKey")).toString();
                    const QString remoteAccountId = source.value(QStringLiteral("remoteAccountId")).toString();
                    if (providerKey.trimmed().isEmpty() || remoteAccountId.trimmed().isEmpty()
                        || providerKey != providerKey.trimmed()
                        || remoteAccountId != remoteAccountId.trimmed()
                        || providerKey.contains(QLatin1Char('\0'))
                        || remoteAccountId.contains(QLatin1Char('\0'))
                        || sourceIt.key() != ProgressStoreDetail::trackerProgressSourceIdentityKey(
                            providerKey, remoteAccountId)) {
                        continue;
                    }
                    m_trackerProgressSuppressedSources.insert(sourceIt.key(), source);
                }
                continue;
            }
            m_map.insert(it.key(), it.value().toMap());
        }
    }

    void postRemoteSnapshot(quint64 requestId, const QVariantHash &snapshot) {
        if (!m_writer || !m_writerThread.isRunning()) {
            handleRemoteSnapshotFinished(requestId, false,
                                         QStringLiteral("The Continue/progress writer is unavailable."));
            return;
        }
        const bool queued = QMetaObject::invokeMethod(
            m_writer,
            "writeSnapshotWithReceipt",
            Qt::QueuedConnection,
            Q_ARG(quint64, requestId),
            Q_ARG(QVariantHash, snapshot));
        if (!queued)
            handleRemoteSnapshotFinished(
                requestId,
                false,
                QStringLiteral("The Continue/progress writer could not queue the owner snapshot."));
    }

    void postLocalReceiptSnapshot(quint64 requestId, const QVariantHash &snapshot) {
        if (!m_writer || !m_writerThread.isRunning()) {
            handleWriterSnapshotFinished(
                requestId, false,
                QStringLiteral("The Continue/progress writer is unavailable."));
            return;
        }
        const bool queued = QMetaObject::invokeMethod(
            m_writer,
            "writeSnapshotWithReceipt",
            Qt::QueuedConnection,
            Q_ARG(quint64, requestId),
            Q_ARG(QVariantHash, snapshot));
        if (!queued) {
            handleWriterSnapshotFinished(
                requestId, false,
                QStringLiteral("The Continue/progress writer could not queue the owner snapshot."));
        }
    }

    void handleWriterFailure(const QString &error) {
        if (m_persistenceError == error)
            return;
        m_persistenceError = error.isEmpty()
            ? QStringLiteral("The Continue/progress store could not be committed.")
            : error;
        emit healthChanged();
        emit persistenceFailed(m_persistenceError);
    }

    void handleWriterSnapshotWritten(const QVariantHash &snapshot) {
        m_lastPersistedSnapshot = snapshot;
        m_hasPersistedSnapshot = true;
        if (healthy() && snapshot == snapshotHash()) {
            const auto pending = m_trackerPendingEntries;
            m_trackerPendingEntries.clear();
            for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
                const QString key = mapKey(it.value().first, it.value().second);
                const QVariantMap current = m_map.value(key).toMap();
                if (current.isEmpty()
                    || snapshot.value(key).toMap() != current
                    || current.value(QStringLiteral("_trackerOrigin")).toString()
                        != QLatin1String("native_local")) {
                    continue;
                }
                emit durableEntryCurrent(it.value().first, it.value().second);
            }
            emit durableSnapshotCurrent();
        }
    }

    void handleRemoteSnapshotFinished(quint64 requestId, bool committed,
                                      const QString &error) {
        auto it = m_pendingRemote.find(requestId);
        if (it == m_pendingRemote.end())
            return;
        PendingRemote pending = it.value();
        m_pendingRemote.erase(it);
        const auto releaseTrackerOperation = [this, requestId, &pending] {
            if (!pending.trackerImport)
                return;
            const auto operation = m_pendingTrackerImportOperations.constFind(
                pending.trackerImportOperationId);
            if (operation != m_pendingTrackerImportOperations.cend()
                && operation->first == requestId)
                m_pendingTrackerImportOperations.remove(pending.trackerImportOperationId);
        };

        if (!committed) {
            handleWriterFailure(error);
            releaseTrackerOperation();
            if (pending.trackerImportCallback)
                pending.trackerImportCallback(TrackerImportApplyStatus::Failed, {}, persistenceError());
            else if (pending.callback)
                pending.callback(false, persistenceError());
            return;
        }

        if (!healthy()) {
            releaseTrackerOperation();
            if (pending.trackerImportCallback)
                pending.trackerImportCallback(TrackerImportApplyStatus::Failed, {}, persistenceError());
            else if (pending.callback)
                pending.callback(false, persistenceError());
            return;
        }

        // Local playback/user work may have landed while the worker was
        // writing. Rebase over the newest in-memory map, but only apply the
        // remote operation when its target record still matches the state that
        // existed when the remote write began. Unrelated local records remain
        // part of the final snapshot; a newer same-record local write wins.
        const QVariantHash current = snapshotHash();
        if (pending.baseSnapshot != current) {
            const bool keyUnchanged =
                m_map.contains(pending.key) == pending.baseKeyPresent
                && (!pending.baseKeyPresent
                    || m_map.value(pending.key).toMap()
                        == pending.baseEntry);
            if (pending.trackerImport) {
                pending.target = current;
                if (pending.trackerImportStale || !keyUnchanged) {
                    pending.trackerImportStale = true;
                } else {
                    pending.target.insert(pending.key, pending.remoteEntry);
                    QVariantMap receipts = current.value(
                        ProgressStoreDetail::trackerImportReceiptSnapshotKey()).toMap();
                    receipts.insert(pending.trackerImportOperationId,
                                    receiptToVariant(pending.trackerImportReceipt));
                    pending.target.insert(
                        ProgressStoreDetail::trackerImportReceiptSnapshotKey(), receipts);
                }
            } else {
                pending.target = current;
                if (keyUnchanged) {
                    if (!pending.remoteEntry.isEmpty())
                        pending.target.insert(pending.key, pending.remoteEntry);
                    else
                        pending.target.remove(pending.key);
                }
            }
            pending.baseSnapshot = current;
            m_pendingRemote.insert(requestId, pending);
            postRemoteSnapshot(requestId, pending.target);
            return;
        }

        const QVariantHash before = snapshotHash();
        replaceSnapshot(pending.target);
        const QVariantHash after = snapshotHash();

        if (before != after) {
            ++m_revision;
            emit changed();
            if (!pending.trackerImport && !pending.kind.isEmpty() && !pending.id.isEmpty()
                && pending.target.contains(pending.key))
                emit syncedEntryApplied(pending.kind, pending.id);
        }

        if (healthy() && m_hasPersistedSnapshot
            && m_lastPersistedSnapshot == snapshotHash()) {
            emit durableSnapshotCurrent();
        }

        releaseTrackerOperation();
        if (pending.trackerImportCallback) {
            if (pending.trackerImportStale) {
                pending.trackerImportCallback(
                    TrackerImportApplyStatus::Stale, {},
                    QStringLiteral("Colosseum Progress changed while the tracker write was pending."));
            } else {
                pending.trackerImportCallback(TrackerImportApplyStatus::Applied,
                                              pending.trackerImportReceipt.resultingEntry, QString());
            }
        } else if (pending.callback) {
            pending.callback(true, QString());
        }
    }

    void handleWriterSnapshotFinished(quint64 requestId, bool committed,
                                      const QString &error) {
        if (m_pendingRemote.contains(requestId)) {
            handleRemoteSnapshotFinished(requestId, committed, error);
            return;
        }
        auto it = m_pendingLocalReceipts.find(requestId);
        if (it == m_pendingLocalReceipts.end())
            return;
        QVariantHash snapshot = it.value();
        m_pendingLocalReceipts.erase(it);
        const RemoteCommitCallback callback = m_localReceiptCallbacks.take(requestId);
        if (!committed) {
            handleWriterFailure(error);
            if (callback)
                callback(false, persistenceError());
            return;
        }
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return;
        }
        const QVariantHash current = snapshotHash();
        if (snapshot != current) {
            m_pendingLocalReceipts.insert(requestId, current);
            m_localReceiptCallbacks.insert(requestId, callback);
            postLocalReceiptSnapshot(requestId, current);
            return;
        }
        if (callback)
            callback(true, QString());
    }

    // Move the writer onto its thread and arrange a final synchronous flush at shutdown so the
    // latest resume point always lands on disk before the process dies.
    void setupWriter() {
        m_writer->moveToThread(&m_writerThread);
        connect(&m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
        connect(m_writer, &ProgressDiskWriter::writeFailed,
                this, &ProgressStore::handleWriterFailure,
                Qt::QueuedConnection);
        connect(m_writer, &ProgressDiskWriter::snapshotWritten,
                this, &ProgressStore::handleWriterSnapshotWritten,
                Qt::QueuedConnection);
        connect(m_writer, &ProgressDiskWriter::snapshotFinished,
                this, &ProgressStore::handleWriterSnapshotFinished,
                Qt::QueuedConnection);
        if (qApp) {
            connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
                // Drain every queued write (and post the latest map first) so the final resume
                // point lands on disk, then stop the worker thread. flush() is a no-op if the
                // thread is already stopped, so this composes safely with the destructor's flush().
                flush();
                if (m_writerThread.isRunning()) {
                    m_writerThread.quit();
                    m_writerThread.wait();
                }
            });
        }
        m_writerThread.start();
    }

    // Mutate the map + schedule a background persist. Returns true if anything changed (the
    // caller decides whether to emit changed() — record() notifies, recordSilent() does not).
    // No signal here. Finished threshold matches Tankoban 2's proven StreamProgress::isFinished
    // (>= 90%): a film watched past 90% is "done" and drops off Continue. (TB2 advances a series
    // to the next episode instead of dropping — a future enhancement here; for now we drop.)
    bool persist(const QVariantMap &entry) {
        if (!healthy())
            return false;
        const QString activityEventId = entry.value(QStringLiteral("_trackerActivityEventId")).toString();
        const QString activitySessionId = entry.value(QStringLiteral("_trackerActivitySessionId")).toString();
        QVariantMap canonicalEntry = entry;
        canonicalEntry.remove(QStringLiteral("_trackerActivityEventId"));
        canonicalEntry.remove(QStringLiteral("_trackerActivitySessionId"));
        canonicalEntry = ProgressStoreDetail::withoutTrackerImportSourceMetadata(
            std::move(canonicalEntry));
        const QString kind = canonicalEntry.value(QStringLiteral("kind")).toString();
        const QString id   = canonicalEntry.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || kind.isEmpty())
            return false;
        const QString key = mapKey(kind, id);
        const QVariantMap previous = m_map.value(key).toMap();

        const double progress = canonicalEntry.value(QStringLiteral("progress")).toDouble();
        const bool isSeriesEpisode =
            kind == QStringLiteral("video") && id.count(QLatin1Char(':')) >= 2;
        if (kind == QStringLiteral("video") && progress >= 0.90 && !isSeriesEpisode) {
            // A vault id is "vault:<sha1>" — ONE colon — so every vault video, episodes
            // included, retires here; the mark is what survives the dropped resume record.
            // Guarded on the entry existing so the mark is written once at the crossing:
            // setWatchedMark bumps changed(), and a 5s recordSilent() cascade of that is the
            // proven video-stutter source.
            if (m_map.contains(key)) {
                emit completionCrossed(QStringLiteral("movie"), id,
                                       QDateTime::currentMSecsSinceEpoch(), activityEventId,
                                       activitySessionId);
                if (id.startsWith(QStringLiteral("vault:")))
                    setWatchedMark(id, true);
                m_map.remove(key);
                m_trackerPendingEntries.insert(key, qMakePair(kind, id));
                scheduleSave();
                emit syncDirty();
                return true;
            }
            return false;
        }

        QVariantMap rec = canonicalEntry;
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("kind"), kind);
        rec.insert(QStringLiteral("_trackerOrigin"), QStringLiteral("native_local"));
        if (isSeriesEpisode && progress >= 0.90) {
            if (!previous.value(QStringLiteral("watched")).toBool())
                emit completionCrossed(QStringLiteral("episode"), id,
                                       QDateTime::currentMSecsSinceEpoch(), activityEventId,
                                       activitySessionId);
            rec.insert(QStringLiteral("watched"), true);
        }
        rec.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(key, rec);
        m_trackerPendingEntries.insert(key, qMakePair(kind, id));
        scheduleSave();
        emit syncDirty();
        return true;
    }

    void load() {
        m_map.clear();
        m_trackerImportReceipts.clear();
        m_trackerProgressSuppressedSources.clear();
        const QByteArray blob =
            m_settings->value(QStringLiteral("continue/entries")).toByteArray();
        if (m_settings->status() != QSettings::NoError) {
            m_loadError = QStringLiteral("The Continue/progress persistence store could not be read.");
            return;
        }
        if (blob.isEmpty())
            return;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(blob, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            m_loadError = QStringLiteral("The Continue/progress persistence file is malformed.");
            return;
        }

        QHash<QString, QVariant> loaded;
        const QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (!it.value().isObject()) {
                m_loadError = QStringLiteral("A persisted Continue/progress record is malformed.");
                m_map.clear();
                m_trackerImportReceipts.clear();
                m_trackerProgressSuppressedSources.clear();
                return;
            }
            if (it.key() == ProgressStoreDetail::trackerImportReceiptSnapshotKey()) {
                const QJsonObject receiptObject = it.value().toObject();
                for (auto receiptIt = receiptObject.constBegin();
                     receiptIt != receiptObject.constEnd(); ++receiptIt) {
                    const auto receipt = receiptFromVariant(
                        receiptIt.value().toObject().toVariantMap());
                    if (!receipt || receiptIt.key().trimmed().isEmpty()) {
                        m_loadError = QStringLiteral("A persisted tracker Progress receipt is malformed.");
                        m_map.clear();
                        m_trackerImportReceipts.clear();
                        m_trackerProgressSuppressedSources.clear();
                        return;
                    }
                    m_trackerImportReceipts.insert(receiptIt.key(), *receipt);
                }
                continue;
            }
            if (it.key() == ProgressStoreDetail::trackerProgressSourceRemovalSnapshotKey()) {
                const QJsonObject removedSources = it.value().toObject();
                for (auto sourceIt = removedSources.constBegin();
                     sourceIt != removedSources.constEnd(); ++sourceIt) {
                    const QVariantMap source = sourceIt.value().toObject().toVariantMap();
                    const QString providerKey = source.value(QStringLiteral("providerKey")).toString();
                    const QString remoteAccountId = source.value(QStringLiteral("remoteAccountId")).toString();
                    if (providerKey.trimmed().isEmpty() || remoteAccountId.trimmed().isEmpty()
                        || providerKey != providerKey.trimmed()
                        || remoteAccountId != remoteAccountId.trimmed()
                        || providerKey.contains(QLatin1Char('\0'))
                        || remoteAccountId.contains(QLatin1Char('\0'))
                        || sourceIt.key() != ProgressStoreDetail::trackerProgressSourceIdentityKey(
                            providerKey, remoteAccountId)) {
                        m_loadError = QStringLiteral("A persisted tracker Progress removal record is malformed.");
                        m_map.clear();
                        m_trackerImportReceipts.clear();
                        m_trackerProgressSuppressedSources.clear();
                        return;
                    }
                    m_trackerProgressSuppressedSources.insert(sourceIt.key(), source);
                }
                continue;
            }
            const QVariantMap record = it.value().toObject().toVariantMap();
            const QString kind = record.value(QStringLiteral("kind")).toString();
            const QString id = record.value(QStringLiteral("id")).toString();
            if (kind.isEmpty() || id.isEmpty() || it.key() != mapKey(kind, id)) {
                m_loadError = QStringLiteral("A persisted Continue/progress record has invalid identity fields.");
                m_map.clear();
                m_trackerImportReceipts.clear();
                m_trackerProgressSuppressedSources.clear();
                return;
            }
            loaded.insert(it.key(), record);
        }
        m_map = loaded;
    }

    // GUI-thread QSettings: used ONLY for load() at startup and for the infrequent
    // lastSeason / watchedMark keys (rare user actions, never the 5s playback tick). The hot
    // continue/entries path is owned by the background writer's own QSettings instance.
    // A pointer (not a plain member) because which backing store to build — registry vs. a
    // tagged isolation file — is a runtime decision made in the constructor body; see
    // progressStoreTaggedIniPath() above and ProgressStore's default constructor.
    std::unique_ptr<QSettings> m_settings;
    QHash<QString, QVariant> m_map;   // "kind\x1fid" → entry map
    QHash<QString, TrackerImportReceipt> m_trackerImportReceipts;
    QHash<QString, QVariantMap> m_trackerProgressSuppressedSources;
    QHash<QString, QPair<QString, QString>> m_trackerPendingEntries;
    int m_revision = 0;
    QString m_loadError;
    QString m_persistenceError;
    quint64 m_nextRemoteRequest = 1;
    QHash<quint64, PendingRemote> m_pendingRemote;
    QHash<QString, QPair<quint64, QString>> m_pendingTrackerImportOperations;
    bool m_trackerProgressRemovalPending = false;
    QHash<quint64, QVariantHash> m_pendingLocalReceipts;
    QHash<quint64, RemoteCommitCallback> m_localReceiptCallbacks;
    QVariantHash m_lastPersistedSnapshot;
    bool m_hasPersistedSnapshot = false;
    ProgressDiskWriter *m_writer = nullptr;
    QThread m_writerThread;
#ifdef COLOSSEUM_PROGRESS_STORE_TESTING
    bool m_forceWatchStatePersistenceFailure = false;
#endif
};
