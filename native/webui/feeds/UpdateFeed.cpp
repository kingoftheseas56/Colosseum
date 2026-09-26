#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"
#include "../../update/UpdateService.h"

#include <QElapsedTimer>
#include <QMetaEnum>
#include <QMetaObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <utility>

namespace {
using UpdateService = Colosseum::Update::UpdateService;

QString targetVersion(const UpdateService &updates)
{
    const QString latest = updates.latestVersion();
    return latest.isEmpty() ? updates.installedVersion() : latest;
}

QString primaryCopy(UpdateService::State state)
{
    // Moved from qml/UpdatePage.qml:87-101.
    switch (state) {
    case UpdateService::Checking:
    case UpdateService::Verifying:
    case UpdateService::Installing:
        return {};
    case UpdateService::Available: return QStringLiteral("Download update");
    case UpdateService::Downloading: return QStringLiteral("Pause download");
    case UpdateService::Paused: return QStringLiteral("Resume download");
    case UpdateService::Ready: return QStringLiteral("Restart and update");
    case UpdateService::RecoverableError: return QStringLiteral("Retry download");
    default: return QStringLiteral("Check again");
    }
}

QString primaryAction(UpdateService::State state)
{
    switch (state) {
    case UpdateService::Available:
    case UpdateService::Paused:
    case UpdateService::RecoverableError:
        return QStringLiteral("page.update.download");
    case UpdateService::Downloading:
        return QStringLiteral("page.update.pause");
    case UpdateService::Ready:
        return QStringLiteral("page.update.install");
    case UpdateService::Checking:
    case UpdateService::Verifying:
    case UpdateService::Installing:
        return {};
    default:
        return QStringLiteral("page.update.check");
    }
}

QString statusCopy(const UpdateService &updates)
{
    // Moved from qml/UpdatePage.qml:103-117.
    const QString version = targetVersion(updates);
    switch (updates.state()) {
    case UpdateService::Checking: return QStringLiteral("Checking for updates");
    case UpdateService::UpToDate: return QStringLiteral("Everything is up to date");
    case UpdateService::Available: return QStringLiteral("Colosseum %1 is ready").arg(version);
    case UpdateService::Downloading: return QStringLiteral("Updating to %1").arg(version);
    case UpdateService::Paused: return QStringLiteral("Update paused");
    case UpdateService::Verifying: return QStringLiteral("Verifying the update");
    case UpdateService::Ready: return QStringLiteral("Ready to enter %1").arg(version);
    case UpdateService::Installing: return QStringLiteral("Colosseum is updating");
    case UpdateService::RecoverableError: return QStringLiteral("The update could not finish");
    case UpdateService::VerificationFailure: return QStringLiteral("This update could not be verified");
    case UpdateService::ManualUpdateRequired: return QStringLiteral("Manual update required");
    default: return QStringLiteral("No update check yet");
    }
}

QString formatBytes(qint64 bytes)
{
    const qint64 mb = qRound64(qMax<qint64>(0, bytes) / 1048576.0);
    return QStringLiteral("%1 MB").arg(mb);
}

QString progressCopy(const UpdateService &updates)
{
    // Moved from qml/UpdatePage.qml:128-136.
    if (updates.totalBytes() <= 0)
        return updates.receivedBytes() > 0
            ? QStringLiteral("%1 downloaded · size unknown").arg(formatBytes(updates.receivedBytes()))
            : QStringLiteral("Download size unknown");
    return QStringLiteral("%1 of %2 · %3%")
        .arg(formatBytes(updates.receivedBytes()),
             formatBytes(updates.totalBytes()))
        .arg(qRound(qBound(0.0, updates.progress(), 1.0) * 100.0));
}

QString metadataCopy(const UpdateService &updates)
{
    // Moved from qml/UpdatePage.qml:138-147.
    const QString installed = updates.installedVersion();
    const QString latest = targetVersion(updates);
    if (updates.state() == UpdateService::UpToDate)
        return QStringLiteral("Installed %1 · Latest %2").arg(installed, latest.isEmpty() ? installed : latest);
    if (updates.state() == UpdateService::Downloading) return {};
    if (!latest.isEmpty()) return QStringLiteral("Target %1").arg(latest);
    return installed.isEmpty() ? QString() : QStringLiteral("Installed %1").arg(installed);
}

QVariantList filteredHighlights(const QVariantList &source, const QVariantMap &release)
{
    // Moved from qml/UpdatePage.qml:49-60 and
    // qml/update/UpdateLivingGallery.qml:35-55. Native owns both the kind filter
    // and the signed single-chapter fallback; web only renders these records.
    QVariantList out;
    for (const QVariant &value : source) {
        const QVariantMap row = value.toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("feature") || kind == QLatin1String("statistic")
            || kind == QLatin1String("beforeAfter") || kind == QLatin1String("milestone"))
            out.append(row);
    }
    if (out.isEmpty()) {
        out.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("feature")},
            {QStringLiteral("section"), QStringLiteral("RELEASE")},
            {QStringLiteral("title"),
             release.value(QStringLiteral("title"), QStringLiteral("The latest chapter"))},
            {QStringLiteral("body"),
             release.value(QStringLiteral("summary"),
                           QStringLiteral("The latest Colosseum chronicle lives here."))},
            {QStringLiteral("artwork"), QVariantList{}}});
    }
    return out;
}

QVariantMap captureUpdateData(const UpdateService &updates)
{
    QVariantMap data{{QStringLiteral("schema"), QStringLiteral("update.state")}};
    const UpdateService::State state = updates.state();
    const bool progressVisible = state == UpdateService::Downloading || state == UpdateService::Paused;
    const QString action = primaryAction(state);
    data.insert(QStringLiteral("state"), static_cast<int>(state));
    const char *stateKey = QMetaEnum::fromType<UpdateService::State>().valueToKey(state);
    data.insert(QStringLiteral("stateName"),
                stateKey ? QString::fromLatin1(stateKey) : QStringLiteral("Idle"));
    data.insert(QStringLiteral("installedVersion"), updates.installedVersion());
    data.insert(QStringLiteral("latestVersion"), updates.latestVersion());
    data.insert(QStringLiteral("updateAvailable"), updates.updateAvailable());
    data.insert(QStringLiteral("unseenUpdate"), updates.unseenUpdate());
    data.insert(QStringLiteral("receivedBytes"), updates.receivedBytes());
    data.insert(QStringLiteral("totalBytes"), updates.totalBytes());
    data.insert(QStringLiteral("progress"), updates.progress());
    data.insert(QStringLiteral("progressVisible"), progressVisible);
    data.insert(QStringLiteral("progressIndeterminate"), progressVisible && updates.totalBytes() <= 0);
    data.insert(QStringLiteral("progressText"), progressCopy(updates));
    data.insert(QStringLiteral("statusText"), statusCopy(updates));
    data.insert(QStringLiteral("metadataText"), metadataCopy(updates));
    const QVariantMap release = updates.release();
    data.insert(QStringLiteral("release"), release);
    data.insert(QStringLiteral("chapters"), filteredHighlights(updates.highlights(), release));
    data.insert(QStringLiteral("primary"), QVariantMap{
        {QStringLiteral("label"), primaryCopy(state)},
        {QStringLiteral("action"), action},
        {QStringLiteral("visible"), !action.isEmpty()},
        {QStringLiteral("enabled"), !action.isEmpty()}});
    return data;
}

void captureUpdate(ColosseumWebBridge &bridge, FeedContext &context)
{
    auto *updates = qobject_cast<UpdateService *>(bridge.service(QStringLiteral("Updates")));
    if (!updates) {
        context.nativeSnapshot = {
            {QStringLiteral("schema"), QStringLiteral("update.state")},
            {QStringLiteral("error"), QStringLiteral("Update service is unavailable.")}};
        return;
    }
    // FeedRegistry capture hooks run on the GUI thread before build() moves to
    // the worker, so the worker never reads QObject-owned updater state.
    context.nativeSnapshot = captureUpdateData(*updates);
}

QMetaObject::Connection bindUpdateChanged(QObject *owner, QObject *receiver,
                                         std::function<void()> refresh)
{
    auto *updates = qobject_cast<UpdateService *>(owner);
    if (!updates) return {};

    // State changes are user-visible transitions and refresh immediately.
    // Same-state changed() emissions are primarily download progress/artwork;
    // CONTRACT §3.2 caps progress-driven subscription updates to <= 1/second.
    auto lastState = std::make_shared<UpdateService::State>(updates->state());
    auto throttle = std::make_shared<QElapsedTimer>();
    return QObject::connect(updates, &UpdateService::changed, receiver,
        [updates, lastState, throttle, refresh = std::move(refresh)] {
            const auto state = updates->state();
            if (state != *lastState) {
                *lastState = state;
                throttle->start();
                refresh();
                return;
            }
            if (!throttle->isValid()) {
                throttle->start();
                refresh();
                return;
            }
            if (throttle->elapsed() >= 1000) {
                throttle->restart();
                refresh();
            }
        });
}

QVariantMap updateSection(const QString &state, QVariantMap data = {})
{
    QVariantMap result = WebFeedValue::section(
        QStringLiteral("update.chronicle"), 0, {}, QStringLiteral("custom"), {}, state);
    if (data.isEmpty()) data.insert(QStringLiteral("schema"), QStringLiteral("update.state"));
    result.insert(QStringLiteral("data"), WebFeedValue::jsonMap(data));
    if (state == QLatin1String("error"))
        result.insert(QStringLiteral("error"), data.value(QStringLiteral("error")));
    return result;
}

bool emptyPayload(const QVariantMap &payload) { return payload.isEmpty(); }

UpdateService *service(ColosseumWebBridge &bridge, ActionRegistry::Completion &done)
{
    auto *updates = qobject_cast<UpdateService *>(bridge.service(QStringLiteral("Updates")));
    if (!updates)
        done(QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("Update service is unavailable.")}});
    return updates;
}

void completeOk(ActionRegistry::Completion done, const QVariant &result = {})
{
    QVariantMap answer{{QStringLiteral("ok"), true}};
    if (result.isValid()) answer.insert(QStringLiteral("result"), result);
    done(answer);
}

void completeError(ActionRegistry::Completion done, const QString &error)
{
    done({{QStringLiteral("ok"), false},
          {QStringLiteral("error"), error}});
}

void completeCheckWhenSettled(UpdateService *updates, ColosseumWebBridge &bridge,
                              ActionRegistry::Completion done)
{
    if (updates->state() != UpdateService::Checking) {
        completeOk(done);
        return;
    }

    auto settled = std::make_shared<bool>(false);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(updates, &UpdateService::changed, &bridge,
        [updates, settled, connection, done] {
            if (*settled || updates->state() == UpdateService::Checking) return;
            *settled = true;
            QObject::disconnect(*connection);
            completeOk(done);
        });
    QTimer::singleShot(30000, &bridge, [settled, connection, done] {
        if (*settled) return;
        *settled = true;
        QObject::disconnect(*connection);
        completeError(done, QStringLiteral("The update check did not finish in time."));
    });
}

void markSeen(ColosseumWebBridge &bridge, const QVariantMap &, ActionRegistry::Completion done)
{
    auto *updates = service(bridge, done);
    if (!updates) return;
    // Main.qml:1939-1957 marks the offered release seen when the page opens.
    updates->markSeen();
    completeOk(done);
}

void checkNow(ColosseumWebBridge &bridge, const QVariantMap &, ActionRegistry::Completion done)
{
    auto *updates = service(bridge, done);
    if (!updates) return;
    // UpdatePage.qml:149-157 default primary action -> checkNow().
    if (updates->state() == UpdateService::Checking || updates->state() == UpdateService::Verifying
        || updates->state() == UpdateService::Installing) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("The updater is busy right now.")}});
        return;
    }
    updates->checkNow();
    // UpdateService::checkNow() is asynchronous when a release client is present.
    // CONTRACT §3: act() settles only after the native operation has settled.
    completeCheckWhenSettled(updates, bridge, std::move(done));
}

void download(ColosseumWebBridge &bridge, const QVariantMap &, ActionRegistry::Completion done)
{
    auto *updates = service(bridge, done);
    if (!updates) return;
    // UpdatePage.qml:151-155 starts/resumes/retries only from these states.
    if (updates->state() != UpdateService::Available && updates->state() != UpdateService::Paused
        && updates->state() != UpdateService::RecoverableError) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("No update is ready to download.")}});
        return;
    }
    updates->download();
    // Starting a download is the operation here. UpdateService transitions
    // synchronously before returning, including synchronous hook failures.
    switch (updates->state()) {
    case UpdateService::Downloading:
    case UpdateService::Verifying:
    case UpdateService::Ready:
        completeOk(done);
        return;
    case UpdateService::VerificationFailure:
        completeError(done, QStringLiteral("The downloaded update could not be verified."));
        return;
    default:
        completeError(done, QStringLiteral("The update download could not start."));
        return;
    }
}

void pause(ColosseumWebBridge &bridge, const QVariantMap &, ActionRegistry::Completion done)
{
    auto *updates = service(bridge, done);
    if (!updates) return;
    // UpdatePage.qml:151 pauses only while Downloading.
    if (updates->state() != UpdateService::Downloading) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("There is no active download to pause.")}});
        return;
    }
    updates->cancelDownload();
    if (updates->state() == UpdateService::Paused)
        completeOk(done);
    else
        completeError(done, QStringLiteral("The download could not be paused."));
}

void install(ColosseumWebBridge &bridge, const QVariantMap &, ActionRegistry::Completion done)
{
    auto *updates = service(bridge, done);
    if (!updates) return;
    // UpdatePage.qml:154 hands Ready to restartAndUpdate().
    if (updates->state() != UpdateService::Ready) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("The update is not ready to install.")}});
        return;
    }
    updates->restartAndUpdate();
    // restartAndUpdate() synchronously attempts the installer launch, then queues
    // app shutdown on success. Reply before that queued shutdown turn executes.
    if (updates->state() == UpdateService::Installing)
        completeOk(done);
    else
        completeError(done, QStringLiteral("The updater could not start the installer."));
}

bool valid(const QVariantMap &params) { return params.isEmpty(); }

QVariantList build(const FeedContext &context)
{
    QVariantMap data = context.nativeSnapshot;
    if (data.isEmpty()) {
        data = {{QStringLiteral("schema"), QStringLiteral("update.state")},
                {QStringLiteral("error"), QStringLiteral("Update service is unavailable.")}};
    }
    return {updateSection(data.contains(QStringLiteral("error"))
                              ? QStringLiteral("error") : QStringLiteral("ready"),
                          std::move(data))};
}

FeedRegistry::Entry updateFeedEntry()
{
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("page.update");
    entry.valid = valid;
    entry.initial = [](const QVariantMap &) -> QVariantList {
        return {updateSection(QStringLiteral("loading"))};
    };
    entry.build = build;
    entry.capture = captureUpdate;
    entry.ownerSignals.append(
        {QStringLiteral("Updates"), bindUpdateChanged});
    return entry;
}

const bool feedRegistered = FeedRegistry::add(updateFeedEntry());

const bool seenRegistered = ActionRegistry::add({QStringLiteral("page.update.seen"), emptyPayload, markSeen});
const bool checkRegistered = ActionRegistry::add({QStringLiteral("page.update.check"), emptyPayload, checkNow});
const bool downloadRegistered = ActionRegistry::add({QStringLiteral("page.update.download"), emptyPayload, download});
const bool pauseRegistered = ActionRegistry::add({QStringLiteral("page.update.pause"), emptyPayload, pause});
const bool installRegistered = ActionRegistry::add({QStringLiteral("page.update.install"), emptyPayload, install});
} // namespace
