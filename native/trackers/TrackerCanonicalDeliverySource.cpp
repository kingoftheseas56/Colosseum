#include "TrackerCanonicalDeliverySource.h"

#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include "ProgressStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace {

TrackerDeliveryOrigin originFrom(const QString &origin)
{
    if (origin == QLatin1String("native_local"))
        return TrackerDeliveryOrigin::NativeLocal;
    if (origin == QLatin1String("tracker_import"))
        return TrackerDeliveryOrigin::TrackerImport;
    if (origin == QLatin1String("account_sync"))
        return TrackerDeliveryOrigin::AccountSync;
    return TrackerDeliveryOrigin::Unknown;
}

TrackerMediaDomain mediaDomainFor(const QString &kind, const QString &id)
{
    if (kind == QLatin1String("anime") || kind.startsWith(QLatin1String("anime_")))
        return TrackerMediaDomain::Anime;
    if (kind == QLatin1String("manga") || kind.startsWith(QLatin1String("manga_"))
        || kind == QLatin1String("manga_chapter")) {
        return TrackerMediaDomain::Manga;
    }
    if (kind == QLatin1String("movie") || kind == QLatin1String("film"))
        return TrackerMediaDomain::Movie;
    if (kind == QLatin1String("episode") || kind == QLatin1String("series")
        || kind == QLatin1String("television") || kind == QLatin1String("tv")) {
        return TrackerMediaDomain::Television;
    }
    if (kind == QLatin1String("video")) {
        return id.count(QLatin1Char(':')) >= 2
            ? TrackerMediaDomain::Television
            : TrackerMediaDomain::Movie;
    }
    return TrackerMediaDomain::Unknown;
}

QString fingerprint(const QVariantMap &material)
{
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(material))
        .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
}

QString canonicalId(const QString &kind, const QString &id)
{
    return kind + QLatin1Char(':') + id;
}

std::optional<TrackerDeliveryFact> progressFactFromEntry(const QVariantMap &entry)
{
    const QString kind = entry.value(QStringLiteral("kind")).toString();
    const QString id = entry.value(QStringLiteral("id")).toString();
    bool revisionOk = false;
    const qint64 updatedAt = entry.value(QStringLiteral("updatedAt")).toLongLong(&revisionOk);
    bool progressOk = false;
    const double progress = entry.value(QStringLiteral("progress")).toDouble(&progressOk);
    if (kind.isEmpty() || id.isEmpty() || !revisionOk || updatedAt <= 0
        || !progressOk || !std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        return std::nullopt;

    TrackerDeliveryFact fact;
    fact.canonicalMediaId = canonicalId(kind, id);
    fact.historyKind = kind;
    fact.historyId = id;
    fact.kind = TrackerDeliveryFactKind::Progress;
    fact.sourceRevision = static_cast<quint64>(updatedAt);
    fact.progress = qBound(0, qRound(progress * 100.0), 100);
    fact.origin = originFrom(entry.value(QStringLiteral("_trackerOrigin")).toString());
    fact.mediaDomain = mediaDomainFor(kind, id);
    QVariantMap binding{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("updatedAt"), updatedAt},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("watched"), entry.value(QStringLiteral("watched")).toBool()},
        {QStringLiteral("origin"), static_cast<int>(fact.origin)}
    };
    fact.contentFingerprint = fingerprint(binding);
    return fact;
}

std::optional<TrackerDeliveryFact> completionFactFromEvent(const QVariantMap &event)
{
    if (event.value(QStringLiteral("type")).toString() != QLatin1String("media_completed"))
        return std::nullopt;
    const QString kind = event.value(QStringLiteral("kind")).toString();
    const QString id = event.value(QStringLiteral("itemKey")).toString();
    const QString eventId = event.value(QStringLiteral("eventId")).toString();
    bool atOk = false;
    const qint64 atMs = event.value(QStringLiteral("atMs")).toLongLong(&atOk);
    if (kind.isEmpty() || id.isEmpty() || eventId.isEmpty() || !atOk || atMs <= 0)
        return std::nullopt;

    TrackerDeliveryFact fact;
    fact.canonicalMediaId = canonicalId(kind, id);
    fact.historyKind = kind;
    fact.historyId = id;
    fact.kind = TrackerDeliveryFactKind::Completion;
    fact.sourceRevision = static_cast<quint64>(atMs);
    fact.sourceEventId = eventId;
    fact.progress = 100;
    fact.origin = originFrom(event.value(QStringLiteral("_trackerOrigin")).toString());
    fact.mediaDomain = mediaDomainFor(kind, id);
    QVariantMap binding{
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("itemKey"), id},
        {QStringLiteral("atMs"), atMs},
        {QStringLiteral("origin"), static_cast<int>(fact.origin)}
    };
    fact.contentFingerprint = fingerprint(binding);
    return fact;
}

std::optional<TrackerDeliveryFact> completionFactFromHistoryEvidence(
    const QVariantMap &event)
{
    const QString kind = event.value(QStringLiteral("kind")).toString();
    const QString id = event.value(QStringLiteral("id")).toString();
    const QString eventId = event.value(QStringLiteral("eventId")).toString();
    bool atOk = false;
    const qint64 atMs = event.value(QStringLiteral("atMs")).toLongLong(&atOk);
    if (kind.isEmpty() || id.isEmpty() || eventId.isEmpty() || !atOk || atMs <= 0)
        return std::nullopt;

    TrackerDeliveryFact fact;
    fact.canonicalMediaId = canonicalId(kind, id);
    fact.historyKind = kind;
    fact.historyId = id;
    fact.kind = TrackerDeliveryFactKind::Completion;
    fact.sourceRevision = static_cast<quint64>(atMs);
    fact.sourceEventId = eventId;
    fact.progress = 100;
    fact.origin = TrackerDeliveryOrigin::NativeLocal;
    fact.mediaDomain = mediaDomainFor(kind, id);
    QVariantMap binding{
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("atMs"), atMs},
        {QStringLiteral("origin"), static_cast<int>(fact.origin)}};
    const QString activityEventId = event.value(QStringLiteral("activityEventId")).toString();
    if (!activityEventId.isEmpty())
        binding.insert(QStringLiteral("activityEventId"), activityEventId);
    const QString activitySessionId = event.value(QStringLiteral("activitySessionId")).toString();
    if (!activitySessionId.isEmpty())
        binding.insert(QStringLiteral("activitySessionId"), activitySessionId);
    fact.contentFingerprint = fingerprint(binding);
    return fact;
}

std::optional<TrackerDeliveryFact> completionFactFromHistoryRecord(
    const QVariantMap &record)
{
    const QString kind = record.value(QStringLiteral("kind")).toString();
    const QString id = record.value(QStringLiteral("id")).toString();
    bool atOk = false;
    const qint64 atMs = record.value(QStringLiteral("completedAt")).toLongLong(&atOk);
    if (kind.isEmpty() || id.isEmpty() || !atOk || atMs <= 0)
        return std::nullopt;

    const QString material = kind + QChar(0x1f) + id + QChar(0x1f) + QString::number(atMs);
    const QString eventId = QStringLiteral("history-record-")
        + QString::fromLatin1(QCryptographicHash::hash(
              material.toUtf8(), QCryptographicHash::Sha256).toHex());
    TrackerDeliveryFact fact;
    fact.canonicalMediaId = canonicalId(kind, id);
    fact.historyKind = kind;
    fact.historyId = id;
    fact.kind = TrackerDeliveryFactKind::Completion;
    fact.sourceRevision = static_cast<quint64>(atMs);
    fact.sourceEventId = eventId;
    fact.progress = 100;
    fact.origin = TrackerDeliveryOrigin::Unknown;
    fact.mediaDomain = mediaDomainFor(kind, id);
    fact.contentFingerprint = fingerprint({
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("atMs"), atMs},
        {QStringLiteral("origin"), static_cast<int>(fact.origin)}});
    return fact;
}

QString historyItemKey(const QString &kind, const QString &id)
{
    return kind + QChar(0x1f) + id;
}

bool compatibleVideoCompletionKinds(const QString &left, const QString &right)
{
    const auto isVideoKind = [](const QString &kind) {
        return kind == QLatin1String("video")
            || kind == QLatin1String("movie")
            || kind == QLatin1String("episode");
    };
    return left == right || (isVideoKind(left) && isVideoKind(right));
}

bool activityCompletionPrecedes(const QVariantMap &candidate,
                                const QVariantMap &completion)
{
    if (candidate.value(QStringLiteral("type")).toString()
            != QLatin1String("media_completed")
        || completion.value(QStringLiteral("type")).toString()
            != QLatin1String("media_completed")
        || !candidate.value(QStringLiteral("syncable")).toBool()
        || !completion.value(QStringLiteral("syncable")).toBool()
        || candidate.value(QStringLiteral("_trackerOrigin")).toString()
            != QLatin1String("native_local")
        || completion.value(QStringLiteral("_trackerOrigin")).toString()
            != QLatin1String("native_local")
        || candidate.value(QStringLiteral("sessionId")).toString().isEmpty()
        || candidate.value(QStringLiteral("sessionId")).toString()
            != completion.value(QStringLiteral("sessionId")).toString()
        || candidate.value(QStringLiteral("itemKey")).toString()
            != completion.value(QStringLiteral("itemKey")).toString()
        || !compatibleVideoCompletionKinds(
            candidate.value(QStringLiteral("kind")).toString(),
            completion.value(QStringLiteral("kind")).toString()))
        return false;

    const QString candidateId = candidate.value(QStringLiteral("eventId")).toString();
    const QString completionId = completion.value(QStringLiteral("eventId")).toString();
    if (candidateId.isEmpty() || completionId.isEmpty() || candidateId == completionId)
        return false;

    const bool candidateGuarded = candidate.value(QStringLiteral("reason")).toString()
        == QLatin1String("guarded_90_percent");
    const bool completionGuarded = completion.value(QStringLiteral("reason")).toString()
        == QLatin1String("guarded_90_percent");
    if (candidateGuarded != completionGuarded)
        return candidateGuarded;

    bool candidateAtOk = false;
    bool completionAtOk = false;
    const qint64 candidateAt = candidate.value(QStringLiteral("atMs")).toLongLong(&candidateAtOk);
    const qint64 completionAt = completion.value(QStringLiteral("atMs")).toLongLong(&completionAtOk);
    if (!candidateAtOk || !completionAtOk || candidateAt <= 0 || completionAt <= 0)
        return false;
    return candidateAt < completionAt
        || (candidateAt == completionAt && candidateId < completionId);
}

bool sameFact(const TrackerDeliveryFact &left, const TrackerDeliveryFact &right)
{
    return left.canonicalMediaId == right.canonicalMediaId
        && left.historyKind == right.historyKind && left.historyId == right.historyId
        && left.kind == right.kind && left.sourceRevision == right.sourceRevision
        && left.sourceEventId == right.sourceEventId && left.progress == right.progress
        && left.contentFingerprint == right.contentFingerprint && left.origin == right.origin
        && left.mediaDomain == right.mediaDomain;
}

} // namespace

TrackerCanonicalDeliverySource::TrackerCanonicalDeliverySource(ProgressStore *progress,
                                                               ActivityStore *activity,
                                                               HistoryStore *history)
    : m_progress(progress), m_activity(activity), m_history(history)
{}

bool TrackerCanonicalDeliverySource::isReady() const
{
    return buildFacts().has_value();
}

QList<TrackerDeliveryFact> TrackerCanonicalDeliverySource::currentCommittedFacts() const
{
    const auto facts = buildFacts();
    return facts ? *facts : QList<TrackerDeliveryFact>{};
}

bool TrackerCanonicalDeliverySource::isDurablyCurrent(const TrackerDeliveryFact &fact) const
{
    if (!m_progress || !m_activity || !m_history || !m_progress->healthy()
        || !m_activity->healthy() || !m_history->healthy()
        || !m_history->trackerEvidenceHealthy())
        return false;
    if (fact.kind == TrackerDeliveryFactKind::Progress) {
        if (!m_progress->deliveryEntryDurable(fact.historyKind, fact.historyId))
            return false;
        const auto current = currentProgressFact(fact.historyKind, fact.historyId);
        return current && sameFact(*current, fact);
    }
    std::optional<TrackerDeliveryFact> current;
    if (fact.sourceEventId.startsWith(QLatin1String("history-progress-"))) {
        current = currentHistoryCompletionFact(fact.sourceEventId);
    } else if (fact.sourceEventId.startsWith(QLatin1String("history-record-"))) {
        current = completionFactFromHistoryRecord(
            m_history->get(fact.historyKind, fact.historyId));
    } else {
        current = currentActivityCompletionFact(fact.sourceEventId);
    }
    return current && sameFact(*current, fact);
}

std::optional<TrackerDeliveryFact> TrackerCanonicalDeliverySource::currentProgressFact(
    const QString &kind, const QString &id) const
{
    if (!m_progress || !m_progress->healthy()
        || !m_progress->deliveryEntryDurable(kind, id))
        return std::nullopt;
    return progressFactFromEntry(m_progress->deliveryEntry(kind, id));
}

std::optional<TrackerDeliveryFact> TrackerCanonicalDeliverySource::currentActivityCompletionFact(
    const QString &eventId) const
{
    if (!m_activity || !m_activity->healthy())
        return std::nullopt;
    const QVariantMap event = m_activity->historyProjectionFact(eventId);
    if (!event.value(QStringLiteral("syncable")).toBool()
        || hasEarlierSessionCompletionForActivity(event))
        return std::nullopt;
    return completionFactFromEvent(event);
}

bool TrackerCanonicalDeliverySource::hasEarlierSessionCompletionForActivity(
    const QVariantMap &activityEvent) const
{
    if (!m_activity || !m_activity->healthy()
        || !m_history || !m_history->healthy() || !m_history->trackerEvidenceHealthy()
        || activityEvent.value(QStringLiteral("type")).toString()
            != QLatin1String("media_completed")
        || !activityEvent.value(QStringLiteral("syncable")).toBool()
        || activityEvent.value(QStringLiteral("_trackerOrigin")).toString()
            != QLatin1String("native_local"))
        return false;

    const auto activityFact = completionFactFromEvent(activityEvent);
    const QString activitySessionId = activityEvent.value(QStringLiteral("sessionId")).toString();
    bool activityAtOk = false;
    const qint64 activityAtMs = activityEvent.value(QStringLiteral("atMs")).toLongLong(&activityAtOk);
    if (!activityFact || activityFact->origin != TrackerDeliveryOrigin::NativeLocal
        || activitySessionId.isEmpty() || !activityAtOk || activityAtMs <= 0)
        return false;

    for (const QVariantMap &candidate : m_activity->historyProjectionFacts()) {
        if (activityCompletionPrecedes(candidate, activityEvent))
            return true;
    }

    const QVariantList historyFacts = m_history->trackerLocalCompletionFacts();
    for (const QVariant &value : historyFacts) {
        const QVariantMap historyEvent = value.toMap();
        bool historyAtOk = false;
        const qint64 historyAtMs = historyEvent.value(QStringLiteral("atMs"))
            .toLongLong(&historyAtOk);
        // Linked History evidence is a copy of its specific Activity event. It
        // cannot erase an earlier unlinked Progress completion in this session.
        if (!historyEvent.value(QStringLiteral("activityEventId")).toString().isEmpty()
            || historyEvent.value(QStringLiteral("activitySessionId")).toString()
                != activitySessionId
            || historyEvent.value(QStringLiteral("id")).toString() != activityFact->historyId
            || !compatibleVideoCompletionKinds(
                historyEvent.value(QStringLiteral("kind")).toString(),
                activityFact->historyKind)
            || !historyAtOk || historyAtMs <= 0 || historyAtMs > activityAtMs)
            continue;
        return true;
    }
    return false;
}

bool TrackerCanonicalDeliverySource::hasEarlierSessionCompletionForHistory(
    const QVariantMap &historyEvent) const
{
    if (!m_activity || !m_activity->healthy())
        return false;

    const QString activityEventId = historyEvent.value(QStringLiteral("activityEventId")).toString();
    if (!activityEventId.isEmpty()) {
        const QVariantMap activityEvent = m_activity->historyProjectionFact(activityEventId);
        const auto activityFact = completionFactFromEvent(activityEvent);
        if (activityFact && activityFact->origin == TrackerDeliveryOrigin::NativeLocal
            && activityEvent.value(QStringLiteral("syncable")).toBool()
            && compatibleVideoCompletionKinds(
                activityFact->historyKind,
                historyEvent.value(QStringLiteral("kind")).toString())
            && activityFact->historyId == historyEvent.value(QStringLiteral("id")).toString()
            && (historyEvent.value(QStringLiteral("activitySessionId")).toString().isEmpty()
                || historyEvent.value(QStringLiteral("activitySessionId")).toString()
                    == activityEvent.value(QStringLiteral("sessionId")).toString()))
            return true;
    }

    const QString activitySessionId =
        historyEvent.value(QStringLiteral("activitySessionId")).toString();
    if (activitySessionId.isEmpty())
        return false;
    const QString historyKind = historyEvent.value(QStringLiteral("kind")).toString();
    const QString historyId = historyEvent.value(QStringLiteral("id")).toString();
    bool historyAtOk = false;
    const qint64 historyAtMs = historyEvent.value(QStringLiteral("atMs")).toLongLong(&historyAtOk);
    if (!historyAtOk || historyAtMs <= 0)
        return false;
    for (const QVariantMap &event : m_activity->historyProjectionFacts()) {
        bool activityAtOk = false;
        const qint64 activityAtMs = event.value(QStringLiteral("atMs")).toLongLong(&activityAtOk);
        if (event.value(QStringLiteral("type")).toString() != QLatin1String("media_completed")
            || !event.value(QStringLiteral("syncable")).toBool()
            || event.value(QStringLiteral("_trackerOrigin")).toString()
                != QLatin1String("native_local")
            || event.value(QStringLiteral("sessionId")).toString() != activitySessionId
            || event.value(QStringLiteral("itemKey")).toString() != historyId
            || !compatibleVideoCompletionKinds(
                event.value(QStringLiteral("kind")).toString(), historyKind)
            || !activityAtOk || activityAtMs <= 0
            || activityAtMs >= historyAtMs)
            continue;
        return true;
    }

    const QString historyEventId = historyEvent.value(QStringLiteral("eventId")).toString();
    for (const QVariant &value : m_history->trackerLocalCompletionFacts()) {
        const QVariantMap earlierHistoryEvent = value.toMap();
        bool earlierAtOk = false;
        const qint64 earlierAtMs = earlierHistoryEvent.value(QStringLiteral("atMs"))
            .toLongLong(&earlierAtOk);
        if (earlierHistoryEvent.value(QStringLiteral("eventId")).toString() == historyEventId
            || !earlierHistoryEvent.value(QStringLiteral("activityEventId")).toString().isEmpty()
            || earlierHistoryEvent.value(QStringLiteral("activitySessionId")).toString()
                != activitySessionId
            || earlierHistoryEvent.value(QStringLiteral("id")).toString() != historyId
            || !compatibleVideoCompletionKinds(
                earlierHistoryEvent.value(QStringLiteral("kind")).toString(), historyKind)
            || !earlierAtOk || earlierAtMs <= 0 || earlierAtMs >= historyAtMs)
            continue;
        return true;
    }
    return false;
}

std::optional<TrackerDeliveryFact> TrackerCanonicalDeliverySource::currentHistoryCompletionFact(
    const QString &eventId) const
{
    if (!m_history || !m_activity || !m_history->healthy()
        || !m_history->trackerEvidenceHealthy())
        return std::nullopt;
    const QVariantMap event = m_history->trackerLocalCompletionFact(eventId);
    const QString kind = event.value(QStringLiteral("kind")).toString();
    const QString id = event.value(QStringLiteral("id")).toString();
    if (kind.isEmpty() || id.isEmpty()
        || m_history->get(kind, id).value(QStringLiteral("completedAt")).toLongLong() <= 0
        || hasEarlierSessionCompletionForHistory(event))
        return std::nullopt;
    return completionFactFromHistoryEvidence(event);
}

std::optional<QList<TrackerDeliveryFact>> TrackerCanonicalDeliverySource::buildFacts() const
{
    if (!m_progress || !m_activity || !m_history || !m_progress->healthy()
        || !m_activity->healthy() || !m_history->healthy()
        || !m_history->trackerEvidenceHealthy())
        return std::nullopt;

    QList<TrackerDeliveryFact> facts;
    for (const QVariant &value : m_progress->deliveryEntries()) {
        const auto fact = progressFactFromEntry(value.toMap());
        if (!fact)
            return std::nullopt;
        facts.append(*fact);
    }

    QHash<QString, QVariantList> localHistoryFactsByItem;
    for (const QVariant &value : m_history->trackerLocalCompletionFacts()) {
        const QVariantMap event = value.toMap();
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString id = event.value(QStringLiteral("id")).toString();
        if (kind.isEmpty() || id.isEmpty())
            return std::nullopt;
        localHistoryFactsByItem[historyItemKey(kind, id)].append(event);
    }

    QSet<QString> activityCompletionItems;
    for (const QVariantMap &event : m_activity->historyProjectionFacts()) {
        if (!event.value(QStringLiteral("syncable")).toBool())
            continue;
        const auto fact = completionFactFromEvent(event);
        if (!fact) {
            if (event.value(QStringLiteral("type")).toString() == QLatin1String("media_completed"))
                return std::nullopt;
            continue;
        }
        if (fact->kind == TrackerDeliveryFactKind::Completion
            && hasEarlierSessionCompletionForActivity(event))
            continue;
        activityCompletionItems.insert(historyItemKey(fact->historyKind, fact->historyId));
        facts.append(*fact);
    }

    for (const QVariant &value : m_history->records()) {
        const QVariantMap record = value.toMap();
        const QString kind = record.value(QStringLiteral("kind")).toString();
        const QString id = record.value(QStringLiteral("id")).toString();
        const QString key = historyItemKey(kind, id);
        const QVariantList localFacts = localHistoryFactsByItem.value(key);
        if (!localFacts.isEmpty()) {
            // Suppress only the History copy linked to the exact native Activity
            // event or playback session. Other sessions may be genuine rewatches.
            for (const QVariant &localFact : localFacts) {
                const QVariantMap historyEvent = localFact.toMap();
                if (hasEarlierSessionCompletionForHistory(historyEvent))
                    continue;
                const auto fact = completionFactFromHistoryEvidence(historyEvent);
                if (!fact)
                    return std::nullopt;
                facts.append(*fact);
            }
            continue;
        }
        if (activityCompletionItems.contains(key))
            continue;
        const auto fact = completionFactFromHistoryRecord(record);
        if (fact)
            facts.append(*fact); // legacy/account-merged History stays visible but ineligible
    }

    return facts;
}
