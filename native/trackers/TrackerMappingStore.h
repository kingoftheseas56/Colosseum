#pragma once

#include "TrackerTypes.h"

#include "account/ProfilePaths.h"

#include <QString>

#include <optional>

enum class TrackerMappingProvenance : quint8 {
    ExactProviderIdentity,
    UserConfirmed
};

struct TrackerRemoteMediaKey {
    TrackerProviderId providerId = TrackerProviderId::Simkl;
    QString remoteAccountId;
    QString remoteMediaId;
};

struct TrackerCanonicalTitleCandidate {
    QString canonicalMediaId;
    QString historyKind;
    QString historyId;
    QString displayName;
    // Optional sanitized native Progress context for a local match chooser.
    // This presentation field is not part of mapping identity or persistence.
    QString displayContext;
};

struct TrackerTitleMapping {
    TrackerRemoteMediaKey remote;
    TrackerCanonicalTitleCandidate canonical;
    TrackerMappingProvenance provenance = TrackerMappingProvenance::ExactProviderIdentity;
    quint64 revision = 0;
};

// The index is intentionally read-only. A title search may help the person
// choose a candidate, but only exact provider identity or their explicit
// candidate choice is allowed to create a mapping.
class TrackerCanonicalTitleIndex
{
public:
    virtual ~TrackerCanonicalTitleIndex() = default;
    virtual std::optional<TrackerCanonicalTitleCandidate> exactCandidate(
        const TrackerRemoteMediaKey &remote) const = 0;
    virtual bool candidateSearchAvailable() const { return true; }
    virtual QList<TrackerCanonicalTitleCandidate> userCandidates() const = 0;
};

class TrackerMappingStore final
{
public:
    explicit TrackerMappingStore(const ProfilePaths &profile);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    QList<TrackerTitleMapping> mappings() const;
    std::optional<TrackerTitleMapping> mapping(const TrackerRemoteMediaKey &remote) const;
    bool adoptPrivateStateFrom(const TrackerMappingStore &source,
                               QString *error = nullptr);
    bool upsert(const TrackerRemoteMediaKey &remote,
                const TrackerCanonicalTitleCandidate &candidate,
                TrackerMappingProvenance provenance,
                QString *error = nullptr);
    bool remove(const TrackerRemoteMediaKey &remote, QString *error = nullptr);

#ifdef COLOSSEUM_TRACKER_MAPPING_TESTING
    void failNextRemovalPersistenceForTesting() { m_failNextRemovalPersistence = true; }
#endif

private:
    bool load();
    bool persist(const QList<TrackerTitleMapping> &candidate, QString *error) const;

    ProfilePaths m_profile;
    QString m_path;
    QList<TrackerTitleMapping> m_mappings;
    bool m_healthy = true;
    QString m_error;
#ifdef COLOSSEUM_TRACKER_MAPPING_TESTING
    bool m_failNextRemovalPersistence = false;
#endif
};

class TrackerExactMappingService final
{
public:
    TrackerExactMappingService(TrackerMappingStore *store,
                               const TrackerCanonicalTitleIndex *index);

    bool mapExact(const TrackerRemoteMediaKey &remote, QString *error = nullptr);
    QList<TrackerCanonicalTitleCandidate> findMatchCandidates() const;
    bool confirmCandidate(const TrackerRemoteMediaKey &remote,
                          const QString &canonicalMediaId,
                          QString *error = nullptr);

private:
    TrackerMappingStore *m_store = nullptr;
    const TrackerCanonicalTitleIndex *m_index = nullptr;
};
