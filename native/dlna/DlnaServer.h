#pragma once
// DlnaServer — the `dlnaSharing` QML context property and the pupnp lifecycle owner (Arc 71).
// GUI-thread QObject: UpnpInit2/registration/advertisement and teardown run on a worker so
// the UI never blocks (`busy` covers both); results hop back via queued invocations. The AV
// logic lives in DlnaContentDirectory (pure) and the byte serving in DlnaMediaFiles (pupnp
// threads); this class is identity, persistence, SOAP/GENA glue and interface policing.
//
// Persistence (QSettings, injectable for tests): dlnaSharing/enabled (default false),
// dlnaSharing/uuid (generated once), dlnaSharing/port (bound port reused first so TVs' cached
// locations survive restarts; falls back to a random port when taken) and
// dlnaSharing/systemUpdateId (written after each snapshot change and on stop, seeded back
// into DlnaLibrary at startup by the app wiring).
//
// Sharing is a standing wish, not a one-shot attempt: while `enabled`, a 10 s poll starts it
// whenever it is not running and a home adapter exists (no network at boot, Wi-Fi drop, sleep
// and wake all heal themselves) and restarts it when the adapter or its address changes. The
// UI sees error "No home network found" while it waits.

#include "DlnaNetwork.h"
#include "DlnaServerCore.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

class QSettings;

class DlnaServer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString friendlyName READ friendlyName CONSTANT)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemCountChanged)
    Q_PROPERTY(QStringList shareTypes READ shareTypes NOTIFY shareTypesChanged)
    Q_PROPERTY(QString networkLabel READ networkLabel NOTIFY networkLabelChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit DlnaServer(QObject* parent = nullptr);
    ~DlnaServer() override;

    // The snapshot seam: production wires DlnaLibrary::snapshot, tests wire a fake. Must be
    // thread-safe. Set before start()/maybeStartFromSettings().
    void setSnapshotProvider(DlnaServerCore::SnapshotProvider provider);
    // Isolated settings for tests; null (default) uses the application QSettings.
    void setSettings(QSettings* settings);
    // Test lane: bind exactly this adapter (loopback) and skip home-network re-evaluation.
    void setForcedInterface(const DlnaNetAdapter& adapter);
    // Test seam for the network policy: replaces DlnaNetwork::pickHomeAdapter() so a test can
    // make a home adapter appear and vanish (and relaxes the production RFC1918 check on the
    // bound address, which a loopback test lane cannot satisfy).
    using HomeAdapterProvider = std::function<std::optional<DlnaNetAdapter>()>;
    void setHomeAdapterProvider(HomeAdapterProvider provider);
    // Poll period in ms (default 10000). Tests shorten it.
    void setPollIntervalMs(int ms);
    // Idle timeout for stalled media clients (pupnp patch 0002); default 90 s. Tests shorten it.
    void setWriteIdleTimeoutSeconds(int seconds);

    bool enabled() const { return m_enabled; }
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    QString friendlyName() const;
    int itemCount() const { return m_itemCount; }
    QStringList shareTypes() const { return m_shareTypes; }
    QString networkLabel() const { return m_networkLabel; }
    QString error() const { return m_error; }

    // The user's wish (persisted). True starts sharing (async; `busy` then `running`), false
    // stops it (byebye + UpnpFinish, async).
    Q_INVOKABLE void setEnabled(bool on);

    // App start with a saved enabled=true restores sharing (the user's standing choice).
    void maybeStartFromSettings();

    // The library published a new snapshot: refresh QML state, persist the systemUpdateId,
    // notify GENA subscribers (rate-limited to >= 2 s apart, Kodi-style).
    void handleSnapshotChanged();

    // aboutToQuit hook: byebye + UpnpFinish synchronously, before any owner is destroyed.
    void stopForQuit();

signals:
    void enabledChanged();
    void runningChanged();
    void busyChanged();
    void itemCountChanged();
    void shareTypesChanged();
    void networkLabelChanged();
    void errorChanged();

private:
    void beginStart();
    void beginStop(const QString& errorAfterStop = QString());
    void startWorker(const DlnaNetAdapter& adapter, quint16 tryPort);
    void stopWorker();
    void onStarted(quint16 port, const QString& ifName, const QString& ip, const QString& label);
    void onStartFailed(const QString& message);
    void onStopped(const QString& errorAfterStop);
    void pollInterface();
    void startPolling();
    void setError(const QString& message);
    std::optional<DlnaNetAdapter> homeAdapter() const;
    void notifySystemUpdateId();
    void ensureUdn();

    static QByteArray buildDescription(const DlnaServerCore& core);
    void loadIconAndScpdBytes();

    std::shared_ptr<DlnaServerCore> m_core;
    QSettings* m_settings = nullptr;        // never null after ctor (owned or injected)
    std::unique_ptr<QSettings> m_ownSettings;
    bool m_forcedInterface = false;
    HomeAdapterProvider m_homeProvider;     // test seam; empty = DlnaNetwork::pickHomeAdapter
    bool m_testNetwork = false;             // provider injected: skip the RFC1918 bound-IP check
    int m_writeIdleSeconds = 90;
    QString m_boundIfName;                  // what the running server bound
    QString m_boundIp;                      // ...and its address (a DHCP change restarts)
    QTimer m_pollTimer;                     // 10 s home-network check while sharing is enabled
    QTimer m_notifyTimer;                   // GENA rate-limit remainder
    qint64 m_lastNotifyMs = 0;
    quint64 m_containerUpdateCounter = 0;

    bool m_enabled = false;
    bool m_running = false;
    bool m_busy = false;
    bool m_stopForQuit = false;
    bool m_restartAfterStop = false;   // stopped in order to come back (adapter change / re-enable)
    int m_itemCount = 0;
    QStringList m_shareTypes;
    QString m_networkLabel;
    QString m_error;

    std::thread m_worker;                   // the current start/stop worker, if any
};
