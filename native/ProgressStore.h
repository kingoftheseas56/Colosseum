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
#include <QJsonDocument>
#include <QDateTime>
#include <QThread>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <memory>
#include <algorithm>

namespace {
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
// CollectionStore.h are both #included into main.cpp, and two identically-named
// functions in unnamed namespaces would collide in that one translation unit.
inline QString progressStoreTaggedIniPath(const QString &storeFileName) {
    if (!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"))
        return QString();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + storeFileName;
}
}

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
        ensureSettings();   // created on the worker thread on first use (correct affinity)
        QJsonObject obj;
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings->setValue(QStringLiteral("continue/entries"),
                             QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings->sync();
    }
    void flushSync() {}   // shutdown/flush barrier (see ProgressStore::flush)

private:
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
};

class ProgressStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit ProgressStore(QObject *parent = nullptr)
        : QObject(parent) {
        // Tagged (isolated Lanista test) sessions divert to a file under the tag's own
        // AppData root; untagged (the daily app) is unchanged — registry, same keys.
        const QString tagged = progressStoreTaggedIniPath(QStringLiteral("progress-store.ini"));
        if (tagged.isEmpty()) {
            m_settings = std::make_unique<QSettings>(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"));
            m_writer = new ProgressDiskWriter(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"));
        } else {
            m_settings = std::make_unique<QSettings>(tagged, QSettings::IniFormat);
            m_writer = new ProgressDiskWriter(tagged);
        }
        load();
        setupWriter();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file instead of
    // the app's registry scope, so harnesses stay hermetic and never touch the user's
    // real Continue data. Mirrors SearchHistoryStore's path constructor.
    explicit ProgressStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {
        load();
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

    // Native sync seam: complete raw Continue/progress state. Unlike recent(),
    // this does NOT group/dedupe series episodes, because sync identity is one
    // logical progress record per kind/id.
    QVariantList syncEntries() const {
        QStringList keys = m_map.keys();
        keys.sort();
        QVariantList out;
        out.reserve(keys.size());
        for (const QString &key : keys)
            out.append(m_map.value(key).toMap());
        return out;
    }

    // Upsert one resume entry, persist it, and refresh the Continue row. Use for lifecycle
    // writes (open / stop / forget, and the player's stop / stream-death / playback-failure /
    // episode-advance / EOF sites) where the visible Continue data genuinely changes. The payload
    // comes from QML as a plain JS object; `id` + `kind` identify it. Reading progress is
    // per-chapter and never auto-dropped (finishing a chapter ≠ finishing the series — use
    // forget() for an explicit "remove from Continue").
    Q_INVOKABLE void record(const QVariantMap &entry) {
        if (persist(entry))
            bump();
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
            out.append(rec);
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
        for (const QString &key : doomed)
            m_map.remove(key);
        m_settings->remove(QStringLiteral("video/watchedMark/")
                          + seriesRootId(id));
        scheduleSave();
        emit syncDirty();
        bump();
    }

    Q_INVOKABLE QVariantMap get(const QString &kind, const QString &id) const {
        return m_map.value(mapKey(kind, id)).toMap();
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
        return doomed.size();
    }

    Q_INVOKABLE int lastSeason(const QString &seriesId) const {
        if (seriesId.isEmpty())
            return -1;
        return m_settings->value(QStringLiteral("video/lastSeason/") + seriesId, -1).toInt();
    }

    Q_INVOKABLE void rememberLastSeason(const QString &seriesId, int season) {
        if (seriesId.isEmpty() || season <= 0)
            return;
        const QString key = QStringLiteral("video/lastSeason/") + seriesId;
        if (m_settings->value(key, -1).toInt() == season)
            return;
        m_settings->setValue(key, season);
        m_settings->sync();
        bump();
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
    Q_INVOKABLE void setWatchedMark(const QString &id, bool watched) {
        if (id.isEmpty()) return;
        m_settings->setValue(QStringLiteral("video/watchedMark/") + seriesRootId(id),
                            watched ? 1 : -1);
        m_settings->sync();
        bump();
    }
    Q_INVOKABLE void clearWatchedMark(const QString &id) {
        if (id.isEmpty()) return;
        m_settings->remove(QStringLiteral("video/watchedMark/") + seriesRootId(id));
        m_settings->sync();
        bump();
    }

    // ---- native remote-import seam (sync) ----
    // These preserve the incoming durable record exactly instead of routing through
    // record(), which intentionally stamps a fresh updatedAt for a NEW local user
    // action. They emit changed() so existing Continue/QML bindings react once, but
    // deliberately do not emit syncDirty(): remote import is not a new local mutation.
    bool applySyncedEntry(const QVariantMap &entry) {
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QString id   = entry.value(QStringLiteral("id")).toString();
        if (kind.isEmpty() || id.isEmpty())
            return false;

        QVariantMap exact = entry;
        exact.insert(QStringLiteral("kind"), kind);
        exact.insert(QStringLiteral("id"), id);

        const QString key = mapKey(kind, id);
        if (m_map.value(key).toMap() == exact)
            return true;

        m_map.insert(key, exact);
        scheduleSave();
        bump();
        emit syncedEntryApplied(kind, id);
        return true;
    }

    bool removeSyncedEntry(const QString &kind, const QString &id) {
        if (kind.isEmpty() || id.isEmpty())
            return false;

        const QString key = mapKey(kind, id);
        if (!m_map.remove(key))
            return true;

        scheduleSave();
        bump();
        return true;
    }

signals:
    void changed();
    // Remote-only import notification. Active readers may react to a synced
    // winner without treating ordinary local progress writes as imported resume.
    // Idempotent replay of the same winner does not emit it.
    void syncedEntryApplied(const QString &kind, const QString &id);
    // Fires for every LOCAL Continue/progress mutation, including the 5s
    // recordSilent() playback tick. It intentionally does not alter revision
    // or changed(), preserving the proven no-rerender silent path.
    void syncDirty();

private:
    static QString mapKey(const QString &kind, const QString &id) {
        return kind + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
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
        if (m_writer) {
            QMetaObject::invokeMethod(m_writer, "writeSnapshot", Qt::QueuedConnection,
                                      Q_ARG(QVariantHash, m_map));
        }
    }

    // Move the writer onto its thread and arrange a final synchronous flush at shutdown so the
    // latest resume point always lands on disk before the process dies.
    void setupWriter() {
        m_writer->moveToThread(&m_writerThread);
        connect(&m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
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
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QString id   = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || kind.isEmpty())
            return false;
        const QString key = mapKey(kind, id);

        const double progress = entry.value(QStringLiteral("progress")).toDouble();
        const bool isSeriesEpisode =
            kind == QStringLiteral("video") && id.count(QLatin1Char(':')) >= 2;
        if (kind == QStringLiteral("video") && progress >= 0.90 && !isSeriesEpisode) {
            // A vault id is "vault:<sha1>" — ONE colon — so every vault video, episodes
            // included, retires here; the mark is what survives the dropped resume record.
            // Guarded on the entry existing so the mark is written once at the crossing:
            // setWatchedMark bumps changed(), and a 5s recordSilent() cascade of that is the
            // proven video-stutter source.
            if (m_map.contains(key)) {
                if (id.startsWith(QStringLiteral("vault:")))
                    setWatchedMark(id, true);
                m_map.remove(key);
                scheduleSave();
                emit syncDirty();
                return true;
            }
            return false;
        }

        QVariantMap rec = entry;
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("kind"), kind);
        if (isSeriesEpisode && progress >= 0.90)
            rec.insert(QStringLiteral("watched"), true);
        rec.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(key, rec);
        scheduleSave();
        emit syncDirty();
        return true;
    }

    void load() {
        m_map.clear();
        const QByteArray blob =
            m_settings->value(QStringLiteral("continue/entries")).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(blob);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
                m_map.insert(it.key(), it.value().toObject().toVariantMap());
        }
    }

    // GUI-thread QSettings: used ONLY for load() at startup and for the infrequent
    // lastSeason / watchedMark keys (rare user actions, never the 5s playback tick). The hot
    // continue/entries path is owned by the background writer's own QSettings instance.
    // A pointer (not a plain member) because which backing store to build — registry vs. a
    // tagged isolation file — is a runtime decision made in the constructor body; see
    // progressStoreTaggedIniPath() above and ProgressStore's default constructor.
    std::unique_ptr<QSettings> m_settings;
    QHash<QString, QVariant> m_map;   // "kind\x1fid" → entry map
    int m_revision = 0;
    ProgressDiskWriter *m_writer = nullptr;
    QThread m_writerThread;
};
