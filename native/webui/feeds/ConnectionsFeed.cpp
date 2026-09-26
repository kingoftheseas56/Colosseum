#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "../ColosseumWebBridge.h"
#include "../../trackers/TrackerSyncCenterModel.h"

#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QPointer>
#include <memory>

namespace {
QVariantMap section(const QString &id, int index, const QString &schema,
                    const QString &state = QStringLiteral("loading"),
                    QVariantMap data = {})
{
    QVariantMap out{{QStringLiteral("id"), id}, {QStringLiteral("index"), index},
                    {QStringLiteral("title"), QString()},
                    {QStringLiteral("layout"), QStringLiteral("custom")},
                    {QStringLiteral("state"), state},
                    {QStringLiteral("items"), QVariantList{}}};
    if (!data.isEmpty()) {
        data.insert(QStringLiteral("schema"), schema);
        out.insert(QStringLiteral("data"), data);
    }
    return out;
}

bool valid(const QVariantMap &params) { return params.isEmpty(); }

QVariantList initial(const QVariantMap &)
{
    return {section(QStringLiteral("connections.header"), 0, {}),
            section(QStringLiteral("connections.native"), 1, {}),
            section(QStringLiteral("connections.stremio"), 2, {}),
            section(QStringLiteral("connections.catalogue"), 3, {})};
}

void capture(ColosseumWebBridge &bridge, FeedContext &context)
{
    // TrackerSyncCenterPage.qml:68-98,1560-1670. Capture on the GUI thread;
    // the feed worker sees only the model's safe, profile-bound projection.
    auto *model = qobject_cast<TrackerSyncCenterModel *>(
        bridge.service(QStringLiteral("TrackerSyncCenter")));
    if (!model) return;
    context.nativeSnapshot = {
        {QStringLiteral("revision"), QVariant::fromValue(model->revision())},
        {QStringLiteral("aggregate"), model->aggregateState()},
        {QStringLiteral("connected"), model->connectedTrackers()},
        {QStringLiteral("catalogue"), model->catalogue()},
        {QStringLiteral("globalSettings"), model->globalSettings()},
        {QStringLiteral("importReviews"), model->importReviews()}
    };
}

QString summary(const QVariantMap &aggregate)
{
    const int connected = aggregate.value(QStringLiteral("connectedCount")).toInt();
    const int waiting = aggregate.value(QStringLiteral("waitingCount")).toInt();
    const int attention = aggregate.value(QStringLiteral("attentionProviderCount")).toInt();
    const int unresolved = aggregate.value(QStringLiteral("unresolvedCount")).toInt();
    if (!connected)
        return attention ? QStringLiteral("Disconnected third-party tracker work needs attention")
                         : QStringLiteral("No third-party trackers connected");
    QStringList parts{QStringLiteral("%1 connected").arg(connected)};
    if (waiting) parts.append(QStringLiteral("%1 waiting").arg(waiting));
    if (attention) parts.append(QStringLiteral("%1 need attention").arg(attention));
    else if (unresolved) parts.append(QStringLiteral("%1 need review").arg(unresolved));
    if (aggregate.value(QStringLiteral("syncing")).toBool()) parts.prepend(QStringLiteral("Syncing"));
    return parts.join(QStringLiteral(" · "));
}

QVariantList build(const FeedContext &context)
{
    const QVariantMap snap = context.nativeSnapshot;
    if (snap.isEmpty()) {
        QVariantMap error = section(QStringLiteral("connections.owner"), 0, {}, QStringLiteral("error"));
        error.insert(QStringLiteral("error"), QStringLiteral("Connection information is unavailable for this profile."));
        return {error};
    }
    const QVariantMap aggregate = snap.value(QStringLiteral("aggregate")).toMap();
    const QVariantList connected = snap.value(QStringLiteral("connected")).toList();
    const QVariantList catalogue = snap.value(QStringLiteral("catalogue")).toList();
    const QString aggregateText = summary(aggregate);
    QVariantList sections;
    // TrackerSyncCenterPage.qml:1560-1670. Heading and controls.
    sections.append(section(QStringLiteral("connections.header"), 0,
                            QStringLiteral("connections.header"), QStringLiteral("ready"),
                            {{QStringLiteral("revision"), snap.value(QStringLiteral("revision"))},
                             {QStringLiteral("aggregate"), aggregate},
                             {QStringLiteral("globalSettings"), snap.value(QStringLiteral("globalSettings"))},
                             {QStringLiteral("summary"), aggregateText}}));
    if (connected.isEmpty()) {
        // TrackerSyncCenterPage.qml:1671-1741. Native History remains canonical.
        sections.append(section(QStringLiteral("connections.native"), 1,
                                QStringLiteral("connections.native"), QStringLiteral("ready"),
                                {{QStringLiteral("name"), QStringLiteral("Colosseum")},
                                 {QStringLiteral("statement"), QStringLiteral("Works without trackers.")},
                                 {QStringLiteral("detail"), QStringLiteral("Your native progress, History, and activity stay here.")},
                                 {QStringLiteral("badge"), QStringLiteral("Always available")}}));
    } else {
        // TrackerSyncCenterPage.qml:1742-1936. A visual map of native and connected providers.
        sections.append(section(QStringLiteral("connections.relay"), 1,
                                QStringLiteral("connections.relay"), QStringLiteral("ready"),
                                {{QStringLiteral("summary"), aggregateText},
                                 {QStringLiteral("providers"), connected}}));
    }
    if (aggregate.value(QStringLiteral("attentionProviderCount")).toInt() > 0
        || aggregate.value(QStringLiteral("unresolvedCount")).toInt() > 0)
        sections.append(section(QStringLiteral("connections.attention"), 2,
                                QStringLiteral("connections.attention"), QStringLiteral("ready"),
                                {{QStringLiteral("attentionProviderCount"), aggregate.value(QStringLiteral("attentionProviderCount"))},
                                 {QStringLiteral("unresolvedCount"), aggregate.value(QStringLiteral("unresolvedCount"))}}));
    if (!connected.isEmpty())
        // TrackerSyncCenterPage.qml:1936-2140. Connected providers stay distinct from catalogue.
        sections.append(section(QStringLiteral("connections.connected"), 3,
                                QStringLiteral("connections.connected"), QStringLiteral("ready"),
                                {{QStringLiteral("providers"), connected}}));
    // TrackerSyncCenterPage.qml:2141-2279. Status/panel handoff awaits the
    // shared native Stremio seam; the card does not claim a connection.
    sections.append(section(QStringLiteral("connections.stremio"), 4,
                            QStringLiteral("connections.stremio"), QStringLiteral("ready"),
                            {{QStringLiteral("name"), QStringLiteral("Stremio")},
                             {QStringLiteral("capabilities"), QStringList{QStringLiteral("Library"),
                                                                           QStringLiteral("Progress"),
                                                                           QStringLiteral("History")}},
                             {QStringLiteral("status"), QStringLiteral("Status unavailable")},
                             {QStringLiteral("panelAvailable"), false}}));
    // TrackerSyncCenterPage.qml:2280-2402. Availability is native-issued.
    sections.append(section(QStringLiteral("connections.catalogue"), 5,
                            QStringLiteral("connections.catalogue"),
                            catalogue.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                            {{QStringLiteral("providers"), catalogue}}));
    return sections;
}

const bool feedRegistered = FeedRegistry::add({QStringLiteral("page.connections"), {}, valid,
                                               initial, build, false, false, false, false,
                                               nullptr, capture});

TrackerSyncCenterModel *owner(ColosseumWebBridge &bridge,
                              const ActionRegistry::Completion &done)
{
    auto *model = qobject_cast<TrackerSyncCenterModel *>(
        bridge.service(QStringLiteral("TrackerSyncCenter")));
    if (!model)
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Connections are unavailable for this profile.")}});
    return model;
}

bool expectedRevision(const QVariantMap &payload, TrackerSyncCenterModel &model,
                      const ActionRegistry::Completion &done, quint64 *revision)
{
    bool validRevision = false;
    const quint64 expected = payload.value(QStringLiteral("revision")).toULongLong(&validRevision);
    if (!validRevision || expected != model.revision()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Connection information changed. Open this action again.")}});
        return false;
    }
    *revision = expected;
    return true;
}

QVariantMap response(bool accepted, TrackerSyncCenterModel &model,
                     const QString &success, const QString &failure)
{
    const QVariantMap result = model.lastActionResult();
    const QString code = result.value(QStringLiteral("code")).toString();
    if (!accepted) {
        QString error = failure;
        if (code == QLatin1String("stale_intent"))
            error = QStringLiteral("Connection information changed. Open this action again.");
        else if (code == QLatin1String("persistence_failed"))
            error = QStringLiteral("Colosseum could not save that change.");
        else if (code == QLatin1String("progress_removed_history_removal_failed"))
            error = QStringLiteral("Eligible imported Progress was removed, but tracker History evidence remains. Reopen the preview before retrying.");
        else if (code == QLatin1String("progress_removal_failed"))
            error = QStringLiteral("Tracker-imported Progress could not be removed. Reopen the preview before retrying.");
        else if (code == QLatin1String("disconnect_cleanup_pending"))
            error = QStringLiteral("The tracker is disconnected, but known-unsent updates remain paused. Retry cleanup here.");
        else if (code == QLatin1String("delivery_in_flight"))
            error = QStringLiteral("A tracker update is still sending. Let it finish before disconnecting.");
        else if (code == QLatin1String("apply_needs_attention"))
            error = QStringLiteral("The import was confirmed, but applying its progress needs attention. Review the current batch before retrying.");
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("notice"), success},
                {QStringLiteral("revision"), QVariant::fromValue(model.revision())}}}};
}

void settledAction(ColosseumWebBridge &bridge, TrackerSyncCenterModel &model,
                   const QString &action, const QString &pendingCode,
                   const QStringList &terminalCodes,
                   const QString &success, const QString &failure,
                   const std::function<bool()> &start,
                   ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:679-695,1099-1142. Applying an import or
    // removing imported data can finish after the model call returns. The
    // web action resolves only after that final native outcome.
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto completed = std::make_shared<bool>(false);
    auto invoking = std::make_shared<bool>(true);
    QPointer<TrackerSyncCenterModel> guarded(&model);
    auto finish = [connection, completed, done](const QVariantMap &answer) {
        if (*completed) return;
        *completed = true;
        QObject::disconnect(*connection);
        done(answer);
    };
    *connection = QObject::connect(&model, &TrackerSyncCenterModel::modelChanged,
                                   &bridge, [guarded, action, terminalCodes, success,
                                             failure, finish, invoking] {
        if (!guarded || *invoking) return;
        const QVariantMap result = guarded->lastActionResult();
        if (result.value(QStringLiteral("action")).toString() != action
            || !terminalCodes.contains(result.value(QStringLiteral("code")).toString()))
            return;
        finish(response(result.value(QStringLiteral("accepted")).toBool(),
                        *guarded, success, failure));
    });
    QObject::connect(&model, &QObject::destroyed, &bridge, [finish] {
        finish({{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("The profile changed before this connection action finished.")}});
    });
    const bool accepted = start();
    *invoking = false;
    if (*completed || !guarded) return;
    const QVariantMap result = guarded->lastActionResult();
    if (!accepted || result.value(QStringLiteral("code")).toString() != pendingCode)
        finish(response(accepted, *guarded, success, failure));
}

bool validRead(const QVariantMap &payload)
{
    return !payload.value(QStringLiteral("providerKey")).toString().isEmpty()
        && payload.contains(QStringLiteral("revision"));
}

void readDossier(ColosseumWebBridge &bridge, const QVariantMap &payload,
                 ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1188-1217,1304-1348. Read only the current
    // profile's safe dossier; the native model withholds remote identity.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QString key = payload.value(QStringLiteral("providerKey")).toString();
    QVariantMap dossier;
    QVariantList delivery;
    if (key == QLatin1String("global")) {
        dossier = model->globalSettings();
        dossier.insert(QStringLiteral("found"), true);
        dossier.insert(QStringLiteral("providerKey"), key);
        dossier.insert(QStringLiteral("providerName"), QStringLiteral("Connection preferences"));
        dossier.insert(QStringLiteral("status"), QStringLiteral("Colosseum first"));
    } else {
        dossier = model->providerDossier(key);
        delivery = model->deliveryRows(key);
    }
    if (!dossier.value(QStringLiteral("found")).toBool() || model->revision() != revision) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Connection details changed. Open them again.")}});
        return;
    }
    QVariantList imports;
    for (const QVariant &value : model->importReviews()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("providerKey")).toString() == key)
            imports.append(row);
    }
    done({{QStringLiteral("ok"), true},
          {QStringLiteral("result"), QVariantMap{
              {QStringLiteral("revision"), QVariant::fromValue(revision)},
              {QStringLiteral("dossier"), dossier},
              {QStringLiteral("deliveryRows"), delivery},
              {QStringLiteral("importReviews"), imports}}}});
}

bool validGlobalSetting(const QVariantMap &payload)
{
    const QString key = payload.value(QStringLiteral("key")).toString();
    return (key == QLatin1String("trackerSyncEnabled")
            || key == QLatin1String("checkOnLaunch")
            || key == QLatin1String("backgroundDelivery")
            || key == QLatin1String("completionMessages"))
        && payload.value(QStringLiteral("enabled")).metaType().id() == QMetaType::Bool
        && payload.contains(QStringLiteral("revision"));
}

void setGlobal(ColosseumWebBridge &bridge, const QVariantMap &payload,
               ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1153-1162,3008-3044.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const bool accepted = model->setGlobalSetting(payload.value(QStringLiteral("key")).toString(),
                                                   payload.value(QStringLiteral("enabled")).toBool(),
                                                   revision);
    done(response(accepted, *model, QStringLiteral("Preference saved in Colosseum."),
                  QStringLiteral("Colosseum could not save that preference.")));
}

bool validProviderSetting(const QVariantMap &payload)
{
    const QString setting = payload.value(QStringLiteral("setting")).toString();
    return (setting == QLatin1String("pull") || setting == QLatin1String("send")
            || setting == QLatin1String("live"))
        && !payload.value(QStringLiteral("providerKey")).toString().isEmpty()
        && payload.value(QStringLiteral("enabled")).metaType().id() == QMetaType::Bool
        && payload.contains(QStringLiteral("revision"));
}

void setProvider(ColosseumWebBridge &bridge, const QVariantMap &payload,
                 ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1164-1186,2930-2995.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QString key = payload.value(QStringLiteral("providerKey")).toString();
    const QString setting = payload.value(QStringLiteral("setting")).toString();
    const bool enabled = payload.value(QStringLiteral("enabled")).toBool();
    bool accepted = false;
    if (setting == QLatin1String("pull"))
        accepted = model->setProviderPullAutomatically(key, enabled, revision);
    else if (setting == QLatin1String("send"))
        accepted = model->setProviderSendEnabled(key, enabled, revision);
    else if (setting == QLatin1String("live"))
        accepted = model->setLivePlaybackTrackingEnabled(key, enabled, revision);
    done(response(accepted, *model, QStringLiteral("Tracker preference saved."),
                  QStringLiteral("That tracker preference is unavailable.")));
}

void syncAll(ColosseumWebBridge &bridge, const QVariantMap &payload,
             ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1393-1396. Native owner decides eligibility.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const bool accepted = model->requestSyncAll(revision);
    done(response(accepted, *model, QStringLiteral("Tracker update requested."),
                  QStringLiteral("No connected tracker can update right now.")));
}

bool validBatch(const QVariantMap &payload)
{
    return !payload.value(QStringLiteral("batchId")).toString().isEmpty()
        && payload.contains(QStringLiteral("revision"));
}

void importReview(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:331-376,3303-3714. This snapshot contains
    // public review handles and native-issued choice lists only.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantMap result = model->importReviewSnapshot(
        payload.value(QStringLiteral("batchId")).toString(), revision);
    if (!result.value(QStringLiteral("accepted")).toBool()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("This import review changed. Open it again.")}});
        return;
    }
    done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}});
}

bool validImportChoice(const QVariantMap &payload)
{
    return validBatch(payload)
        && !payload.value(QStringLiteral("itemId")).toString().isEmpty()
        && !payload.value(QStringLiteral("choice")).toString().isEmpty();
}

void resolveImport(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1031-1098. Model checks each item's offered
    // decision; web never fabricates an eligibility rule.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const bool accepted = model->resolveImportItem(
        payload.value(QStringLiteral("batchId")).toString(),
        payload.value(QStringLiteral("itemId")).toString(),
        payload.value(QStringLiteral("choice")).toString(), revision);
    done(response(accepted, *model, QStringLiteral("Import choice saved."),
                  QStringLiteral("That import choice could not be saved.")));
}

bool validImportMany(const QVariantMap &payload)
{
    const QVariantList items = payload.value(QStringLiteral("itemIds")).toList();
    return validBatch(payload) && !items.isEmpty() && items.size() <= 1000
        && !payload.value(QStringLiteral("choice")).toString().isEmpty();
}

QStringList publicIds(const QVariantList &values)
{
    QStringList ids;
    for (const QVariant &value : values) {
        const QString id = value.toString();
        if (id.isEmpty()) return {};
        ids.append(id);
    }
    return ids;
}

void resolveImportMany(ColosseumWebBridge &bridge, const QVariantMap &payload,
                       ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:827-979. Model validates one common choice
    // against all selected items as an atomic review operation.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantList values = payload.value(QStringLiteral("itemIds")).toList();
    const QStringList ids = publicIds(values);
    if (ids.size() != values.size()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Select valid import items and try again.")}});
        return;
    }
    const bool accepted = model->resolveImportItems(
        payload.value(QStringLiteral("batchId")).toString(), ids,
        payload.value(QStringLiteral("choice")).toString(), revision);
    done(response(accepted, *model, QStringLiteral("Import choices saved."),
                  QStringLiteral("Those import choices could not be saved.")));
}

void confirmImport(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1099-1142. Native rejects incomplete pages
    // and unresolved disagreements before any imported Progress/History apply.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    settledAction(bridge, *model, QStringLiteral("confirm_import"),
                  QStringLiteral("pending"),
                  {QStringLiteral("applied"), QStringLiteral("apply_needs_attention")},
                  QStringLiteral("Import confirmed in Colosseum."),
                  QStringLiteral("The import could not finish. Review its current status before retrying."),
                  [model, payload, revision] {
                      return model->confirmImport(payload.value(QStringLiteral("batchId")).toString(),
                                                  revision);
                  }, std::move(done));
}

bool validMatchSearch(const QVariantMap &payload)
{
    return validBatch(payload)
        && !payload.value(QStringLiteral("itemId")).toString().isEmpty()
        && payload.value(QStringLiteral("query")).toString().size() <= 200;
}

void titleMatches(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:402-548. This search returns native-issued
    // candidate handles; a match alone never marks History as watched.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantList rows = model->titleMatchCandidates(
        payload.value(QStringLiteral("batchId")).toString(),
        payload.value(QStringLiteral("itemId")).toString(),
        payload.value(QStringLiteral("query")).toString(), revision);
    if (model->revision() != revision) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("This import review changed. Open it again.")}});
        return;
    }
    done({{QStringLiteral("ok"), true},
          {QStringLiteral("result"), QVariantMap{
              {QStringLiteral("revision"), QVariant::fromValue(revision)},
              {QStringLiteral("items"), rows}}}});
}

bool validMatchChoice(const QVariantMap &payload)
{
    return validBatch(payload)
        && !payload.value(QStringLiteral("itemId")).toString().isEmpty()
        && !payload.value(QStringLiteral("candidateId")).toString().isEmpty();
}

void confirmMatch(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:494-548. Native rechecks the candidate and
    // current review revision before accepting the title match.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const bool accepted = model->confirmTitleMatch(
        payload.value(QStringLiteral("batchId")).toString(),
        payload.value(QStringLiteral("itemId")).toString(),
        payload.value(QStringLiteral("candidateId")).toString(), revision);
    done(response(accepted, *model, QStringLiteral("Title match saved. Review its import choice separately."),
                  QStringLiteral("This title match is no longer available.")));
}

bool validProviderKey(const QVariantMap &payload)
{
    return !payload.value(QStringLiteral("providerKey")).toString().isEmpty()
        && payload.contains(QStringLiteral("revision"));
}

void beginExport(ColosseumWebBridge &bridge, const QVariantMap &payload,
                 ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1398-1418,3071-3302. First-send consent is
    // a separate native review scoped to this profile and remote account.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantMap result = model->beginExportReview(
        payload.value(QStringLiteral("providerKey")).toString(), revision);
    if (!result.value(QStringLiteral("accepted")).toBool()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Colosseum could not prepare a current send review. Try again.")}});
        return;
    }
    done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}});
}

bool validExportChoice(const QVariantMap &payload)
{
    const QVariantList items = payload.value(QStringLiteral("itemIds")).toList();
    return !payload.value(QStringLiteral("reviewId")).toString().isEmpty()
        && payload.contains(QStringLiteral("revision"))
        && !items.isEmpty() && items.size() <= 1000;
}

void confirmExport(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1429-1468. No automatic first export; only
    // selected native preview handles are sent after this confirmation.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantList values = payload.value(QStringLiteral("itemIds")).toList();
    const QStringList ids = publicIds(values);
    if (ids.size() != values.size()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Select valid updates and try again.")}});
        return;
    }
    const bool accepted = model->confirmExportReview(
        payload.value(QStringLiteral("reviewId")).toString(), ids, revision);
    done(response(accepted, *model, QStringLiteral("Selected Colosseum updates queued for this tracker."),
                  QStringLiteral("The send review changed. Open it again.")));
}

void diagnose(ColosseumWebBridge &bridge, const QVariantMap &payload,
              ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:1219-1303,2838-2921. Return only the
    // model's public route; no delivery payload or remote identity escapes.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const QVariantMap route = model->diagnoseRoute(
        payload.value(QStringLiteral("providerKey")).toString());
    if (!route.value(QStringLiteral("accepted")).toBool() || model->revision() != revision) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("That tracker issue changed. Open its current status again.")}});
        return;
    }
    done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), route}});
}

bool validDisconnect(const QVariantMap &payload)
{
    const QString choice = payload.value(QStringLiteral("choice")).toString();
    return validProviderKey(payload)
        && (choice == QLatin1String("keep_paused")
            || choice == QLatin1String("discard_known_unsent"));
}

void disconnect(ColosseumWebBridge &bridge, const QVariantMap &payload,
                ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:696-800,3952-4132. Native rechecks pending
    // and uncertain work; the web panel never performs queue surgery.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    const bool accepted = model->disconnectTracker(
        payload.value(QStringLiteral("providerKey")).toString(),
        payload.value(QStringLiteral("choice")).toString(), revision);
    done(response(accepted, *model,
                  QStringLiteral("Tracker disconnected. Uncertain deliveries remain paused for recovery."),
                  QStringLiteral("The disconnect could not finish. Check the current pending work.")));
}

void removeImported(ColosseumWebBridge &bridge, const QVariantMap &payload,
                    ActionRegistry::Completion done)
{
    // TrackerSyncCenterPage.qml:565-695,4258-4470. The model removes only
    // eligible tracker-imported Progress and that source's History evidence.
    auto *model = owner(bridge, done);
    if (!model) return;
    quint64 revision = 0;
    if (!expectedRevision(payload, *model, done, &revision)) return;
    settledAction(bridge, *model, QStringLiteral("remove_imported_tracker_data"),
                  QStringLiteral("removal_pending"),
                  {QStringLiteral("removed"), QStringLiteral("progress_removal_failed"),
                   QStringLiteral("progress_removed_history_removal_failed")},
                  QStringLiteral("Imported tracker Progress and History evidence were removed. Native data and other providers remain."),
                  QStringLiteral("Imported data removal did not finish. Reopen the preview to see what remains."),
                  [model, payload, revision] {
                      return model->removeImportedData(
                          payload.value(QStringLiteral("providerKey")).toString(), revision);
                  }, std::move(done));
}

const bool dossierRegistered = ActionRegistry::add({QStringLiteral("page.connections.dossier"),
                                                     validRead, readDossier});
const bool globalRegistered = ActionRegistry::add({QStringLiteral("page.connections.globalSetting"),
                                                    validGlobalSetting, setGlobal});
const bool providerRegistered = ActionRegistry::add({QStringLiteral("page.connections.providerSetting"),
                                                      validProviderSetting, setProvider});
const bool syncRegistered = ActionRegistry::add({QStringLiteral("page.connections.syncAll"),
                                                  [](const QVariantMap &payload) {
                                                      return payload.contains(QStringLiteral("revision"));
                                                  }, syncAll});
const bool importRegistered = ActionRegistry::add({QStringLiteral("page.connections.importReview"),
                                                    validBatch, importReview});
const bool importChoiceRegistered = ActionRegistry::add({QStringLiteral("page.connections.resolveImport"),
                                                          validImportChoice, resolveImport});
const bool importManyRegistered = ActionRegistry::add({QStringLiteral("page.connections.resolveImportMany"),
                                                        validImportMany, resolveImportMany});
const bool importConfirmRegistered = ActionRegistry::add({QStringLiteral("page.connections.confirmImport"),
                                                           validBatch, confirmImport});
const bool matchSearchRegistered = ActionRegistry::add({QStringLiteral("page.connections.titleMatches"),
                                                         validMatchSearch, titleMatches});
const bool matchConfirmRegistered = ActionRegistry::add({QStringLiteral("page.connections.confirmTitleMatch"),
                                                          validMatchChoice, confirmMatch});
const bool exportBeginRegistered = ActionRegistry::add({QStringLiteral("page.connections.beginExport"),
                                                         validProviderKey, beginExport});
const bool exportConfirmRegistered = ActionRegistry::add({QStringLiteral("page.connections.confirmExport"),
                                                           validExportChoice, confirmExport});
const bool diagnoseRegistered = ActionRegistry::add({QStringLiteral("page.connections.diagnose"),
                                                      validProviderKey, diagnose});
const bool disconnectRegistered = ActionRegistry::add({QStringLiteral("page.connections.disconnect"),
                                                        validDisconnect, disconnect});
const bool removeRegistered = ActionRegistry::add({QStringLiteral("page.connections.removeImported"),
                                                    validProviderKey, removeImported});
} // namespace
