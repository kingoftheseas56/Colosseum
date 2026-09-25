#pragma once

#include "TrackerMappingStore.h"

#include <QString>
#include <QSet>

enum class TrackerImportedEventKind : quint8 {
    Activity,
    Completion
};

// A provider list's `updated_at` tells us when it was fetched or changed, not
// when somebody consumed the title. It is deliberately represented so that it
// can never be mistaken for a witnessed History event.
enum class TrackerEvidenceTimestampSource : quint8 {
    Unknown,
    ProviderEvent,
    ListUpdatedAt
};

enum class TrackerEvidenceTimestampPrecision : quint8 {
    Unknown,
    ExactMillisecond,
    DateOnly
};

struct TrackerImportedHistoryEvidence {
    TrackerTitleMapping mapping;
    TrackerImportedEventKind eventKind = TrackerImportedEventKind::Activity;
    QString providerEventId;
    QString importSnapshotId;
    qint64 occurredAtMs = 0;
    TrackerEvidenceTimestampSource timestampSource = TrackerEvidenceTimestampSource::Unknown;
    TrackerEvidenceTimestampPrecision timestampPrecision = TrackerEvidenceTimestampPrecision::Unknown;
    QString eventPayloadFingerprint;
};

struct TrackerHistoryEvidenceRow {
    QString canonicalMediaId;
    QString historyKind;
    QString historyId;
    TrackerImportedEventKind eventKind = TrackerImportedEventKind::Activity;
    qint64 occurredAtMs = 0;
    QList<TrackerProviderId> providers;
    QList<QString> contributionIds;
};

// Tracker-only imported evidence is a canonical fact owned by the tracker
// plane. It never writes Activity or native witnessed History. Later History
// presentation consumes its stable contribution IDs and attribution.
class TrackerHistoryEvidenceStore final
{
public:
    TrackerHistoryEvidenceStore(const ProfilePaths &profile,
                                const TrackerMappingStore *mappings);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    QList<TrackerImportedHistoryEvidence> contributions() const;
    QList<TrackerHistoryEvidenceRow> rows() const;
    bool adoptPrivateStateFrom(const TrackerHistoryEvidenceStore &source,
                               QString *error = nullptr);
    bool record(const TrackerImportedHistoryEvidence &evidence, QString *error = nullptr);
    int removeSource(TrackerProviderId providerId,
                     const QString &remoteAccountId,
                     QString *error = nullptr);
    bool sourceRemovalSuppressed(TrackerProviderId providerId,
                                 const QString &remoteAccountId) const;
    bool allowSourceAgain(TrackerProviderId providerId,
                          const QString &remoteAccountId,
                          QString *error = nullptr);

private:
    bool load();
    bool persist(const QList<TrackerImportedHistoryEvidence> &candidate,
                 const QSet<QString> &suppressedSources,
                 QString *error) const;

    ProfilePaths m_profile;
    const TrackerMappingStore *m_mappings = nullptr;
    QString m_path;
    QList<TrackerImportedHistoryEvidence> m_contributions;
    QSet<QString> m_suppressedSources;
    bool m_healthy = true;
    QString m_error;
};
