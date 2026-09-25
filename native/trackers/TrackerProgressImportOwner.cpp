#include "TrackerProgressImportOwner.h"

#include "ProgressStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

QString effectDigest(const QString &operationId,
                     const TrackerTitleMapping &mapping,
                     const TrackerImportProgressTarget &target,
                     int progress,
                     bool completed)
{
    const QJsonObject effect{
        {QStringLiteral("operationId"), operationId},
        {QStringLiteral("canonicalMediaId"), mapping.canonical.canonicalMediaId},
        {QStringLiteral("historyKind"), mapping.canonical.historyKind},
        {QStringLiteral("historyId"), mapping.canonical.historyId},
        {QStringLiteral("providerProgress"), progress},
        {QStringLiteral("providerCompleted"), completed},
        {QStringLiteral("targetKind"), target.kind},
        {QStringLiteral("targetId"), target.id},
        {QStringLiteral("targetFraction"), target.fraction},
        {QStringLiteral("targetCompleted"), target.completed}};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(effect).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

QString safeProgressTitle(const TrackerTitleMapping &mapping)
{
    return mapping.canonical.displayName.trimmed();
}

QString safePresentationText(const QString &value, int maximum)
{
    QString safe;
    safe.reserve(qMin(value.size(), maximum));
    bool previousWasSpace = true;
    for (const QChar character : value) {
        if (character.isSpace() || character.isNull()
            || character.category() == QChar::Other_Control) {
            if (!previousWasSpace && safe.size() < maximum) {
                safe.append(QLatin1Char(' '));
                previousWasSpace = true;
            }
            continue;
        }
        if (safe.size() >= maximum)
            break;
        safe.append(character);
        previousWasSpace = false;
    }
    return safe.trimmed();
}

QString safeProgressKind(const QString &kind)
{
    if (kind == QLatin1String("video"))
        return QStringLiteral("Video");
    if (kind == QLatin1String("book"))
        return QStringLiteral("Book");
    if (kind == QLatin1String("manga"))
        return QStringLiteral("Manga");
    return {};
}

} // namespace

TrackerProgressImportOwner::TrackerProgressImportOwner(ProgressStore *progress)
    : m_progress(progress)
{}

std::optional<TrackerImportedProgressValue>
TrackerProgressImportOwner::currentProgress(const TrackerTitleMapping &) const
{
    // A canonical title mapping alone does not identify a Continue record.
    // Callers must provide the exact target returned by the provider adapter.
    return std::nullopt;
}

std::optional<TrackerImportedProgressValue>
TrackerProgressImportOwner::currentProgress(
    const TrackerTitleMapping &mapping,
    const TrackerImportProgressTarget &target) const
{
    if (!m_progress || !m_progress->healthy() || !validTarget(mapping, target))
        return std::nullopt;
    const QVariantMap entry = m_progress->deliveryEntry(target.kind, target.id);
    if (entry.isEmpty())
        return std::nullopt;
    return importedValue(mapping, target, 0,
                         entry.value(QStringLiteral("watched")).toBool(),
                         false, entry);
}

TrackerImportOwnerApplyResult TrackerProgressImportOwner::applyImportedProgress(
    const QString &, const TrackerTitleMapping &,
    const std::optional<TrackerImportedProgressValue> &, int, bool,
    std::optional<TrackerImportedProgressValue> *, QString *error)
{
    if (error)
        *error = QStringLiteral("Tracker Progress imports require an exact target and asynchronous durable receipt.");
    return TrackerImportOwnerApplyResult::Unsupported;
}

TrackerImportOwnerApplyResult TrackerProgressImportOwner::applyImportedProgressWithTarget(
    const QString &, const TrackerTitleMapping &,
    const TrackerImportProgressTarget &,
    const std::optional<TrackerImportedProgressValue> &, int, bool,
    std::optional<TrackerImportedProgressValue> *, QString *error)
{
    if (error)
        *error = QStringLiteral("Tracker Progress imports require asynchronous durable receipt.");
    return TrackerImportOwnerApplyResult::Unsupported;
}

void TrackerProgressImportOwner::applyImportedProgressAsync(
    const QString &operationId,
    const TrackerTitleMapping &mapping,
    const TrackerImportProgressTarget &target,
    const std::optional<TrackerImportedProgressValue> &expectedLocal,
    int providerProgress,
    bool providerCompleted,
    ApplyCallback callback)
{
    if (!m_progress || !validTarget(mapping, target) || operationId.trimmed().isEmpty()
        || providerProgress < 0 || !std::isfinite(target.fraction)) {
        if (callback)
            callback(TrackerImportOwnerApplyResult::Unsupported, std::nullopt,
                     QStringLiteral("The provider fact has no exact representable Colosseum Progress target."));
        return;
    }

    if (m_progress->trackerProgressSourceRemovalSuppressed(
            trackerProviderKey(mapping.remote.providerId),
            mapping.remote.remoteAccountId)) {
        if (callback)
            callback(TrackerImportOwnerApplyResult::Stale, std::nullopt,
                     QStringLiteral("Tracker Progress from this source was removed and is suppressed."));
        return;
    }

    const QVariantMap currentEntry = m_progress->deliveryEntry(target.kind, target.id);
    const bool expectedPresent = expectedLocal.has_value();

    const QString revisionToken = expectedPresent
        ? ProgressStore::trackerImportRevisionToken(currentEntry) : QString();
    if (expectedPresent
        && (currentEntry.isEmpty() || expectedLocal->ownerRevisionToken.isEmpty()
            || expectedLocal->ownerRevisionToken != revisionToken
            || !expectedLocal->exactProgressTarget
            || !qFuzzyCompare(expectedLocal->exactProgressTarget->fraction + 1.0,
                              currentEntry.value(QStringLiteral("progress")).toDouble() + 1.0)
            || expectedLocal->exactProgressTarget->completed
                != currentEntry.value(QStringLiteral("watched")).toBool())) {
        if (callback)
            callback(TrackerImportOwnerApplyResult::Stale, std::nullopt,
                     QStringLiteral("Colosseum Progress changed after the tracker preview."));
        return;
    }

    const qint64 updatedAt = expectedPresent
        ? currentEntry.value(QStringLiteral("updatedAt")).toLongLong() : 0;
    const double currentFraction = expectedPresent
        ? currentEntry.value(QStringLiteral("progress")).toDouble() : 0.0;
    const bool currentWatched = expectedPresent
        && currentEntry.value(QStringLiteral("watched")).toBool();
    QVariantMap importedEntry = currentEntry;
    importedEntry.insert(QStringLiteral("kind"), target.kind);
    importedEntry.insert(QStringLiteral("id"), target.id);
    importedEntry.insert(QStringLiteral("_trackerProviderKey"),
                         trackerProviderKey(mapping.remote.providerId));
    importedEntry.insert(QStringLiteral("_trackerRemoteAccountId"),
                         mapping.remote.remoteAccountId);
    if (importedEntry.value(QStringLiteral("title")).toString().isEmpty())
        importedEntry.insert(QStringLiteral("title"), safeProgressTitle(mapping));
    if (importedEntry.value(QStringLiteral("caption")).toString().isEmpty())
        importedEntry.insert(QStringLiteral("caption"), safeProgressTitle(mapping));
    importedEntry.insert(QStringLiteral("progress"), target.fraction);
    // Provider completion updates only Progress's watched bit. This import
    // path intentionally bypasses completionCrossed(), History, and Activity.
    importedEntry.insert(QStringLiteral("watched"), target.completed);
    importedEntry.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());

    QPointer<ProgressStore> progress = m_progress;
    const QString digest = effectDigest(operationId, mapping, target,
                                        providerProgress, providerCompleted);
    m_progress->applyTrackerImportedEntryAsync(
        operationId, digest, target.kind, target.id, expectedPresent,
        revisionToken, updatedAt, currentFraction, currentWatched, importedEntry,
        [progress, mapping, target, providerProgress, providerCompleted,
         nativeWitnessed = expectedLocal && expectedLocal->nativeWitnessedHistory,
         callback = std::move(callback)](
            ProgressStore::TrackerImportApplyStatus status,
            const QVariantMap &entry,
            const QString &error) mutable {
            if (!progress) {
                if (callback)
                    callback(TrackerImportOwnerApplyResult::Failed, std::nullopt,
                             QStringLiteral("The active profile changed before the Progress receipt completed."));
                return;
            }
            TrackerImportOwnerApplyResult ownerStatus = TrackerImportOwnerApplyResult::Failed;
            switch (status) {
            case ProgressStore::TrackerImportApplyStatus::Applied:
                ownerStatus = TrackerImportOwnerApplyResult::Applied;
                break;
            case ProgressStore::TrackerImportApplyStatus::AlreadyApplied:
                ownerStatus = TrackerImportOwnerApplyResult::AlreadyApplied;
                break;
            case ProgressStore::TrackerImportApplyStatus::Stale:
                ownerStatus = TrackerImportOwnerApplyResult::Stale;
                break;
            case ProgressStore::TrackerImportApplyStatus::Failed:
                ownerStatus = TrackerImportOwnerApplyResult::Failed;
                break;
            }
            std::optional<TrackerImportedProgressValue> resulting;
            if (ownerStatus == TrackerImportOwnerApplyResult::Applied
                || ownerStatus == TrackerImportOwnerApplyResult::AlreadyApplied) {
                resulting = importedValue(mapping, target, providerProgress,
                                          providerCompleted, nativeWitnessed, entry);
            }
            if (callback)
                callback(ownerStatus, std::move(resulting), error);
        });
}

int TrackerProgressImportOwner::importedProgressCount(
    TrackerProviderId providerId,
    const QString &remoteAccountId) const
{
    if (!m_progress || !m_progress->healthy()
        || remoteAccountId.trimmed().isEmpty()) {
        return 0;
    }
    return m_progress->trackerImportedProgressCount(
        trackerProviderKey(providerId), remoteAccountId);
}

QVariantList TrackerProgressImportOwner::importedProgressRemovalPreview(
    TrackerProviderId providerId,
    const QString &remoteAccountId) const
{
    const QString providerKey = trackerProviderKey(providerId);
    if (!m_progress || !m_progress->healthy() || providerKey.isEmpty()
        || remoteAccountId.trimmed().isEmpty()) {
        return {};
    }
    return m_progress->trackerImportedProgressRemovalPreview(providerKey,
                                                              remoteAccountId);
}

bool TrackerProgressImportOwner::importedProgressSourceRemovalSuppressed(
    TrackerProviderId providerId,
    const QString &remoteAccountId) const
{
    const QString providerKey = trackerProviderKey(providerId);
    return m_progress && m_progress->healthy() && !providerKey.isEmpty()
        && !remoteAccountId.trimmed().isEmpty()
        && m_progress->trackerProgressSourceRemovalSuppressed(providerKey,
                                                              remoteAccountId);
}

void TrackerProgressImportOwner::removeImportedProgressAsync(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    ProgressRemovalCallback callback)
{
    const QString providerKey = trackerProviderKey(providerId);
    if (!m_progress || providerKey.isEmpty() || remoteAccountId.trimmed().isEmpty()) {
        if (callback)
            callback(false, 0, QStringLiteral("Imported tracker Progress is unavailable."));
        return;
    }
    m_progress->removeTrackerImportedProgressAsync(
        providerKey, remoteAccountId, std::move(callback));
}

std::optional<TrackerCanonicalTitleCandidate>
TrackerProgressImportOwner::exactCandidate(const TrackerRemoteMediaKey &) const
{
    // No built-in provider currently has a verified production identity
    // resolver. User selection below is the only mapping path this index adds.
    return std::nullopt;
}

bool TrackerProgressImportOwner::candidateSearchAvailable() const
{
    return m_progress && m_progress->healthy();
}

QList<TrackerCanonicalTitleCandidate>
TrackerProgressImportOwner::userCandidates() const
{
    QList<TrackerCanonicalTitleCandidate> candidates;
    if (!m_progress || !m_progress->healthy())
        return candidates;

    for (const QVariant &entryValue : m_progress->deliveryEntries()) {
        const QVariantMap entry = entryValue.toMap();
        const QString kind = entry.value(QStringLiteral("kind")).toString().trimmed();
        const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
        if (safeProgressKind(kind).isEmpty() || id.isEmpty() || kind.size() > 64
            || kind.contains(QChar::Null) || id.contains(QChar::Null)
            || kind.contains(QLatin1String("..")) || id.contains(QLatin1String(".."))) {
            continue;
        }

        QString displayName = entry.value(QStringLiteral("title")).toString();
        if (displayName.trimmed().isEmpty())
            displayName = entry.value(QStringLiteral("caption")).toString();
        const QString safeName = safePresentationText(displayName, 512);
        if (safeName.isEmpty())
            continue;

        QString detail = entry.value(QStringLiteral("sub")).toString();
        const QString caption = entry.value(QStringLiteral("caption")).toString();
        if (detail.trimmed().isEmpty()
            || detail.trimmed().compare(safeName, Qt::CaseInsensitive) == 0) {
            detail = caption;
        }
        QString safeDetail = safePresentationText(detail, 160);
        if (safeDetail.isEmpty()) {
            bool progressIsNumeric = false;
            const double progress = entry.value(QStringLiteral("progress"))
                                        .toDouble(&progressIsNumeric);
            if (progressIsNumeric && std::isfinite(progress)
                && progress >= 0.0 && progress <= 1.0) {
                safeDetail = QStringLiteral("%1% complete")
                                 .arg(qRound(progress * 100.0));
            }
        }
        candidates.append({kind + QLatin1Char(':') + id, kind, id, safeName,
                           safeDetail.compare(safeName, Qt::CaseInsensitive) == 0
                               ? QString() : safeDetail});
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const TrackerCanonicalTitleCandidate &left,
           const TrackerCanonicalTitleCandidate &right) {
            const int byName = QString::compare(left.displayName, right.displayName,
                                                Qt::CaseInsensitive);
            if (byName != 0)
                return byName < 0;
            const int byContext = QString::compare(left.displayContext,
                                                   right.displayContext,
                                                   Qt::CaseInsensitive);
            return byContext == 0
                ? QString::compare(left.historyKind, right.historyKind,
                                   Qt::CaseInsensitive) < 0
                : byContext < 0;
        });
    // Only expose rows users can distinguish from the text on screen. The
    // canonical ID is deliberately excluded from this key: it is private and
    // cannot help someone choose between otherwise identical progress rows.
    const auto presentationKey = [](const TrackerCanonicalTitleCandidate &candidate) {
        return candidate.displayName.simplified().toCaseFolded() + QLatin1Char('\0')
            + candidate.historyKind.toCaseFolded() + QLatin1Char('\0')
            + candidate.displayContext.simplified().toCaseFolded();
    };
    QHash<QString, int> presentationCounts;
    for (const TrackerCanonicalTitleCandidate &candidate : candidates)
        ++presentationCounts[presentationKey(candidate)];

    QList<TrackerCanonicalTitleCandidate> distinguishable;
    distinguishable.reserve(candidates.size());
    for (const TrackerCanonicalTitleCandidate &candidate : candidates) {
        if (presentationCounts.value(presentationKey(candidate)) == 1)
            distinguishable.append(candidate);
    }
    return distinguishable;
}

bool TrackerProgressImportOwner::validTarget(
    const TrackerTitleMapping &mapping,
    const TrackerImportProgressTarget &target)
{
    static const QSet<QString> supportedKinds{
        QStringLiteral("video"), QStringLiteral("book"), QStringLiteral("manga"),
        QStringLiteral("comic"), QStringLiteral("tankoban"), QStringLiteral("audiobook")};
    return !mapping.canonical.canonicalMediaId.isEmpty()
        && target.canonicalMediaId == mapping.canonical.canonicalMediaId
        && supportedKinds.contains(target.kind) && !target.id.trimmed().isEmpty()
        && target.id == target.id.trimmed() && !target.id.contains(QLatin1Char('\0'))
        && std::isfinite(target.fraction) && target.fraction >= 0.0
        && target.fraction <= 1.0;
}

TrackerImportedProgressValue TrackerProgressImportOwner::importedValue(
    const TrackerTitleMapping &mapping,
    const TrackerImportProgressTarget &target,
    int providerProgress,
    bool providerCompleted,
    bool nativeWitnessedHistory,
    const QVariantMap &entry)
{
    TrackerImportedProgressValue value{mapping.canonical.canonicalMediaId,
                                       mapping.canonical.historyKind,
                                       mapping.canonical.historyId,
                                       providerProgress, providerCompleted,
                                       entry.value(QStringLiteral("updatedAt")).toLongLong(),
                                       nativeWitnessedHistory};
    value.exactProgressTarget = target;
    value.exactProgressTarget->fraction = entry.value(QStringLiteral("progress")).toDouble();
    value.exactProgressTarget->completed = entry.value(QStringLiteral("watched")).toBool();
    value.ownerRevisionToken = ProgressStore::trackerImportRevisionToken(entry);
    return value;
}
