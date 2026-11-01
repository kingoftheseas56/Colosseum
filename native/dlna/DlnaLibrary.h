#pragma once
// DlnaLibrary - projects Colosseum's Vault + downloaded audiobooks into an immutable
// DlnaSnapshot for the DLNA server (Arc 71). It is the ONLY place that touches the owners;
// pupnp worker threads call snapshot() and read the returned shared_ptr, nothing else.
//
// Lives on the GUI/owner thread (VaultIndex's SQLite connection and the other owners are
// thread-affine). A rebuild is two steps: on the owner thread, read the owners and copy the
// eligible rows (config/index checks only, no file I/O); then a QtConcurrent worker does all the
// file work (one QFileInfo stat per candidate, folder-exists once per root, canonical-path
// dedupe) and the projection, and the result is published back on the owner thread. At most one
// build is in flight; owner changes that arrive during a build trigger exactly one more build
// after it completes. Rebuilds are debounced (~300 ms) after any owner change signal.
//
// What is shared (fail closed, see DlnaLibrary.cpp): confirmed, unhidden roots, not-away rows
// with no errorState whose file exists with the indexed size; video rows must carry
// admissionVerdict "Admitted"; samples/trailers/extras/featurettes are never listed; the
// downloads synthetic root is just another confirmed root (physical files deduped by canonical
// path); audiobooks only when their files exist.

#include "DlnaSnapshot.h"
#include "engine/VaultIndex.h"

#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <mutex>

class VaultConfig;

class DlnaLibrary : public QObject
{
    Q_OBJECT

public:
    // `index` + `config` are required. `audiobooks` is an AudiobookDownloader in production
    // (any QObject exposing Q_INVOKABLE QVariantList downloadedAudiobooks() and
    // QStringList localFiles(QString), plus optional finished(QString,QString)/removed(QString)
    // signals, in tests - the VaultDownloadsRoot QMetaObject-dispatch pattern, so this class
    // does not link the downloader). `vaultLibrary` is optional: when given, its changed()
    // signal also schedules a rebuild. None of the pointers are owned.
    DlnaLibrary(VaultIndex* index, VaultConfig* config, QObject* audiobooks = nullptr,
                QObject* vaultLibrary = nullptr, QObject* parent = nullptr);
    ~DlnaLibrary() override;

    // Optional cover hook, called for every shared vault row ON THE WORKER THREAD during a
    // rebuild: it must be thread-safe, cheap and non-blocking (e.g. VaultThumbnailer::
    // cachedThumbPath, which is a path computation plus one exists() check). Return "" when no
    // local image is already on disk. Unset = coverPath stays empty. Set before start().
    using CoverResolver = std::function<QString(const VaultIndex::FileRow&)>;
    void setCoverResolver(CoverResolver resolver) { m_coverResolver = std::move(resolver); }

    // Persistence seam (this class never touches QSettings). Call once before the first
    // publish with the value the server stored last time; the next published change is
    // value + 1. A value not above the current one is ignored, so the id never goes backwards.
    void setInitialSystemUpdateId(quint32 value);
    // The id of the snapshot currently published (thread-safe). The server stores this after
    // each snapshotChanged() and again on shutdown.
    quint32 currentSystemUpdateId() const;

    // Connect owner signals and schedule the first rebuild. Idempotent.
    void start();
    // Disconnect, drop any pending rebuild, discard any build still running and revoke:
    // publishes an empty snapshot (no snapshotChanged emission). Idempotent.
    void stop();
    bool isStarted() const { return m_started; }

    // Start a build now (no debounce). Asynchronous: returns immediately; the result arrives
    // via snapshotChanged() (when the content differs) and isBuilding() turns false once the
    // result has been handled. If a build is already running, one more is queued behind it.
    void rebuildNow();
    // True from rebuildNow() until its result (and any queued follow-up) is fully handled.
    // Owner thread only.
    bool isBuilding() const { return m_building; }

    // Thread-safe. Never null. The returned snapshot is immutable.
    std::shared_ptr<const DlnaSnapshot> snapshot() const;

    // Truthful MIME by extension; application/octet-stream when unknown.
    static QString mimeForPath(const QString& path);

signals:
    // Emitted on the owner thread after a changed snapshot has been published.
    void snapshotChanged();

private slots:
    void scheduleRebuild();
    void onBuildFinished();

private:
    void publish(std::shared_ptr<DlnaSnapshot> next, bool notify);

    VaultIndex* m_index;
    VaultConfig* m_config;
    QObject* m_audiobooks;
    QObject* m_vaultLibrary;
    CoverResolver m_coverResolver;
    QTimer m_debounce;
    QList<QMetaObject::Connection> m_connections;
    bool m_started = false;

    QFutureWatcher<std::shared_ptr<DlnaSnapshot>> m_watcher;
    bool m_building = false;
    bool m_rebuildPending = false;
    quint64 m_epoch = 0;    // bumped by stop(): results from older epochs are discarded
    quint64 m_jobEpoch = 0; // epoch the in-flight build started in

    mutable std::mutex m_mutex;
    std::shared_ptr<const DlnaSnapshot> m_snapshot; // guarded by m_mutex
    quint32 m_lastUpdateId = 0;                     // owner thread only
};
