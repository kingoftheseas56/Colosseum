#include "FeriaBrowserRuntime.h"

#include "FeriaBrowserOperations.h"
#include "FeriaBrowserPolicy.h"

#include <QMetaObject>
#include <QThread>
#include <QUrl>

using FeriaBrowser::Command;
using FeriaBrowser::PageKey;
using FeriaBrowser::Phase;
using FeriaBrowser::WorkKind;

namespace {

bool parseWorkKind(const QString &name, WorkKind *kind)
{
    if (name == QStringLiteral("script")) *kind = WorkKind::Script;
    else if (name == QStringLiteral("request")) *kind = WorkKind::Request;
    else if (name == QStringLiteral("adoption")) *kind = WorkKind::Adoption;
    else return false;
    return true;
}

bool parseCommand(const QString &name, Command *command)
{
    static const QHash<QString, Command> commands{
        {QStringLiteral("activate"), Command::Activate},
        {QStringLiteral("navigate"), Command::Navigate},
        {QStringLiteral("back"), Command::Back},
        {QStringLiteral("forward"), Command::Forward},
        {QStringLiteral("reload"), Command::Reload},
        {QStringLiteral("stop"), Command::Stop},
        {QStringLiteral("close"), Command::Close},
        {QStringLiteral("recover"), Command::Recover},
    };
    const auto found = commands.constFind(name);
    if (found == commands.constEnd()) return false;
    *command = found.value();
    return true;
}

} // namespace

PageKey PageKey::fromIdentity(const QVariantMap &identity)
{
    PageKey key;
    key.accountEpoch = identity.value(QStringLiteral("accountEpoch")).toULongLong();
    key.profileId = identity.value(QStringLiteral("profileId")).toString();
    key.spaceId = identity.value(QStringLiteral("spaceId")).toString();
    key.pageGeneration = identity.value(QStringLiteral("pageGeneration")).toULongLong();
    return key;
}

QString FeriaBrowser::phaseName(Phase phase)
{
    switch (phase) {
    case Phase::Creating: return QStringLiteral("creating");
    case Phase::Ready: return QStringLiteral("ready");
    case Phase::CloseRequested: return QStringLiteral("close-requested");
    case Phase::Retiring: return QStringLiteral("retiring");
    case Phase::Failed: return QStringLiteral("failed");
    case Phase::Absent: break;
    }
    return QStringLiteral("absent");
}

FeriaBrowserRuntime::FeriaBrowserRuntime(FeriaBrowserPolicy *policy)
    : QObject(policy), m_policy(policy)
{
#ifdef FERIA_TEST_CONTROLS
    // Test-build-only RED control: switches the lease and work fences off so
    // the negative checks can show the assertions they normally guarantee
    // fail for the intended reason. Compiled out of production builds.
    m_fenceDisabled = qEnvironmentVariable("FERIA_UNSAFE_RUNTIME_FENCE") == QStringLiteral("1");
#endif
}

bool FeriaBrowserRuntime::onOwnerThread() const
{
    // Engine objects, the store and the adapter all live on this object's
    // thread. Admission from anywhere else is refused, never marshalled: a
    // queued admission could outlive the page it was meant for.
    return QThread::currentThread() == thread();
}

void FeriaBrowserRuntime::bump()
{
    ++m_revision;
    emit changed();
}

bool FeriaBrowserRuntime::attach(QObject *adapter, QObject *store)
{
    if (!adapter || !store) return false;
    if (!m_adapter.isNull() && m_adapter.data() != adapter) return false;
    if (!m_store.isNull() && m_store.data() != store) return false;
    m_adapter = adapter;
    if (m_store.isNull()) {
        m_store = store;
        // SessionStore is header-only and driven through its meta-object, as
        // FeriaBrowserOperations already does; no reverse include.
        connect(store, SIGNAL(changed()), this, SLOT(reconcile()));
    }
    return true;
}

QVariantMap FeriaBrowserRuntime::canonicalIdentity(const QString &spaceId) const
{
    if (m_store.isNull() || spaceId.isEmpty()) return {};
    QVariantMap identity;
    QMetaObject::invokeMethod(m_store.data(), "browserIdentity", Qt::DirectConnection,
                              Q_RETURN_ARG(QVariantMap, identity), Q_ARG(QString, spaceId));
    return identity;
}

QVariantMap FeriaBrowserRuntime::canonicalRecord(const QString &spaceId) const
{
    if (m_store.isNull() || spaceId.isEmpty()) return {};
    QVariantMap record;
    QMetaObject::invokeMethod(m_store.data(), "get", Qt::DirectConnection,
                              Q_RETURN_ARG(QVariantMap, record), Q_ARG(QString, spaceId));
    return record;
}

FeriaBrowserOperations *FeriaBrowserRuntime::operations() const
{
    if (m_store.isNull()) return nullptr;
    FeriaBrowserOperations *ledger = nullptr;
    QMetaObject::invokeMethod(m_store.data(), "browserOperations", Qt::DirectConnection,
                              Q_RETURN_ARG(FeriaBrowserOperations*, ledger));
    return ledger;
}

bool FeriaBrowserRuntime::keyCurrent(const PageKey &key) const
{
    if (m_fenceDisabled) return true;
    return key.isValid() && PageKey::fromIdentity(canonicalIdentity(key.spaceId)) == key;
}

// ---- engine objects -------------------------------------------------------

void FeriaBrowserRuntime::registerView(QObject *view)
{
    if (!view || m_views.contains(view)) return;
    m_views.insert(view, View{});
    connect(view, &QObject::destroyed, this, [this, view] { viewDestroyed(view); });
    emit viewCountChanged();
}

void FeriaBrowserRuntime::registerProfile(QObject *profile)
{
    if (!profile || m_profiles.contains(profile)) return;
    m_profiles.insert(profile);
    connect(profile, &QObject::destroyed, this, [this, profile] { profileDestroyed(profile); });
    emit profileCountChanged();
}

void FeriaBrowserRuntime::viewDestroyed(QObject *view)
{
    const View state = m_views.take(view);
    retireWork(view);
    const auto space = m_spaces.find(state.spaceId);
    if (space != m_spaces.end() && space->view == view) {
        space->view = nullptr;
        if (space->phase == Phase::Retiring || !keyCurrent(space->key)) {
            const QString spaceId = space.key();
            m_spaces.erase(space);
            emit phaseChanged(spaceId, FeriaBrowser::phaseName(Phase::Absent),
                              QStringLiteral("destroyed"));
            bump();
        } else {
            // The activity is still open but its engine view is gone without
            // a retirement: surface it so the owner can recover or close.
            setPhase(state.spaceId, Phase::Failed, QStringLiteral("view-destroyed"));
        }
    }
    emit viewCountChanged();
}

void FeriaBrowserRuntime::profileDestroyed(QObject *profile)
{
    m_profiles.remove(profile);
    retireProfileWork(profile);
    emit profileCountChanged();
}

int FeriaBrowserRuntime::viewsForProfile(QObject *profile) const
{
    int count = 0;
    for (auto view = m_views.constBegin(); view != m_views.constEnd(); ++view)
        if (view.key()->property("profile").value<QObject *>() == profile) ++count;
    return count;
}

bool FeriaBrowserRuntime::profileReleasable(QObject *profile) const
{
    if (!profile || viewsForProfile(profile) > 0) return false;
    for (const Work &work : m_work)
        if (work.profile == profile) return false;
    return true;
}

// ---- instance lifecycle ---------------------------------------------------

Phase FeriaBrowserRuntime::effectivePhase(const QString &spaceId, const Space &space) const
{
    // A pending close is canonical state: SessionStore's close attempt. It is
    // read here, never copied, so cancellation returns the instance to ready
    // the moment the store clears the attempt.
    if (space.phase == Phase::Ready
        && !canonicalRecord(spaceId).value(QStringLiteral("browserCloseAttempt")).toString().isEmpty())
        return Phase::CloseRequested;
    return space.phase;
}

void FeriaBrowserRuntime::report(const QString &spaceId)
{
    const auto space = m_spaces.find(spaceId);
    if (space == m_spaces.end()) return;
    const QString name = FeriaBrowser::phaseName(effectivePhase(spaceId, *space));
    const QString reported = name + QLatin1Char('|') + space->reason;
    if (reported == space->reported) return;
    space->reported = reported;
    emit phaseChanged(spaceId, name, space->reason);
    bump();
}

void FeriaBrowserRuntime::setPhase(const QString &spaceId, Phase phase, const QString &reason)
{
    const auto space = m_spaces.find(spaceId);
    if (space == m_spaces.end()) return;
    space->phase = phase;
    space->reason = reason;
    report(spaceId);
}

void FeriaBrowserRuntime::reconcile()
{
    // The canonical registry moved. An instance whose record is gone, or
    // whose page was reallocated, is retired; nothing here closes or opens
    // an activity.
    const QStringList spaces = m_spaces.keys();
    for (const QString &spaceId : spaces) {
        const auto space = m_spaces.constFind(spaceId);
        if (space == m_spaces.constEnd()) continue;
        if (keyCurrent(space->key)) {
            report(spaceId);
        } else if (!space->view) {
            // No engine view to drain: the instance ends with its record.
            m_spaces.remove(spaceId);
            emit phaseChanged(spaceId, FeriaBrowser::phaseName(Phase::Absent),
                              QStringLiteral("record-closed"));
            bump();
        } else if (space->phase != Phase::Retiring) {
            setPhase(spaceId, Phase::Retiring, QStringLiteral("record-closed"));
        }
    }
}

bool FeriaBrowserRuntime::beginInstance(const QString &spaceId)
{
    if (m_shuttingDown) return false;
    const PageKey key = PageKey::fromIdentity(canonicalIdentity(spaceId));
    if (!key.isValid()) return false;
    Space &space = m_spaces[spaceId];
    if (space.view) {
        // A replacement instance for the same activity: the previous view can
        // still drain, but nothing admitted for it is valid any more.
        const auto previous = m_views.find(space.view);
        if (previous != m_views.end()) previous->retiring = true;
        space.view = nullptr;
    }
    space.key = key;
    ++space.engineGeneration;
    setPhase(spaceId, Phase::Creating, QString());
    return true;
}

bool FeriaBrowserRuntime::bindView(const QString &spaceId, QObject *view)
{
    const auto space = m_spaces.find(spaceId);
    if (!view || space == m_spaces.end() || space->phase == Phase::Retiring) return false;
    if (space->view && space->view != view) return false; // one live view per activity
    registerView(view);
    View &state = m_views[view];
    if (!state.spaceId.isEmpty() && state.spaceId != spaceId) return false; // never rebound
    state.spaceId = spaceId;
    state.engineGeneration = space->engineGeneration;
    space->view = view;
    bump();
    return true;
}

void FeriaBrowserRuntime::failInstance(const QString &spaceId, const QString &reason)
{
    const auto space = m_spaces.constFind(spaceId);
    if (space == m_spaces.constEnd() || space->phase == Phase::Retiring) return;
    setPhase(spaceId, Phase::Failed, reason);
}

void FeriaBrowserRuntime::markReady(QObject *view)
{
    const auto state = m_views.constFind(view);
    if (state == m_views.constEnd() || state->retiring) return;
    const auto space = m_spaces.constFind(state->spaceId);
    if (space == m_spaces.constEnd() || space->view != view) return;
    if (space->phase == Phase::Creating || space->phase == Phase::Failed)
        setPhase(state->spaceId, Phase::Ready, QString());
}

void FeriaBrowserRuntime::documentChanged(QObject *view)
{
    const auto state = m_views.find(view);
    if (state != m_views.end()) ++state->documentEpoch;
}

void FeriaBrowserRuntime::rendererLost(QObject *view)
{
    const auto state = m_views.find(view);
    if (state == m_views.end()) return;
    // The renderer cannot answer anything admitted for it. The activity keeps
    // its identity; its next document belongs to a new engine generation.
    retireWork(view);
    ++state->documentEpoch;
    const auto space = m_spaces.find(state->spaceId);
    if (space != m_spaces.end() && space->view == view) {
        state->engineGeneration = ++space->engineGeneration;
        if (space->phase != Phase::Retiring)
            setPhase(state->spaceId, Phase::Failed, QStringLiteral("renderer-lost"));
    } else {
        ++state->engineGeneration;
    }
}

void FeriaBrowserRuntime::beginRetirement(QObject *view)
{
    const auto state = m_views.find(view);
    if (state == m_views.end() || state->retiring) return;
    state->retiring = true;
    // Scripts already dispatched stay outstanding until the engine returns or
    // loses them; prompts and adoptions can no longer be answered.
    for (auto work = m_work.begin(); work != m_work.end();) {
        if (work->view == view && work->kind != WorkKind::Script) work = m_work.erase(work);
        else ++work;
    }
    const auto space = m_spaces.constFind(state->spaceId);
    if (space != m_spaces.constEnd() && space->view == view && space->phase != Phase::Retiring)
        setPhase(state->spaceId, Phase::Retiring, QStringLiteral("retiring"));
}

void FeriaBrowserRuntime::retireInstance(const QString &spaceId)
{
    const auto space = m_spaces.find(spaceId);
    if (space == m_spaces.end()) return;
    if (space->view) {
        if (space->phase != Phase::Retiring)
            setPhase(spaceId, Phase::Retiring, QStringLiteral("retiring"));
        return;
    }
    // No engine view was ever created or it is already gone: nothing to drain.
    m_spaces.erase(space);
    emit phaseChanged(spaceId, FeriaBrowser::phaseName(Phase::Absent), QStringLiteral("destroyed"));
    bump();
}

QString FeriaBrowserRuntime::phase(const QString &spaceId) const
{
    const auto space = m_spaces.constFind(spaceId);
    return FeriaBrowser::phaseName(space == m_spaces.constEnd()
        ? Phase::Absent : effectivePhase(spaceId, *space));
}

QVariantMap FeriaBrowserRuntime::instance(const QString &spaceId) const
{
    const auto space = m_spaces.constFind(spaceId);
    if (space == m_spaces.constEnd())
        return {{QStringLiteral("spaceId"), spaceId},
                {QStringLiteral("phase"), FeriaBrowser::phaseName(Phase::Absent)}};
    const auto view = m_views.constFind(space->view);
    return {{QStringLiteral("spaceId"), spaceId},
            {QStringLiteral("phase"), FeriaBrowser::phaseName(effectivePhase(spaceId, *space))},
            {QStringLiteral("reason"), space->reason},
            {QStringLiteral("engineGeneration"), space->engineGeneration},
            {QStringLiteral("documentEpoch"), view == m_views.constEnd() ? 0 : view->documentEpoch},
            {QStringLiteral("bound"), space->view != nullptr},
            {QStringLiteral("pendingWork"), space->view ? pendingWork(space->view) : 0}};
}

// ---- asynchronous work ----------------------------------------------------

qint64 FeriaBrowserRuntime::admitWork(QObject *view, const QString &kind)
{
    WorkKind parsed;
    if (m_shuttingDown || !onOwnerThread() || !parseWorkKind(kind, &parsed)) return 0;
    const auto state = m_views.constFind(view);
    if (state == m_views.constEnd() || state->retiring) return 0;
    Work work;
    work.kind = parsed;
    work.view = view;
    work.engineGeneration = state->engineGeneration;
    work.documentEpoch = state->documentEpoch;
    if (!state->spaceId.isEmpty()) {
        // Bound views admit work only as the current instance of a page that
        // is still canonical: a superseded view or a closed account gets none.
        const auto space = m_spaces.constFind(state->spaceId);
        if (space == m_spaces.constEnd() || space->view != view || !keyCurrent(space->key))
            return 0;
        work.key = space->key;
    }
    m_work.insert(++m_workSerial, work);
    return m_workSerial;
}

qint64 FeriaBrowserRuntime::admitProfileWork(QObject *profile, const QString &kind)
{
    if (m_shuttingDown || !onOwnerThread() || kind != QStringLiteral("download")
        || !m_profiles.contains(profile))
        return 0;
    Work work;
    work.kind = WorkKind::Download;
    work.profile = profile;
    m_work.insert(++m_workSerial, work);
    return m_workSerial;
}

bool FeriaBrowserRuntime::workValid(qint64 ticket) const
{
    const auto work = m_work.constFind(ticket);
    if (work == m_work.constEnd()) return false;
    if (m_fenceDisabled) return true;
    if (m_shuttingDown) return false;
    if (work->profile) return m_profiles.contains(work->profile);
    const auto state = m_views.constFind(work->view);
    if (state == m_views.constEnd() || state->retiring) return false;
    if (state->engineGeneration != work->engineGeneration) return false;
    if (work->kind != WorkKind::Adoption && state->documentEpoch != work->documentEpoch)
        return false;
    return !work->key.isValid() || keyCurrent(work->key);
}

bool FeriaBrowserRuntime::redeemWork(qint64 ticket)
{
    const bool valid = workValid(ticket);
    m_work.remove(ticket);
    return valid;
}

void FeriaBrowserRuntime::releaseWork(qint64 ticket)
{
    m_work.remove(ticket);
}

int FeriaBrowserRuntime::retireWork(QObject *view)
{
    int retired = 0;
    for (auto work = m_work.begin(); work != m_work.end();) {
        if (work->view == view && view) { work = m_work.erase(work); ++retired; }
        else ++work;
    }
    return retired;
}

int FeriaBrowserRuntime::retireProfileWork(QObject *profile)
{
    int retired = 0;
    for (auto work = m_work.begin(); work != m_work.end();) {
        if (work->profile == profile && profile) { work = m_work.erase(work); ++retired; }
        else ++work;
    }
    return retired;
}

int FeriaBrowserRuntime::pendingWork(QObject *view) const
{
    int pending = 0;
    for (const Work &work : m_work)
        if (work.view == view) ++pending;
    return pending;
}

// ---- trusted commands -----------------------------------------------------

QVariantMap FeriaBrowserRuntime::lease(const QString &spaceId) const
{
    return canonicalIdentity(spaceId);
}

bool FeriaBrowserRuntime::leaseCurrent(const QVariantMap &lease) const
{
    const PageKey key = PageKey::fromIdentity(lease);
    if (!key.isValid()) return false;
    const QVariantMap canonical = canonicalIdentity(key.spaceId);
    if (canonical.isEmpty()) return false;
    if (m_fenceDisabled) return true;
    if (PageKey::fromIdentity(canonical) != key) return false;
    // A lease that names a host is bound to that host placement as well.
    if (lease.contains(QStringLiteral("hostId"))
        && (lease.value(QStringLiteral("hostId")) != canonical.value(QStringLiteral("hostId"))
            || lease.value(QStringLiteral("hostGeneration")).toULongLong()
               != canonical.value(QStringLiteral("hostGeneration")).toULongLong()))
        return false;
    return true;
}

QVariantMap FeriaBrowserRuntime::finish(QVariantMap result, bool accepted, const QString &reason)
{
    result.insert(QStringLiteral("accepted"), accepted);
    result.insert(QStringLiteral("reason"), reason);
    emit commandFinished(result);
    return result;
}

QVariantMap FeriaBrowserRuntime::open(const QVariantMap &descriptor, const QVariantMap &options)
{
    QVariantMap result{{QStringLiteral("command"), QStringLiteral("open")},
                       {QStringLiteral("spaceId"), QString()}};
    if (!onOwnerThread()) return finish(result, false, QStringLiteral("wrong-thread"));
    if (m_shuttingDown) return finish(result, false, QStringLiteral("shutting-down"));
    if (m_store.isNull()) return finish(result, false, QStringLiteral("no-store"));
    const QUrl url = descriptor.value(QStringLiteral("url")).toUrl();
    if (!FeriaBrowserPolicy::allows(url) || url.scheme() == QStringLiteral("blob"))
        return finish(result, false, QStringLiteral("navigation-not-allowed"));
    QString spaceId;
    QMetaObject::invokeMethod(m_store.data(), "openBrowser", Qt::DirectConnection,
        Q_RETURN_ARG(QString, spaceId), Q_ARG(QVariantMap, descriptor),
        Q_ARG(bool, options.value(QStringLiteral("fresh")).toBool()),
        Q_ARG(bool, options.value(QStringLiteral("navigate")).toBool()),
        Q_ARG(bool, options.value(QStringLiteral("activate"), true).toBool()));
    result.insert(QStringLiteral("spaceId"), spaceId);
    return finish(result, !spaceId.isEmpty(),
                  spaceId.isEmpty() ? QStringLiteral("store-refused") : QString());
}

bool FeriaBrowserRuntime::closeWithoutEngine(const QVariantMap &identity)
{
    // No engine view holds a document, so there is no unload consent to ask
    // for. The close still runs through the one close ledger, with evidence
    // that says exactly that.
    FeriaBrowserOperations *ledger = operations();
    if (!ledger) return false;
    const QString spaceId = identity.value(QStringLiteral("spaceId")).toString();
    QString engine = canonicalRecord(spaceId).value(QStringLiteral("target")).toMap()
        .value(QStringLiteral("engineId")).toString();
    if (engine != QStringLiteral("webview2")) engine = QStringLiteral("qtwebengine");
    const QVariantMap admitted = ledger->beginOperation(identity, engine,
                                                        QStringLiteral("request-close"));
    if (!admitted.value(QStringLiteral("admitted")).toBool()) return false;
    const QVariantMap settled = ledger->settleOperation(identity,
        admitted.value(QStringLiteral("operationId")).toString(), QStringLiteral("succeeded"),
        QStringLiteral("no-engine-instance"), QStringLiteral("no-engine-instance"), {});
    return settled.value(QStringLiteral("settled")).toBool()
        && settled.value(QStringLiteral("outcome")).toString() == QStringLiteral("succeeded");
}

QVariantMap FeriaBrowserRuntime::command(const QString &spaceId, const QString &command,
                                         const QVariantMap &arguments)
{
    // Checked here as well: reading the lease already touches the store.
    if (!onOwnerThread())
        return finish({{QStringLiteral("command"), command}, {QStringLiteral("spaceId"), spaceId}},
                      false, QStringLiteral("wrong-thread"));
    return execute(lease(spaceId), command, arguments);
}

QVariantMap FeriaBrowserRuntime::execute(const QVariantMap &lease, const QString &command,
                                         const QVariantMap &arguments)
{
    const QString spaceId = lease.value(QStringLiteral("spaceId")).toString();
    QVariantMap result{{QStringLiteral("command"), command}, {QStringLiteral("spaceId"), spaceId}};
    Command parsed;
    if (!parseCommand(command, &parsed)) return finish(result, false, QStringLiteral("unknown-command"));
    if (!onOwnerThread()) return finish(result, false, QStringLiteral("wrong-thread"));
    if (m_shuttingDown) return finish(result, false, QStringLiteral("shutting-down"));
    if (canonicalIdentity(spaceId).isEmpty()) return finish(result, false, QStringLiteral("unknown-space"));
    if (!leaseCurrent(lease)) return finish(result, false, QStringLiteral("stale-lease"));

    if (parsed == Command::Activate) {
        QMetaObject::invokeMethod(m_store.data(), "switchTo", Qt::DirectConnection,
                                  Q_ARG(QString, spaceId));
        return finish(result, true, QString());
    }

    const auto space = m_spaces.constFind(spaceId);
    const Phase phase = space == m_spaces.constEnd() ? Phase::Absent : effectivePhase(spaceId, *space);
    if (phase == Phase::Retiring) return finish(result, false, QStringLiteral("retiring"));
    const bool bound = space != m_spaces.constEnd() && space->view != nullptr;

    if (parsed == Command::Close && !bound) {
        const bool closed = closeWithoutEngine(canonicalIdentity(spaceId));
        return finish(result, closed, closed ? QString() : QStringLiteral("close-refused"));
    }
    if (phase == Phase::Absent) return finish(result, false, QStringLiteral("no-engine-instance"));

    QVariantMap dispatched = arguments;
    switch (parsed) {
    case Command::Navigate: {
        const QUrl url = arguments.contains(QStringLiteral("text"))
            ? m_policy->omniboxUrl(arguments.value(QStringLiteral("text")).toString(),
                  arguments.value(QStringLiteral("searchEngine"), QStringLiteral("duckduckgo")).toString())
            : arguments.value(QStringLiteral("url")).toUrl();
        if (url.isEmpty() || !FeriaBrowserPolicy::allows(url))
            return finish(result, false, QStringLiteral("navigation-not-allowed"));
        dispatched.insert(QStringLiteral("url"), url.toString());
        break;
    }
    case Command::Back:
    case Command::Forward:
    case Command::Stop:
        if (phase != Phase::Ready && phase != Phase::CloseRequested)
            return finish(result, false, QStringLiteral("not-ready"));
        break;
    case Command::Reload:
        if (!bound || phase == Phase::Creating)
            return finish(result, false, QStringLiteral("not-ready"));
        break;
    case Command::Recover:
        if (phase != Phase::Failed) return finish(result, false, QStringLiteral("not-failed"));
        break;
    case Command::Close:
    case Command::Activate:
        break;
    }

    if (m_adapter.isNull()) return finish(result, false, QStringLiteral("no-adapter"));
    dispatched.insert(QStringLiteral("name"), command);
    dispatched.insert(QStringLiteral("spaceId"), spaceId);
    QVariant accepted;
    const bool invoked = QMetaObject::invokeMethod(m_adapter.data(), "runBrowserCommand",
        Qt::DirectConnection, Q_RETURN_ARG(QVariant, accepted),
        Q_ARG(QVariant, QVariant(dispatched)));
    if (!invoked) return finish(result, false, QStringLiteral("no-adapter"));
    return finish(result, accepted.toBool(),
                  accepted.toBool() ? QString() : QStringLiteral("adapter-refused"));
}

void FeriaBrowserRuntime::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    emit shuttingDownChanged();
    // Nothing admitted so far may act any more, and nothing new is admitted.
    m_work.clear();
    const QStringList spaces = m_spaces.keys();
    for (const QString &spaceId : spaces)
        setPhase(spaceId, Phase::Retiring, QStringLiteral("shutdown"));
#ifdef FERIA_TEST_CONTROLS
    // RED control for the shutdown fixture: leave engines to the default
    // object-tree teardown order instead of views-before-profiles.
    if (qEnvironmentVariable("FERIA_SKIP_ORDERLY_SHUTDOWN") == QStringLiteral("1")) return;
#endif
    if (!m_adapter.isNull())
        QMetaObject::invokeMethod(m_adapter.data(), "shutdownEngines", Qt::DirectConnection);
    bump();
}
