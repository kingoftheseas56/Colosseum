#pragma once
// CollectionStore — the "Your Collection" shelf: what the user CHOSE to save via
// the + Library toggle. NOT Continue: distinct from ProgressStore (auto history) —
// an entry can exist unstarted and survives finishing. One store, three worlds.
//
// QML side (the only contract):
//   Collection.add(world, { id, type, title, cover, payload })   // upsert; stamps addedAt
//   Collection.remove(world, id)
//   Collection.has(world, id)
//   Collection.items(world)        // newest-first by addedAt
//   Collection.revision            // bump on every change — name it in a binding to make
//                                  //   has()/items()-based bindings re-evaluate reactively.
//
// `type` must ride on every entry (universe-tile law: a tile without type opens a
// series as a movie and dies). `payload` is the world-specific reopen snapshot.
// Persistence mirrors ProgressStore: one JSON blob under "collection/entries".

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QThread>

#include <algorithm>
#include <memory>
#include <functional>

namespace {
// Isolation gate (2026-08-14 fix). See ProgressStore.h's progressStoreTaggedIniPath() for the
// full writeup: CollectionStore hardcoded the QSettings two-arg registry constructor
// (org="Brotherhood", app="Colosseum"), so a tagged/isolated Lanista test session read AND
// wrote the real user's Collection shelf regardless of COLOSSEUM_APPDATA_TAG. Named per-store
// (collectionStore...) because CollectionStore.h and ProgressStore.h are both #included into
// main.cpp, and two identically-named functions in unnamed namespaces would collide in that
// one translation unit. Untagged: returns an empty string, meaning "use the registry" — the
// daily app is byte-for-byte unaffected.
inline QString collectionStoreTaggedIniPath(const QString &storeFileName) {
    if (!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"))
        return QString();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + storeFileName;
}
}

// Collection remote imports use the same off-owner-thread persistence seam as
// Continue/progress. QSettings is constructed lazily on this worker so the
// GUI-thread owner never blocks on sync() while a pull is being acknowledged.
class CollectionDiskWriter : public QObject {
    Q_OBJECT
public:
    explicit CollectionDiskWriter(const QString &org, const QString &app,
                                  QObject *parent = nullptr)
        : QObject(parent), m_org(org), m_app(app), m_useIni(false) {}
    explicit CollectionDiskWriter(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent), m_iniPath(iniPath), m_useIni(true) {}

public slots:
    void writeSnapshotWithReceipt(quint64 requestId, const QVariantHash &snapshot) {
        QString error;
        const bool committed = writeSnapshot(snapshot, &error);
        emit snapshotFinished(requestId, committed, error);
    }
    void flushSync() {}

signals:
    void snapshotFinished(quint64 requestId, bool committed, const QString &error);

private:
    bool writeSnapshot(const QVariantHash &snapshot, QString *error) {
        ensureSettings();
        QJsonObject object;
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
            object.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings->setValue(QStringLiteral("collection/entries"),
                             QJsonDocument(object).toJson(QJsonDocument::Compact));
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;
        if (error)
            *error = QStringLiteral("The Collection store could not be committed.");
        return false;
    }

    void ensureSettings() {
        if (m_settings)
            return;
        if (m_useIni)
            m_settings = std::make_unique<QSettings>(m_iniPath, QSettings::IniFormat);
        else
            m_settings = std::make_unique<QSettings>(m_org, m_app);
    }

    std::unique_ptr<QSettings> m_settings;
    QString m_org;
    QString m_app;
    QString m_iniPath;
    bool m_useIni = false;
};

class CollectionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY healthChanged)
public:
    using RemoteCommitCallback = std::function<void(bool, const QString &)>;

    explicit CollectionStore(QObject *parent = nullptr)
        : QObject(parent) {
        // Tagged (isolated Lanista test) sessions divert to a file under the tag's own
        // AppData root; untagged (the daily app) is unchanged — registry, same keys.
        const QString tagged = collectionStoreTaggedIniPath(QStringLiteral("collection-store.ini"));
        m_settings = tagged.isEmpty()
            ? std::make_unique<QSettings>(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"))
            : std::make_unique<QSettings>(tagged, QSettings::IniFormat);
        load();
        m_writer = tagged.isEmpty()
            ? new CollectionDiskWriter(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"))
            : new CollectionDiskWriter(tagged);
        setupWriter();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file so
    // harnesses stay hermetic. Mirrors ProgressStore's path constructor.
    explicit CollectionStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {
        load();
        m_writer = new CollectionDiskWriter(iniPath);
        setupWriter();
    }

    ~CollectionStore() override {
        flush();
        if (m_writerThread.isRunning()) {
            m_writerThread.quit();
            m_writerThread.wait();
        }
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

    void flush() {
        if (!m_writer || !m_writerThread.isRunning())
            return;
        QMetaObject::invokeMethod(m_writer, "flushSync", Qt::BlockingQueuedConnection);
    }

    // Native sync seam: complete authoritative Collection state, without changing
    // the QML contract or creating a second persistence authority. The sync
    // adapter performs the portable/local-only projection.
    QVariantList syncEntries() const {
        QStringList keys = m_map.keys();
        keys.sort();
        QVariantList out;
        out.reserve(keys.size());
        for (const QString &key : keys)
            out.append(m_map.value(key).toMap());
        return out;
    }

    // Callers must include `type` on every entry (enforced at the QML call sites,
    // Tasks 4+): the universe-tile law — a tile without type opens a series as a
    // movie and dies. The store persists whatever it's given.
    Q_INVOKABLE bool add(const QString &world, const QVariantMap &entry) {
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || id.isEmpty())
            return false;
        if (!healthy())
            return false;
        QVariantMap e = entry;
        e.insert(QStringLiteral("world"), world);
        if (!e.value(QStringLiteral("addedAt")).toLongLong())
            e.insert(QStringLiteral("addedAt"), QDateTime::currentMSecsSinceEpoch());
        QHash<QString, QVariant> next = m_map;
        next.insert(mapKey(world, id), e);
        if (!save(next))
            return false;
        m_map = next;
        bump();
        emit syncDirty();
        return true;
    }

    Q_INVOKABLE bool remove(const QString &world, const QString &id) {
        if (!healthy())
            return false;
        const QString key = mapKey(world, id);
        if (!m_map.contains(key))
            return true;
        QHash<QString, QVariant> next = m_map;
        next.remove(key);
        if (!save(next))
            return false;
        m_map = next;
        bump();
        emit syncDirty();
        return true;
    }

    // Synchronous remote-owner seam for adapters that must preserve the
    // existing synchronous contract. Local add/remove emit syncDirty(); these
    // owner methods deliberately do not, so remote imports cannot echo.
    bool applySyncedEntry(const QString &world, const QVariantMap &entry) {
        if (!healthy())
            return false;
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || id.isEmpty())
            return false;

        QVariantMap exact = entry;
        exact.insert(QStringLiteral("world"), world);
        exact.insert(QStringLiteral("id"), id);
        const QString key = mapKey(world, id);
        if (m_map.value(key).toMap() == exact)
            return true;

        QHash<QString, QVariant> next = m_map;
        next.insert(key, exact);
        if (!save(next))
            return false;
        m_map = next;
        bump();
        return true;
    }

    bool removeSyncedEntry(const QString &world, const QString &id) {
        if (!healthy() || world.isEmpty() || id.isEmpty())
            return false;

        const QString key = mapKey(world, id);
        if (!m_map.contains(key))
            return true;

        QHash<QString, QVariant> next = m_map;
        next.remove(key);
        if (!save(next))
            return false;
        m_map = next;
        bump();
        return true;
    }

    Q_INVOKABLE bool has(const QString &world, const QString &id) const {
        return m_map.contains(mapKey(world, id));
    }

    Q_INVOKABLE QVariantList items(const QString &world) const {
        QVariantList out;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            const QVariantMap e = it.value().toMap();
            if (e.value(QStringLiteral("world")).toString() == world)
                out.append(e);
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("addedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("addedAt")).toLongLong();
        });
        return out;
    }

    bool applySyncedEntryAsync(const QString &world, const QVariantMap &entry,
                               RemoteCommitCallback callback) {
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return false;
        }
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || id.isEmpty()) {
            if (callback)
                callback(false, QStringLiteral("The Collection record identity is invalid."));
            return false;
        }

        const QString key = mapKey(world, id);
        const QHash<QString, QVariant> baseSnapshot = m_map;
        QVariantMap exact = entry;
        exact.insert(QStringLiteral("world"), world);
        exact.insert(QStringLiteral("id"), id);
        QHash<QString, QVariant> target = m_map;
        target.insert(key, exact);

        // Idempotent remote winners still require a writer receipt before the
        // sync cursor can acknowledge the owner.
        PendingRemote pending;
        pending.requestId = m_nextRemoteRequest++;
        pending.key = key;
        pending.world = world;
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

    bool removeSyncedEntryAsync(const QString &world, const QString &id,
                                RemoteCommitCallback callback) {
        if (!healthy()) {
            if (callback)
                callback(false, persistenceError());
            return false;
        }
        if (world.isEmpty() || id.isEmpty()) {
            if (callback)
                callback(false, QStringLiteral("The Collection record identity is invalid."));
            return false;
        }

        const QString key = mapKey(world, id);
        const QHash<QString, QVariant> baseSnapshot = m_map;
        QHash<QString, QVariant> target = m_map;
        target.remove(key);

        // Verify the backing store even when the tombstone is already absent.
        PendingRemote pending;
        pending.requestId = m_nextRemoteRequest++;
        pending.key = key;
        pending.world = world;
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
    void healthChanged();
    void persistenceFailed(const QString &error);
    void syncDirty();

private:
    struct PendingRemote {
        quint64 requestId = 0;
        QString key;
        QString world;
        QString id;
        QHash<QString, QVariant> target;
        QVariantMap remoteEntry;
        QHash<QString, QVariant> baseSnapshot;
        bool baseKeyPresent = false;
        QVariantMap baseEntry;
        RemoteCommitCallback callback;
    };

    static QString mapKey(const QString &world, const QString &id) {
        return world + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }
    void bump() { ++m_revision; emit changed(); }

    void load() {
        m_map.clear();
        const QByteArray raw = m_settings->value(QStringLiteral("collection/entries")).toByteArray();
        if (m_settings->status() != QSettings::NoError) {
            m_loadError = QStringLiteral("The Collection persistence store could not be read.");
            return;
        }
        if (raw.isEmpty())
            return;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            m_loadError = QStringLiteral("The Collection persistence file is malformed.");
            return;
        }
        const QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (!it.value().isObject()) {
                m_loadError = QStringLiteral("A persisted Collection record is malformed.");
                m_map.clear();
                return;
            }
            const QVariantMap record = it.value().toObject().toVariantMap();
            const QString world = record.value(QStringLiteral("world")).toString();
            const QString id = record.value(QStringLiteral("id")).toString();
            if (world.isEmpty() || id.isEmpty() || it.key() != mapKey(world, id)) {
                m_loadError = QStringLiteral("A persisted Collection record has invalid identity fields.");
                m_map.clear();
                return;
            }
            m_map.insert(it.key(), record);
        }
    }
    bool save(const QHash<QString, QVariant> &records) {
        QJsonObject obj;
        for (auto it = records.constBegin(); it != records.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings->setValue(QStringLiteral("collection/entries"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError)
            return true;
        m_persistenceError = QStringLiteral("The Collection store could not be committed.");
        emit healthChanged();
        emit persistenceFailed(m_persistenceError);
        return false;
    }

    void setupWriter() {
        m_writer->moveToThread(&m_writerThread);
        connect(&m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
        connect(m_writer, &CollectionDiskWriter::snapshotFinished,
                this, &CollectionStore::handleRemoteSnapshotFinished,
                Qt::QueuedConnection);
        m_writerThread.start();
    }

    void postRemoteSnapshot(quint64 requestId, const QHash<QString, QVariant> &snapshot) {
        if (!m_writer || !m_writerThread.isRunning()) {
            handleRemoteSnapshotFinished(requestId, false,
                                         QStringLiteral("The Collection writer is unavailable."));
            return;
        }
        QVariantHash hash;
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
            hash.insert(it.key(), it.value());
        const bool queued = QMetaObject::invokeMethod(
            m_writer,
            "writeSnapshotWithReceipt",
            Qt::QueuedConnection,
            Q_ARG(quint64, requestId),
            Q_ARG(QVariantHash, hash));
        if (!queued)
            handleRemoteSnapshotFinished(
                requestId,
                false,
                QStringLiteral("The Collection writer could not queue the owner snapshot."));
    }

    void handleRemoteSnapshotFinished(quint64 requestId, bool committed,
                                      const QString &error) {
        auto it = m_pendingRemote.find(requestId);
        if (it == m_pendingRemote.end())
            return;
        PendingRemote pending = it.value();
        m_pendingRemote.erase(it);

        if (!committed) {
            m_persistenceError = error.isEmpty()
                ? QStringLiteral("The Collection store could not be committed.")
                : error;
            emit healthChanged();
            emit persistenceFailed(m_persistenceError);
            if (pending.callback)
                pending.callback(false, persistenceError());
            return;
        }

        if (!healthy()) {
            if (pending.callback)
                pending.callback(false, persistenceError());
            return;
        }

        const QHash<QString, QVariant> current = m_map;
        if (pending.baseSnapshot != current) {
            const bool keyUnchanged =
                m_map.contains(pending.key) == pending.baseKeyPresent
                && (!pending.baseKeyPresent
                    || m_map.value(pending.key).toMap()
                        == pending.baseEntry);
            pending.target = current;
            if (keyUnchanged) {
                if (!pending.remoteEntry.isEmpty())
                    pending.target.insert(pending.key, pending.remoteEntry);
                else
                    pending.target.remove(pending.key);
            }
            pending.baseSnapshot = current;
            m_pendingRemote.insert(requestId, pending);
            postRemoteSnapshot(requestId, pending.target);
            return;
        }

        const bool changed = pending.target != m_map;
        m_map = pending.target;
        if (changed) {
            bump();
        }
        if (pending.callback)
            pending.callback(true, QString());
    }

    // A pointer (not a plain member) because which backing store to build — registry vs. a
    // tagged isolation file — is a runtime decision; see collectionStoreTaggedIniPath() above
    // and CollectionStore's default constructor.
    std::unique_ptr<QSettings> m_settings;
    QHash<QString, QVariant> m_map;
    int m_revision = 0;
    QString m_loadError;
    QString m_persistenceError;
    quint64 m_nextRemoteRequest = 1;
    QHash<quint64, PendingRemote> m_pendingRemote;
    CollectionDiskWriter *m_writer = nullptr;
    QThread m_writerThread;
};
