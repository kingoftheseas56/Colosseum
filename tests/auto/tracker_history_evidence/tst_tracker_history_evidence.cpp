#include "trackers/TrackerHistoryEvidenceStore.h"
#include "trackers/TrackerMappingStore.h"

#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

constexpr auto kProfileId = "11111111-1111-4111-8111-111111111111";

class FakeIndex final : public TrackerCanonicalTitleIndex
{
public:
    std::optional<TrackerCanonicalTitleCandidate> exactCandidate(
        const TrackerRemoteMediaKey &remote) const override
    {
        return exact.value(remote.remoteMediaId);
    }

    QList<TrackerCanonicalTitleCandidate> userCandidates() const override { return candidates; }

    QHash<QString, TrackerCanonicalTitleCandidate> exact;
    QList<TrackerCanonicalTitleCandidate> candidates;
};

TrackerCanonicalTitleCandidate frieren()
{
    return {QStringLiteral("ct1:49f10000-0000-4000-8000-000000000001"),
            QStringLiteral("series"), QStringLiteral("theatre:frieren"),
            QStringLiteral("Frieren: Beyond Journey's End")};
}

TrackerCanonicalTitleCandidate onePiece()
{
    return {QStringLiteral("ct1:49f10000-0000-4000-8000-000000000002"),
            QStringLiteral("series"), QStringLiteral("theatre:one-piece"),
            QStringLiteral("One Piece")};
}

TrackerRemoteMediaKey remote(TrackerProviderId provider, const QString &account,
                              const QString &mediaId)
{
    return {provider, account, mediaId};
}

TrackerImportedHistoryEvidence evidence(const TrackerTitleMapping &mapping,
                                        const QString &eventId,
                                        qint64 occurredAtMs,
                                        const QString &payloadFingerprint = {})
{
    TrackerImportedHistoryEvidence result{mapping, TrackerImportedEventKind::Completion, eventId,
                                           QStringLiteral("pull-snapshot-1"), occurredAtMs};
    result.timestampSource = TrackerEvidenceTimestampSource::ProviderEvent;
    result.timestampPrecision = TrackerEvidenceTimestampPrecision::ExactMillisecond;
    result.eventPayloadFingerprint = payloadFingerprint.isEmpty()
        ? QStringLiteral("completion:") + (eventId.isEmpty() ? QStringLiteral("snapshot") : eventId)
        : payloadFingerprint;
    return result;
}

} // namespace

class TrackerHistoryEvidenceTest : public QObject
{
    Q_OBJECT

private slots:
    void exactAndUserConfirmedMappingsAreDurable();
    void ambiguousOrGuessedTitlesCannotCreateAMapping();
    void onlyTrustedTimestampedEventsEnterImportedHistory();
    void onlyCurrentConfirmedMappingsCanEnterImportedHistory();
    void providerEventIdsRemainIdempotentAcrossSnapshotsAndUnchangedMappingRevisions();
    void duplicatesRewatchesAndMultipleProvidersPreserveAttribution();
    void sourceOnlyRemovalCannotTouchOtherProviderEvidence();
};

void TrackerHistoryEvidenceTest::exactAndUserConfirmedMappingsAreDurable()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore store(*profile);
    FakeIndex index;
    index.exact.insert(QStringLiteral("simkl:123"), frieren());
    index.candidates = {frieren(), onePiece()};
    TrackerExactMappingService service(&store, &index);

    const TrackerRemoteMediaKey exactRemote = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    QVERIFY(service.mapExact(exactRemote));
    QCOMPARE(store.mapping(exactRemote)->provenance,
             TrackerMappingProvenance::ExactProviderIdentity);

    const TrackerRemoteMediaKey chosenRemote = remote(TrackerProviderId::Simkl, "42", "simkl:456");
    QVERIFY(service.confirmCandidate(chosenRemote, onePiece().canonicalMediaId));
    QCOMPARE(store.mapping(chosenRemote)->provenance, TrackerMappingProvenance::UserConfirmed);

    TrackerMappingStore reopened(*profile);
    QCOMPARE(reopened.mappings().size(), 2);
    QCOMPARE(reopened.mapping(exactRemote)->canonical.canonicalMediaId, frieren().canonicalMediaId);
}

void TrackerHistoryEvidenceTest::ambiguousOrGuessedTitlesCannotCreateAMapping()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore store(*profile);
    FakeIndex index;
    index.candidates = {frieren(), onePiece()};
    TrackerExactMappingService service(&store, &index);
    const TrackerRemoteMediaKey item = remote(TrackerProviderId::Simkl, "42", "unknown");

    QVERIFY(!service.mapExact(item));
    QVERIFY(!service.confirmCandidate(item, QStringLiteral("title-guess-not-a-candidate")));
    QVERIFY(!store.mapping(item).has_value());
    QCOMPARE(service.findMatchCandidates().size(), 2);
}

void TrackerHistoryEvidenceTest::onlyTrustedTimestampedEventsEnterImportedHistory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    const TrackerRemoteMediaKey item = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    QVERIFY(mappings.upsert(item, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    TrackerHistoryEvidenceStore history(*profile, &mappings);
    const TrackerTitleMapping mapping = *mappings.mapping(item);

    TrackerImportedHistoryEvidence noEventOrSnapshot = evidence(mapping, {}, 1000);
    noEventOrSnapshot.importSnapshotId.clear();
    QVERIFY(!history.record(noEventOrSnapshot));
    TrackerImportedHistoryEvidence unclassified = evidence(mapping, QStringLiteral("unclassified"), 1000);
    unclassified.timestampSource = TrackerEvidenceTimestampSource::Unknown;
    unclassified.timestampPrecision = TrackerEvidenceTimestampPrecision::Unknown;
    QVERIFY(!history.record(unclassified));
    TrackerImportedHistoryEvidence listUpdatedAtOnly = evidence(mapping, QStringLiteral("event"), 1000);
    listUpdatedAtOnly.timestampSource = TrackerEvidenceTimestampSource::ListUpdatedAt;
    QVERIFY(!history.record(listUpdatedAtOnly));
    TrackerImportedHistoryEvidence dateOnly = evidence(mapping, QStringLiteral("date-only-event"), 1000);
    dateOnly.timestampPrecision = TrackerEvidenceTimestampPrecision::DateOnly;
    QVERIFY(!history.record(dateOnly));
    QVERIFY(!QFileInfo::exists(profile->activityDbPath()));

    QVERIFY(history.record(evidence(mapping, QStringLiteral("watch-event-1"), 1000)));
    QCOMPARE(history.contributions().size(), 1);
    QCOMPARE(history.rows().size(), 1);
    TrackerHistoryEvidenceStore reopened(*profile, &mappings);
    QCOMPARE(reopened.rows().first().occurredAtMs, 1000LL);
    QVERIFY(!QFileInfo::exists(profile->activityDbPath()));
}

void TrackerHistoryEvidenceTest::onlyCurrentConfirmedMappingsCanEnterImportedHistory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    const TrackerRemoteMediaKey item = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    QVERIFY(mappings.upsert(item, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    TrackerHistoryEvidenceStore history(*profile, &mappings);
    const TrackerTitleMapping current = *mappings.mapping(item);

    TrackerImportedHistoryEvidence forged = evidence(current, QStringLiteral("forged"), 1000);
    forged.mapping.canonical = onePiece();
    QVERIFY(!history.record(forged));

    QVERIFY(mappings.upsert(item, onePiece(), TrackerMappingProvenance::UserConfirmed));
    QVERIFY(!history.record(evidence(current, QStringLiteral("stale"), 1100)));
    QVERIFY(history.record(evidence(*mappings.mapping(item), QStringLiteral("current"), 1200)));
}

void TrackerHistoryEvidenceTest::providerEventIdsRemainIdempotentAcrossSnapshotsAndUnchangedMappingRevisions()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    const TrackerRemoteMediaKey item = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    QVERIFY(mappings.upsert(item, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    TrackerHistoryEvidenceStore history(*profile, &mappings);

    TrackerImportedHistoryEvidence first = evidence(*mappings.mapping(item), QStringLiteral("event-1"), 1000,
                                                     QStringLiteral("completed:s1e1"));
    first.importSnapshotId = QStringLiteral("snapshot-1");
    QVERIFY(history.record(first));
    TrackerImportedHistoryEvidence secondSnapshot = first;
    secondSnapshot.importSnapshotId = QStringLiteral("snapshot-2");
    QVERIFY(history.record(secondSnapshot));

    QVERIFY(mappings.upsert(item, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    TrackerImportedHistoryEvidence unchangedRevision = secondSnapshot;
    unchangedRevision.mapping = *mappings.mapping(item);
    unchangedRevision.importSnapshotId = QStringLiteral("snapshot-3");
    QVERIFY(history.record(unchangedRevision));
    QCOMPARE(history.contributions().size(), 1);

    QVERIFY(mappings.upsert(item, onePiece(), TrackerMappingProvenance::UserConfirmed));
    TrackerImportedHistoryEvidence changedCanonical = unchangedRevision;
    changedCanonical.mapping = *mappings.mapping(item);
    changedCanonical.importSnapshotId = QStringLiteral("snapshot-4");
    QVERIFY(!history.record(changedCanonical));
}

void TrackerHistoryEvidenceTest::duplicatesRewatchesAndMultipleProvidersPreserveAttribution()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    const TrackerRemoteMediaKey simkl = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    const TrackerRemoteMediaKey trakt = remote(TrackerProviderId::Trakt, "77", "trakt:456");
    QVERIFY(mappings.upsert(simkl, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    QVERIFY(mappings.upsert(trakt, frieren(), TrackerMappingProvenance::UserConfirmed));
    TrackerHistoryEvidenceStore history(*profile, &mappings);
    QVERIFY(history.record(evidence(*mappings.mapping(simkl), QStringLiteral("watch-1"), 1000,
                             QStringLiteral("completed:s1e1"))));
    QVERIFY(history.record(evidence(*mappings.mapping(simkl), QStringLiteral("watch-1"), 1000,
                             QStringLiteral("completed:s1e1"))));
    QVERIFY(history.record(evidence(*mappings.mapping(simkl), QStringLiteral("rewatch-2"), 2000,
                             QStringLiteral("completed:s1e1"))));
    QVERIFY(history.record(evidence(*mappings.mapping(trakt), QStringLiteral("trakt-watch"), 1000,
                             QStringLiteral("completed:s1e1"))));
    QVERIFY(history.record(evidence(*mappings.mapping(trakt), QStringLiteral("trakt-other"), 1000,
                             QStringLiteral("completed:s1e2"))));

    QCOMPARE(history.contributions().size(), 4);
    const QList<TrackerHistoryEvidenceRow> rows = history.rows();
    QCOMPARE(rows.size(), 3);
    const auto grouped = std::find_if(rows.cbegin(), rows.cend(), [](const TrackerHistoryEvidenceRow &row) {
        return row.occurredAtMs == 1000 && row.providers.contains(TrackerProviderId::Simkl)
            && row.providers.contains(TrackerProviderId::Trakt);
    });
    QVERIFY(grouped != rows.cend());
    QCOMPARE(grouped->providers.size(), 2);
}

void TrackerHistoryEvidenceTest::sourceOnlyRemovalCannotTouchOtherProviderEvidence()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto profile = ProfilePaths::account(QString::fromLatin1(kProfileId), root.path());
    QVERIFY(profile.has_value());
    TrackerMappingStore mappings(*profile);
    const TrackerRemoteMediaKey simkl = remote(TrackerProviderId::Simkl, "42", "simkl:123");
    const TrackerRemoteMediaKey trakt = remote(TrackerProviderId::Trakt, "77", "trakt:456");
    QVERIFY(mappings.upsert(simkl, frieren(), TrackerMappingProvenance::ExactProviderIdentity));
    QVERIFY(mappings.upsert(trakt, frieren(), TrackerMappingProvenance::UserConfirmed));
    TrackerHistoryEvidenceStore history(*profile, &mappings);
    QVERIFY(history.record(evidence(*mappings.mapping(simkl), QStringLiteral("simkl-event"), 1000)));
    QVERIFY(history.record(evidence(*mappings.mapping(trakt), QStringLiteral("trakt-event"), 1000)));

    QCOMPARE(history.removeSource(TrackerProviderId::Simkl, QStringLiteral("42")), 1);
    QCOMPARE(history.contributions().size(), 1);
    QCOMPARE(history.contributions().first().mapping.remote.providerId, TrackerProviderId::Trakt);
    QCOMPARE(history.removeSource(TrackerProviderId::Simkl, QStringLiteral("42")), 0);
    QVERIFY(history.sourceRemovalSuppressed(TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(!history.record(evidence(*mappings.mapping(simkl), QStringLiteral("simkl-event"), 1000)));
    TrackerHistoryEvidenceStore reopened(*profile, &mappings);
    QVERIFY(reopened.sourceRemovalSuppressed(TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(reopened.allowSourceAgain(TrackerProviderId::Simkl, QStringLiteral("42")));
    QVERIFY(reopened.record(evidence(*mappings.mapping(simkl), QStringLiteral("simkl-event"), 1000)));
    QCOMPARE(reopened.contributions().size(), 2);
    QVERIFY(!QFileInfo::exists(profile->activityDbPath()));
}

QTEST_APPLESS_MAIN(TrackerHistoryEvidenceTest)

#include "tst_tracker_history_evidence.moc"
