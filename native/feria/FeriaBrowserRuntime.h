#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantMap>

class FeriaBrowserPolicy;
class FeriaBrowserOperations;

// Native lifecycle contract for Feria's engine instances (browser foundation
// set 1). SessionStore stays the one registry of open activities and
// FeriaBrowserOperations the one navigation/close ledger; this object owns only
// what exists while an engine view is alive:
//
//   - which engine view currently serves which activity, and what phase that
//     instance is in (creating, ready, close-requested, retiring, failed);
//   - the asynchronous work outstanding against a view or profile, bound to
//     the page, engine generation and document it was admitted for;
//   - the trusted command door (open/activate/navigate/back/forward/reload/
//     stop/close/recover) the shell and any later browser frontend call;
//   - the order engines leave in: work first, views next, profiles last.
//
// It never allocates activity ids and never stores URLs, titles or history.
// Engine objects stay with the adapter (the QML deck and wrappers); this
// object sees them only as QObject identities.
namespace FeriaBrowser {

enum class Phase { Absent, Creating, Ready, CloseRequested, Retiring, Failed };
enum class WorkKind { Script, Request, Adoption, Download };
enum class Command { Activate, Navigate, Back, Forward, Reload, Stop, Close, Recover };

// The stable binding of one page allocation: account, profile, activity and
// the page generation SessionStore assigned when the activity was opened.
// Navigation, redirects, titles and host moves do not change it.
struct PageKey
{
    quint64 accountEpoch = 0;
    QString profileId;
    QString spaceId;
    quint64 pageGeneration = 0;

    bool isValid() const { return !spaceId.isEmpty() && pageGeneration > 0; }
    static PageKey fromIdentity(const QVariantMap &identity);
    friend bool operator==(const PageKey &, const PageKey &) = default;
};

QString phaseName(Phase phase);

} // namespace FeriaBrowser

class FeriaBrowserRuntime final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int viewCount READ viewCount NOTIFY viewCountChanged)
    Q_PROPERTY(int profileCount READ profileCount NOTIFY profileCountChanged)
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit FeriaBrowserRuntime(FeriaBrowserPolicy *policy);

    int viewCount() const { return m_views.size(); }
    int profileCount() const { return m_profiles.size(); }
    bool shuttingDown() const { return m_shuttingDown; }
    int revision() const { return m_revision; }

    // ---- Wiring. The adapter is the one engine owner that executes commands
    // (FeriaSessionDeck); it names the canonical store it adapts. A second
    // adapter or a different store is refused while the first is alive.
    Q_INVOKABLE bool attach(QObject *adapter, QObject *store);
    QObject *store() const { return m_store.data(); }

    // ---- Engine objects. Every engine view and profile registers here,
    // including popups and cleaner views that serve no activity.
    Q_INVOKABLE void registerView(QObject *view);
    Q_INVOKABLE void registerProfile(QObject *profile);
    Q_INVOKABLE bool hasView(QObject *view) const { return m_views.contains(view); }
    Q_INVOKABLE bool hasProfile(QObject *profile) const { return m_profiles.contains(profile); }
    Q_INVOKABLE int viewsForProfile(QObject *profile) const;
    // A profile may be destroyed only after every view using it is gone and no
    // work admitted against it is outstanding.
    Q_INVOKABLE bool profileReleasable(QObject *profile) const;

    // ---- Instance lifecycle, reported by the adapter as the engine does it.
    // beginInstance requires a canonical record; calling it again for a live
    // activity starts a replacement instance with a new engine generation.
    Q_INVOKABLE bool beginInstance(const QString &spaceId);
    Q_INVOKABLE bool bindView(const QString &spaceId, QObject *view);
    Q_INVOKABLE void failInstance(const QString &spaceId, const QString &reason);
    Q_INVOKABLE void markReady(QObject *view);
    Q_INVOKABLE void documentChanged(QObject *view);
    Q_INVOKABLE void rendererLost(QObject *view);
    Q_INVOKABLE void beginRetirement(QObject *view);
    Q_INVOKABLE void retireInstance(const QString &spaceId);
    // creating | ready | close-requested | retiring | failed | absent
    Q_INVOKABLE QString phase(const QString &spaceId) const;
    // {spaceId, phase, reason, engineGeneration, documentEpoch, bound, pendingWork}
    Q_INVOKABLE QVariantMap instance(const QString &spaceId) const;

    // ---- Asynchronous work. A ticket is admitted against the view (or
    // profile) the work was started for and redeemed exactly once when its
    // callback arrives. Redemption fails when the account, page, engine
    // generation or document it was admitted for is no longer current, so a
    // late callback cannot act on a replacement. 0 means not admitted.
    // kinds: script, request (document-scoped); adoption (page-scoped).
    Q_INVOKABLE qint64 admitWork(QObject *view, const QString &kind);
    // kind: download. Bound to the profile object, not to a tab.
    Q_INVOKABLE qint64 admitProfileWork(QObject *profile, const QString &kind);
    Q_INVOKABLE bool workValid(qint64 ticket) const;
    Q_INVOKABLE bool redeemWork(qint64 ticket);
    Q_INVOKABLE void releaseWork(qint64 ticket);
    // The engine can no longer answer (renderer gone, view destroyed).
    Q_INVOKABLE int retireWork(QObject *view);
    Q_INVOKABLE int retireProfileWork(QObject *profile);
    Q_INVOKABLE int pendingWork(QObject *view) const;
    Q_INVOKABLE bool drained(QObject *view) const { return pendingWork(view) == 0; }

    // ---- Trusted commands. A lease is the identity an owner captured; it
    // survives navigation but not an account, profile, page or (when the
    // lease names one) host change. Results: {accepted, reason, command,
    // spaceId}. Website JavaScript has no path to these methods.
    Q_INVOKABLE QVariantMap lease(const QString &spaceId) const;
    Q_INVOKABLE bool leaseCurrent(const QVariantMap &lease) const;
    Q_INVOKABLE QVariantMap open(const QVariantMap &descriptor, const QVariantMap &options = {});
    Q_INVOKABLE QVariantMap execute(const QVariantMap &lease, const QString &command,
                                    const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap command(const QString &spaceId, const QString &command,
                                    const QVariantMap &arguments = {});

public slots:
    // Application exit: refuse new work, retire outstanding tickets, then let
    // the adapter destroy views before profiles. Idempotent.
    void shutdown();

signals:
    void changed();
    void viewCountChanged();
    void profileCountChanged();
    void shuttingDownChanged();
    void phaseChanged(const QString &spaceId, const QString &phase, const QString &reason);
    void commandFinished(const QVariantMap &result);

private slots:
    void reconcile();

private:
    struct View
    {
        QString spaceId;
        quint64 engineGeneration = 0;
        quint64 documentEpoch = 0;
        bool retiring = false;
    };
    struct Space
    {
        FeriaBrowser::PageKey key;
        FeriaBrowser::Phase phase = FeriaBrowser::Phase::Creating;
        QString reason;
        QObject *view = nullptr;
        quint64 engineGeneration = 0;
        QString reported;
    };
    struct Work
    {
        FeriaBrowser::WorkKind kind = FeriaBrowser::WorkKind::Script;
        QObject *view = nullptr;
        QObject *profile = nullptr;
        FeriaBrowser::PageKey key;
        quint64 engineGeneration = 0;
        quint64 documentEpoch = 0;
    };

    QVariantMap canonicalIdentity(const QString &spaceId) const;
    QVariantMap canonicalRecord(const QString &spaceId) const;
    FeriaBrowserOperations *operations() const;
    FeriaBrowser::Phase effectivePhase(const QString &spaceId, const Space &space) const;
    void setPhase(const QString &spaceId, FeriaBrowser::Phase phase, const QString &reason);
    void report(const QString &spaceId);
    void viewDestroyed(QObject *view);
    void profileDestroyed(QObject *profile);
    bool keyCurrent(const FeriaBrowser::PageKey &key) const;
    bool closeWithoutEngine(const QVariantMap &identity);
    QVariantMap finish(QVariantMap result, bool accepted, const QString &reason);
    bool onOwnerThread() const;
    void bump();

    FeriaBrowserPolicy *m_policy = nullptr;
    QPointer<QObject> m_store;
    QPointer<QObject> m_adapter;
    QHash<QObject *, View> m_views;
    QSet<QObject *> m_profiles;
    QHash<QString, Space> m_spaces;
    QHash<qint64, Work> m_work;
    qint64 m_workSerial = 0;
    int m_revision = 0;
    bool m_shuttingDown = false;
    bool m_fenceDisabled = false; // test-only RED control (env-gated)
};
