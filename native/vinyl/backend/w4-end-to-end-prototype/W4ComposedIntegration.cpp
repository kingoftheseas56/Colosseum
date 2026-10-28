#include "W4ComposedIntegration.h"
#include "W4TestFixtures.h"

#include "AnnotationMeaningPlane.h"
#include "DeterministicRanker.h"
#include "FakeVinylCatalogueClient.h"
#include "FeedbackTastePersistence.h"
#include "RecommendationDerivation.h"
#include "TimedTextCore.h"
#include "TimedTextTranslation.h"
#include "VinylListenEventPersistence.h"
#include "VinylPersonalStatePersistence.h"

#include <QByteArray>
#include <QVector>

#include <optional>
#include <type_traits>
#include <utility>

namespace w4h {
namespace {

template <typename T>
T mustLibrary(vinyl::LibraryResult<T> result, const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustPlaylist(vinyl::PlaylistResult<T> result, const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustPersonal(vinyl::PersonalStatePersistenceResult<T> result,
               const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustListenPersistence(vinyl::ListenPersistenceResult<T> result,
                        const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustTimed(vinyl::TimedTextResult<T> result, const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustTranslation(vinyl::TranslationResult<T> result,
                  const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustAnnotation(vinyl::AnnotationResult<T> result, const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustRecommendation(vinyl::RecommendationCandidateResult<T> result,
                     const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustDerive(vinyl::recommendation::DerivationResult<T> result,
             const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

template <typename T>
T mustTastePersistence(vinyl::TastePersistenceResult<T> result,
                       const char *message)
{
    require(result.ok(), message);
    return std::move(*result.value);
}

vinyl::ProviderId provider(const char *value)
{
    return mustDomain(
        vinyl::ProviderId::parse(QString::fromLatin1(value)),
        "provider ID should parse");
}

vinyl::ProviderItemId providerItem(const char *value)
{
    return mustDomain(
        vinyl::ProviderItemId::parse(QString::fromLatin1(value)),
        "provider item ID should parse");
}

vinyl::TimedTextCueId cueId(const char *value)
{
    return mustTimed(
        vinyl::TimedTextCueId::create(QString::fromLatin1(value)),
        "timed-text cue ID should construct");
}

vinyl::TranslationLanguage language(const char *value)
{
    return mustTranslation(
        vinyl::TranslationLanguage::create(QString::fromLatin1(value)),
        "translation language should construct");
}

vinyl::RecommendationGeneratorId generator(const char *value)
{
    return mustRecommendation(
        vinyl::RecommendationGeneratorId::parse(QString::fromLatin1(value)),
        "recommendation generator should parse");
}

int countSignal(
    const QVector<vinyl::BehavioralSignalObservation> &observations,
    const vinyl::CanonicalEntityRef &entity,
    vinyl::BehavioralSignalKind kind)
{
    int count = 0;
    for (const auto &observation : observations) {
        if (observation.canonical() == entity
            && observation.kind() == kind) {
            ++count;
        }
    }
    return count;
}

bool hasEvidence(
    const vinyl::recommendation::RankedRecommendation &ranked,
    vinyl::recommendation::RankEvidenceKind kind)
{
    for (const auto &evidence : ranked.explanation()) {
        if (evidence.kind == kind)
            return true;
    }
    return false;
}

const vinyl::recommendation::RankedRecommendation *findRanked(
    const vinyl::recommendation::RankerOutput &output,
    const vinyl::CanonicalEntityRef &entity)
{
    for (const auto &ranked : output.ranked()) {
        if (ranked.entity() == entity)
            return &ranked;
    }
    return nullptr;
}

bool sameRankOutput(const vinyl::recommendation::RankerOutput &left,
                    const vinyl::recommendation::RankerOutput &right)
{
    if (left.configVersion() != right.configVersion()
        || left.suppressed() != right.suppressed()
        || left.ranked().size() != right.ranked().size()) {
        return false;
    }

    for (int i = 0; i < left.ranked().size(); ++i) {
        const auto &a = left.ranked().at(i);
        const auto &b = right.ranked().at(i);
        if (a.entity() != b.entity()
            || a.directlyPlayable() != b.directlyPlayable()
            || a.score() != b.score()
            || a.explanation() != b.explanation()) {
            return false;
        }
    }
    return true;
}

vinyl::RecommendationCandidate recommendationCandidate(
    const vinyl::CanonicalEntityRef &entity,
    const char *generatorName,
    std::optional<double> rawScore = std::nullopt)
{
    using namespace vinyl;

    const auto reason = mustRecommendation(
        RecommendationReasonEvidence::create(
            RecommendationEvidenceKind::HistorySignal,
            QStringLiteral("W4-H factual BA4/derivation evidence")),
        "recommendation reason should construct");

    const auto provenance = mustRecommendation(
        RecommendationCandidateProvenance::create(
            generator(generatorName),
            RecommendationSourceFamily::History),
        "recommendation provenance should construct");

    const auto contribution = mustRecommendation(
        RecommendationContribution::create(
            RecommendationCanonicalContext{},
            provenance,
            rawScore,
            QVector<RecommendationReasonEvidence>{reason}),
        "recommendation contribution should construct");

    return mustRecommendation(
        RecommendationCandidate::create(entity, contribution),
        "recommendation candidate should construct");
}

vinyl::ListenContext listenContext(
    const vinyl::ProfileScopeId &profileId,
    const vinyl::QueueOccurrence &occurrenceValue,
    const vinyl::PlaybackWorkToken &token,
    const char *suffix,
    std::optional<vinyl::catalogue::LastKnownDisplaySnapshot> display =
        std::nullopt)
{
    using namespace vinyl;
    return mustListen(
        ListenContext::create(
            profileId,
            listenSession(suffix),
            occurrenceValue,
            token,
            playInstance(suffix),
            std::nullopt,
            std::move(display)),
        "listen context should construct");
}

void verifyCatalogueRedirectAndUnavailable()
{
    using namespace vinyl;
    using namespace vinyl::catalogue;

    static_assert(!std::is_same<ProviderId, RecordingMbid>::value,
                  "provider identity must not be canonical recording identity");
    static_assert(!std::is_convertible<ProviderId, RecordingMbid>::value,
                  "provider identity must not repair MusicBrainz identity");

    const ProfileScopeId profileId = profile("h6a-profile");
    const RecordingMbid oldRecording =
        recording("69999999-9999-4999-8999-999999999999");
    const RecordingMbid canonicalRecording =
        recording("62222222-2222-4222-8222-222222222222");
    const RecordingMbid missingRecording =
        recording("6fffffff-ffff-4fff-8fff-ffffffffffff");
    const LastKnownDisplaySnapshot staleDisplay{
        QStringLiteral("Hikari (stale local snapshot)"),
        QStringList{QStringLiteral("Legacy display credit")}};
    const PersonalDisplaySnapshot personalDisplay{
        staleDisplay.primaryText,
        QStringLiteral("Legacy display credit")};

    const QueueOccurrence queueOccurrence =
        occurrence("h6a-queue-old", oldRecording, EnqueueOriginKind::User);
    VinylQueue queue;
    require(queue.append(queueOccurrence).ok
                && queue.activate(queueOccurrence.occurrenceId()).ok,
            "H6-A queue should persist the old MusicBrainz recording ID");

    PlaybackSession queuePlayback;
    mustPlayback(
        queuePlayback.beginOccurrence(queueOccurrence, true),
        "H6-A queue playback token should start");

    QVector<QueueDisplaySnapshot> queueDisplays;
    queueDisplays.push_back(
        QueueDisplaySnapshot{queueOccurrence.occurrenceId(), staleDisplay});
    const QByteArray sessionBytes = mustPersist(
        VinylSessionPersistence::capture(
            profileId,
            queue,
            queuePlayback,
            SessionCoordinatorIntent{},
            queueDisplays),
        "H6-A queue/session snapshot should capture");

    VinylLibrary library(profileId);
    require(
        library.save(mustLibrary(
            LibraryEntry::create(
                CanonicalEntityRef::from(oldRecording),
                1000,
                personalDisplay),
            "H6-A library entry should construct")).ok,
        "H6-A library should save the old MusicBrainz ID");

    Playlist playlist = mustPlaylist(
        Playlist::create(
            profileId,
            mustPlaylist(
                PlaylistId::parse(QStringLiteral("h6a-playlist")),
                "H6-A playlist ID should parse"),
            QStringLiteral("Redirect fixture"),
            1000,
            PlaylistProvenanceKind::Manual),
        "H6-A playlist should construct");
    require(
        playlist.append(mustPlaylist(
            PlaylistItem::create(
                mustPlaylist(
                    PlaylistItemId::parse(QStringLiteral("h6a-item")),
                    "H6-A playlist item ID should parse"),
                oldRecording,
                1100,
                std::nullopt,
                personalDisplay),
            "H6-A playlist item should construct")).ok,
        "H6-A playlist should retain the old MusicBrainz ID");

    const QByteArray personalBytes = mustPersonal(
        VinylPersonalStatePersistence::capture(
            profileId,
            library,
            QVector<Playlist>{playlist}),
        "H6-A personal state should capture");

    ListenEventLedger historyLedger(profileId);
    ListenEventRecorder historyRecorder(historyLedger);
    const QueueOccurrence historyOccurrence =
        occurrence("h6a-history-old", oldRecording, EnqueueOriginKind::User);
    PlaybackSession historyPlayback;
    const PlaybackWorkToken historyToken = mustPlayback(
        historyPlayback.beginOccurrence(historyOccurrence),
        "H6-A history playback should begin");
    const ListenContext historyContext = listenContext(
        profileId,
        historyOccurrence,
        historyToken,
        "h6a-history-play",
        staleDisplay);
    require(
        historyRecorder.start(historyContext, 2000, 1000, 0, 1000).ok,
        "H6-A factual history should start");
    require(
        historyRecorder.complete(
            historyToken, 5000, 4000, 3000, 1000).ok,
        "H6-A factual history should complete");
    const QByteArray historyBytes = mustListenPersistence(
        VinylListenEventPersistence::capture(historyLedger),
        "H6-A history should capture");

    FakeCatalogueClient catalogue;
    catalogue.setAvailable(false);

    const RestoredSessionState restoredSession = mustPersist(
        VinylSessionPersistence::restore(profileId, sessionBytes),
        "H6-A session must restore while catalogue is unavailable");
    const RestoredVinylPersonalState restoredPersonal = mustPersonal(
        VinylPersonalStatePersistence::restore(profileId, personalBytes),
        "H6-A library/playlist must restore while catalogue is unavailable");
    const RestoredListenLedger restoredHistory = mustListenPersistence(
        VinylListenEventPersistence::restore(profileId, historyBytes),
        "H6-A history must restore while catalogue is unavailable");

    const auto unavailable = catalogue.recordingById(oldRecording);
    require(
        unavailable.failed()
            && unavailable.error.code == CatalogueErrorCode::Unavailable,
        "H6-A unavailable catalogue must remain typed Unavailable");
    const auto unavailableBatch = catalogue.lookupCanonicalBatch(
        QVector<CanonicalEntityRef>{
            CanonicalEntityRef::from(oldRecording)});
    require(
        !unavailableBatch.ok()
            && unavailableBatch.error.code == CatalogueErrorCode::Unavailable,
        "H6-A batch lookup must not fabricate canonical metadata while unavailable");

    require(
        restoredSession.queue.activeOccurrence() != nullptr
            && restoredSession.queue.activeOccurrence()->recordingMbid()
                == oldRecording,
        "H6-A queue restore must retain durable structural identity");
    require(
        restoredSession.displaySnapshots.size() == 1
            && restoredSession.displaySnapshots.constFirst().display.primaryText
                == staleDisplay.primaryText,
        "H6-A queue snapshot may remain readable fallback while catalogue is unavailable");
    require(
        restoredPersonal.library.size() == 1
            && restoredPersonal.library.items().constFirst().canonical()
                == CanonicalEntityRef::from(oldRecording)
            && restoredPersonal.library.items().constFirst()
                   .displaySnapshot().has_value()
            && restoredPersonal.library.items().constFirst()
                   .displaySnapshot()->title == personalDisplay.title
            && restoredPersonal.library.items().constFirst()
                   .displaySnapshot()->artistCredit
                == personalDisplay.artistCredit,
        "H6-A Library row must retain identity plus readable display fallback while catalogue is unavailable");
    require(
        restoredPersonal.playlists.size() == 1
            && restoredPersonal.playlists.constFirst().items().size() == 1
            && restoredPersonal.playlists.constFirst().items().constFirst()
                   .recordingMbid() == oldRecording
            && restoredPersonal.playlists.constFirst().items().constFirst()
                   .displaySnapshot().has_value()
            && restoredPersonal.playlists.constFirst().items().constFirst()
                   .displaySnapshot()->title == personalDisplay.title
            && restoredPersonal.playlists.constFirst().items().constFirst()
                   .displaySnapshot()->artistCredit
                == personalDisplay.artistCredit,
        "H6-A Playlist row must retain identity plus readable display fallback while catalogue is unavailable");
    require(
        !restoredHistory.events.isEmpty()
            && restoredHistory.events.constFirst().occurrence.recordingMbid()
                == oldRecording
            && restoredHistory.events.constFirst().displaySnapshot.has_value()
            && restoredHistory.events.constFirst().displaySnapshot->primaryText
                == staleDisplay.primaryText,
        "H6-A history must restore old identity plus explicitly non-authoritative display fallback");

    catalogue.setAvailable(true);

    const auto redirected = catalogue.recordingById(oldRecording);
    require(
        redirected.redirected()
            && redirected.redirectedTo.has_value()
            && *redirected.redirectedTo == canonicalRecording
            && redirected.error.code == CatalogueErrorCode::None,
        "H6-A old MBID must resolve as Redirected to canonical MusicBrainz identity");

    const auto missing = catalogue.recordingById(missingRecording);
    require(
        missing.failed()
            && missing.error.code == CatalogueErrorCode::NotFound,
        "H6-A Redirected must remain distinguishable from NotFound");

    const auto canonicalLookup = catalogue.recordingById(canonicalRecording);
    require(
        canonicalLookup.ok()
            && canonicalLookup.value->mbid == canonicalRecording
            && canonicalLookup.value->title == QStringLiteral("光"),
        "H6-A authoritative canonical catalogue data should return after outage");
    require(
        canonicalLookup.value->title != staleDisplay.primaryText
            && canonicalLookup.value->title != personalDisplay.title
            && restoredPersonal.library.items().constFirst()
                   .displaySnapshot()->title == personalDisplay.title
            && restoredPersonal.playlists.constFirst().items().constFirst()
                   .displaySnapshot()->title == personalDisplay.title,
        "H6-A BA5 display snapshots stay non-authoritative and never overwrite canonical catalogue truth");

    const auto batch = catalogue.lookupCanonicalBatch(
        QVector<CanonicalEntityRef>{
            CanonicalEntityRef::from(oldRecording),
            CanonicalEntityRef::from(missingRecording)});
    require(batch.ok() && batch.value->size() == 2,
            "H6-A canonical batch should return both redirect and missing states");
    require(
        batch.value->at(0).state
                == CanonicalBatchLookupState::Redirected
            && batch.value->at(0).redirectedTo.has_value()
            && *batch.value->at(0).redirectedTo
                == CanonicalEntityRef::from(canonicalRecording),
        "H6-A batch read-repair seam must expose Redirected without rewriting storage");
    require(
        batch.value->at(1).state
            == CanonicalBatchLookupState::NotFound,
        "H6-A batch must retain NotFound separately from Redirected");

    auto resolveStoredRecording =
        [&catalogue](const RecordingMbid &stored) -> RecordingMbid {
        const auto lookup = catalogue.recordingById(stored);
        if (lookup.redirected()) {
            require(lookup.redirectedTo.has_value(),
                    "redirected lookup should expose canonical target");
            return *lookup.redirectedTo;
        }
        require(lookup.ok(), "stored recording should resolve canonically");
        return lookup.value->mbid;
    };

    const RecordingMbid queueStored =
        restoredSession.queue.activeOccurrence()->recordingMbid();
    const RecordingMbid libraryStored = mustDomain(
        RecordingMbid::parse(
            restoredPersonal.library.items().constFirst().canonical()
                .mbidString()),
        "H6-A restored Library canonical reference should remain a RecordingMbid");
    const RecordingMbid playlistStored =
        restoredPersonal.playlists.constFirst().items().constFirst()
            .recordingMbid();
    const RecordingMbid historyStored =
        restoredHistory.events.constFirst().occurrence.recordingMbid();

    require(
        resolveStoredRecording(queueStored) == canonicalRecording
            && resolveStoredRecording(libraryStored) == canonicalRecording
            && resolveStoredRecording(playlistStored) == canonicalRecording
            && resolveStoredRecording(historyStored) == canonicalRecording,
        "H6-A queue, Library, Playlist, and history must share the same MusicBrainz read-repair seam");

    require(
        queueStored == oldRecording
            && libraryStored == oldRecording
            && playlistStored == oldRecording
            && historyStored == oldRecording,
        "H6-A read repair must not silently rewrite durable structural IDs in place");
}

void verifyTimedTextDocumentSwap()
{
    using namespace vinyl;

    const ProfileScopeId profileId = profile("h6b-profile");
    const RecordingMbid song =
        recording("88888888-8888-4888-8888-888888888888");
    const QueueOccurrence songOccurrence =
        occurrence("h6b-occurrence", song, EnqueueOriginKind::User);

    const TimedTextCueId sharedCue = cueId("cue-1");
    const TimedTextDocument documentA = mustTimed(
        TimedTextDocument::synchronized(
            song,
            TimedTextProvenance(
                provider("lyrics.a"),
                providerItem("lyrics-document-a")),
            TimedTextKind::LineSynced,
            QVector<TimedTextCue>{
                TimedTextCue(
                    sharedCue,
                    QStringLiteral("document A text"),
                    1000,
                    2000)}),
        "H6-B document A should construct");

    const TranslationLanguage spanish = language("es");
    const TimedTextTranslationDocument translationA = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            documentA,
            spanish,
            TranslationProvenance(
                provider("translation.a"),
                providerItem("translation-document-a")),
            QVector<CueTranslation>{
                mustTranslation(
                    CueTranslation::translated(
                        sharedCue,
                        QStringLiteral("texto traducido A")),
                    "H6-B cue translation should construct")}),
        "H6-B translation bound to document A should construct");

    const AnnotationReferent annotationA = mustAnnotation(
        AnnotationReferent::cueLinked(documentA, sharedCue),
        "H6-B annotation referent bound to document A should construct");

    const TimedTextDocument documentB = mustTimed(
        TimedTextDocument::synchronized(
            song,
            TimedTextProvenance(
                provider("lyrics.b"),
                providerItem("lyrics-document-b")),
            TimedTextKind::LineSynced,
            QVector<TimedTextCue>{
                TimedTextCue(
                    sharedCue,
                    QStringLiteral("document B revised text"),
                    1000,
                    2000)}),
        "H6-B document B should construct");

    require(
        documentA.documentId() != documentB.documentId(),
        "H6-B same Recording/cue ordinal must still produce distinct document revisions");

    const auto translatedSwap =
        projectTranslationCue(documentB, translationA, 0);
    require(
        !translatedSwap.ok()
            && translatedSwap.error.code
                == TranslationErrorCode::SourceDocumentMismatch,
        "H6-B translation from document A must not migrate to document B by cue ID equality");

    const auto annotationSwap = annotationA.resolveCueIndexIn(documentB);
    require(
        !annotationSwap.ok()
            && annotationSwap.error.code
                == AnnotationErrorCode::SourceDocumentMismatch,
        "H6-B annotation from document A must not migrate to document B by cue ID equality");

    const TimedTextRequestKey originalTimedRequest(
        song, songOccurrence.occurrenceId());
    const TranslationRequestKey originalTranslationRequest(
        originalTimedRequest,
        documentA.documentId(),
        spanish,
        1);
    const TranslationRequestKey swappedTranslationRequest(
        originalTimedRequest,
        documentB.documentId(),
        spanish,
        1);
    require(
        !canApplyTranslationResult(
            originalTranslationRequest,
            swappedTranslationRequest,
            translationA),
        "H6-B translation request ownership must reject a different document identity");

    HarnessContext transport(profileId);
    require(
        transport.queue.append(songOccurrence).ok
            && transport.queue.activate(songOccurrence.occurrenceId()).ok,
        "H6-B source-restart queue should construct");
    const auto startStatus = transport.coordinator.start(true);
    require(
        startStatus.ok && hasAwaiting(startStatus),
        "H6-B source-restart should enter AwaitingInput");

    const PlayInstanceId play = playInstance("h6b-play");
    const auto sourceA =
        exactAudio(song, "h6b.source", "document-a-source", 320, false);
    const auto requestA = mustResolution(
        transport.sources.beginForActive(
            QVector<SelectableSourceCandidate>{sourceA},
            true,
            SourceResolutionReason::Initial,
            play),
        "H6-B first source request should begin");
    const auto acceptedA = transport.sources.acceptResolved(
        requestA,
        resolved(requestA, QStringLiteral("https://h6b/a"), 1000),
        1500);
    require(
        acceptedA.action == SourceResolutionAction::PlaybackRestarted,
        "H6-B first transport should bind");
    const PlaybackWorkToken tokenA = activeToken(transport);
    transport.coordinator.engineLoaded(tokenA);

    const auto sourceB =
        exactAudio(song, "h6b.source", "document-b-source", 256, false);
    const auto requestB = mustResolution(
        transport.sources.beginForActive(
            QVector<SelectableSourceCandidate>{sourceB},
            true,
            SourceResolutionReason::ExplicitSwitch,
            play),
        "H6-B replacement source request should begin");
    const auto acceptedB = transport.sources.acceptResolved(
        requestB,
        resolved(requestB, QStringLiteral("https://h6b/b"), 2000),
        2500);
    require(
        acceptedB.action == SourceResolutionAction::PlaybackRestarted,
        "H6-B replacement transport should bind");
    const PlaybackWorkToken tokenB = activeToken(transport);

    require(
        tokenB.occurrenceId() == tokenA.occurrenceId()
            && tokenB.generation() > tokenA.generation(),
        "H6-B replacement source must advance BA2 generation on the same QueueOccurrence");

    const TimedTextRequestKey currentTimedRequest(
        song, songOccurrence.occurrenceId());
    require(
        canApplyTimedTextResult(
            originalTimedRequest,
            currentTimedRequest,
            documentA),
        "H6-B unchanged Recording/QueueOccurrence/document ownership must survive BA2 generation restart");

    const TranslationRequestKey currentTranslationRequest(
        currentTimedRequest,
        documentA.documentId(),
        spanish,
        1);
    require(
        canApplyTranslationResult(
            originalTranslationRequest,
            currentTranslationRequest,
            translationA),
        "H6-B translation ownership must remain occurrence/document based rather than BA2-generation based");

    requireInvariant(
        transport,
        "H6-B source replacement must preserve BA1-active / BA2-bound invariant");
    transport.coordinator.stop();
}

void recordCompletedPlay(
    vinyl::ListenEventRecorder &recorder,
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &recordingMbid,
    const char *occurrenceName,
    vinyl::EnqueueOriginKind originKind,
    const char *listenName,
    qint64 startedAtMs,
    qint64 completedAtMs)
{
    using namespace vinyl;

    const QueueOccurrence queueOccurrence =
        occurrence(occurrenceName, recordingMbid, originKind);
    PlaybackSession playback;
    const PlaybackWorkToken token = mustPlayback(
        playback.beginOccurrence(queueOccurrence),
        "H6-C playback token should begin");
    const ListenContext context =
        listenContext(profileId, queueOccurrence, token, listenName);

    require(
        recorder.start(
            context,
            startedAtMs,
            startedAtMs,
            0,
            1000).ok,
        "H6-C factual completed play should start");
    require(
        recorder.complete(
            token,
            completedAtMs,
            completedAtMs,
            completedAtMs - startedAtMs,
            1000).ok,
        "H6-C factual completed play should complete");
}

void recordFailedPlay(
    vinyl::ListenEventRecorder &recorder,
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &recordingMbid,
    const char *occurrenceName,
    const char *listenName,
    qint64 startedAtMs,
    qint64 failedAtMs)
{
    using namespace vinyl;

    const QueueOccurrence queueOccurrence =
        occurrence(
            occurrenceName,
            recordingMbid,
            EnqueueOriginKind::User);
    PlaybackSession playback;
    const PlaybackWorkToken token = mustPlayback(
        playback.beginOccurrence(queueOccurrence),
        "H6-C failure playback token should begin");
    const ListenContext context =
        listenContext(profileId, queueOccurrence, token, listenName);

    require(
        recorder.start(
            context,
            startedAtMs,
            startedAtMs,
            0,
            1000).ok,
        "H6-C factual failure play should start");
    require(
        recorder.failure(
            token,
            failedAtMs,
            failedAtMs,
            failedAtMs - startedAtMs,
            1000).ok,
        "H6-C factual failure should remain Failure");
}

void verifyRecommendationComposition()
{
    using namespace vinyl;
    using namespace vinyl::catalogue;
    using namespace vinyl::recommendation;

    const ProfileScopeId profileId = profile("h6c-profile");
    const RecordingMbid sharedRecording =
        recording("62222222-2222-4222-8222-222222222222");
    const RecordingMbid failureRecording =
        recording("63333333-3333-4333-8333-333333333333");
    const RecordingMbid alternateRecording =
        recording("61111111-1111-4111-8111-111111111111");
    const RecordingMbid unknownRecording =
        recording("64444444-4444-4444-8444-444444444444");

    ListenEventLedger ledger(profileId);
    ListenEventRecorder recorder(ledger);

    recordCompletedPlay(
        recorder,
        profileId,
        sharedRecording,
        "h6c-user-occurrence",
        EnqueueOriginKind::User,
        "h6c-user-play",
        10000,
        20000);
    recordCompletedPlay(
        recorder,
        profileId,
        sharedRecording,
        "h6c-radio-occurrence",
        EnqueueOriginKind::Radio,
        "h6c-radio-play",
        30000,
        40000);
    recordFailedPlay(
        recorder,
        profileId,
        failureRecording,
        "h6c-failure-occurrence",
        "h6c-failure-play",
        50000,
        55000);

    const QByteArray ledgerBeforeDerivation = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "H6-C factual BA4 ledger should capture before derivation");

    RecommendationDerivationConfig derivationConfig;
    derivationConfig.version = kRecommendationDerivationVersion;
    derivationConfig.nowMs = 100000;
    derivationConfig.recentWindowMs = 100000;
    derivationConfig.earlySkipPositionMs = 30000;

    const RecommendationDerivationOutput derived = mustDerive(
        deriveRecommendationInputs(ledger, derivationConfig),
        "H6-C BA4 -> W4-G derivation should succeed");
    const RecommendationDerivationOutput derivedAgain = mustDerive(
        deriveRecommendationInputs(ledger, derivationConfig),
        "H6-C repeated derivation should succeed");

    require(
        derived.version() == kRecommendationDerivationVersion
            && derivedAgain.version() == derived.version(),
        "H6-C recommendation derivation must be explicitly versioned");
    require(
        derived.observations().size() == derivedAgain.observations().size()
            && derived.aggregates().size() == derivedAgain.aggregates().size(),
        "H6-C same factual ledger/config must derive the same evidence shape");
    const QByteArray ledgerAfterDerivation = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "H6-C factual BA4 ledger should capture after derivation");
    require(
        ledgerBeforeDerivation == ledgerAfterDerivation,
        "H6-C derivation must be pure and must not mutate factual BA4 history");

    const CanonicalEntityRef sharedRef =
        CanonicalEntityRef::from(sharedRecording);
    const CanonicalEntityRef failureRef =
        CanonicalEntityRef::from(failureRecording);
    const CanonicalEntityRef alternateRef =
        CanonicalEntityRef::from(alternateRecording);
    const CanonicalEntityRef unknownRef =
        CanonicalEntityRef::from(unknownRecording);

    const auto *sharedAggregate = derived.findAggregate(sharedRef);
    require(
        sharedAggregate != nullptr
            && sharedAggregate->playCount() == 2
            && sharedAggregate->naturalCompletionCount() == 1
            && sharedAggregate->passiveAutoplayCompletionCount() == 1,
        "H6-C same Recording must preserve deliberate and passive completion semantics separately");

    require(
        countSignal(
            derived.observations(),
            sharedRef,
            BehavioralSignalKind::DeliberateQueue) == 1
            && countSignal(
                derived.observations(),
                sharedRef,
                BehavioralSignalKind::NaturalCompletion) == 1
            && countSignal(
                derived.observations(),
                sharedRef,
                BehavioralSignalKind::PassiveAutoplayCompletion) == 1,
        "H6-C User and Radio origins for the same Recording must derive different behavioral interpretations");

    const auto *failureAggregate = derived.findAggregate(failureRef);
    require(
        failureAggregate != nullptr
            && failureAggregate->playCount() == 1
            && failureAggregate->recentCompletionCount() == 0
            && failureAggregate->recentSkipCount() == 0
            && failureAggregate->naturalCompletionCount() == 0
            && failureAggregate->passiveAutoplayCompletionCount() == 0
            && failureAggregate->earlySkipCount() == 0,
        "H6-C Failure must preserve factual start without fabricating completion/skip taste evidence");

    TasteProfileState taste(profileId);
    for (const auto &observation : derived.observations()) {
        require(
            taste.recordBehavioralSignal(observation).ok,
            "H6-C derived behavioral observation should enter BE2");
    }
    const QByteArray tasteBeforeExposure = mustTastePersistence(
        FeedbackTastePersistence::capture(taste),
        "H6-C BE2 state should capture before exposure");

    const auto exposureEvidence = mustDerive(
        ExposureEvidenceRef::create(
            ExposureEvidenceKind::BehavioralHistory,
            QStringLiteral("w4-g:ba4-derived:v1"),
            sharedRef),
        "H6-C exposure evidence should reference derivation evidence");
    const auto exposureId = mustDerive(
        RecommendationExposureId::parse(
            QStringLiteral("h6c-exposure-1")),
        "H6-C exposure ID should parse");
    const auto exposure = mustDerive(
        RecommendationExposureRecord::create(
            profileId,
            exposureId,
            sharedRef,
            90000,
            1,
            QStringLiteral("w4-h-ranker-v1"),
            QVector<ExposureEvidenceRef>{exposureEvidence}),
        "H6-C exposure record should construct");

    const int eventCountBeforeExposure = ledger.events().size();
    const qint64 listeningMsBeforeExposure = ledger.totalListeningMs();
    RecommendationExposureLog exposureLog(profileId);
    const auto exposureStatus = exposureLog.append(exposure);
    require(
        exposureStatus.ok && exposureStatus.changed,
        "H6-C recommendation exposure should append factually");
    require(
        ledger.events().size() == eventCountBeforeExposure
            && ledger.totalListeningMs() == listeningMsBeforeExposure,
        "H6-C exposure is not listening and must not create BA4 facts");

    const QByteArray tasteAfterExposure = mustTastePersistence(
        FeedbackTastePersistence::capture(taste),
        "H6-C BE2 state should capture after exposure");
    require(
        tasteBeforeExposure == tasteAfterExposure,
        "H6-C showing a recommendation must not become positive or negative taste");
    require(
        taste.find(sharedRef) != nullptr
            && taste.find(sharedRef)->explicitFeedback().feedback()
                == ExplicitTasteFeedback::Neutral,
        "H6-C behavioral/exposure evidence must not fabricate explicit taste");

    const RecommendationCandidateSet candidates =
        RecommendationCandidateSet::normalize(
            QVector<RecommendationCandidate>{
                recommendationCandidate(
                    sharedRef,
                    "calibrated.gen",
                    0.9),
                recommendationCandidate(
                    alternateRef,
                    "uncalibrated.gen",
                    9999.0),
                recommendationCandidate(
                    unknownRef,
                    "plain.gen")});

    FakeCatalogueClient catalogue;
    const auto catalogueShared =
        catalogue.recordingById(sharedRecording);
    const auto catalogueAlternate =
        catalogue.recordingById(alternateRecording);
    require(
        catalogueShared.ok() && catalogueAlternate.ok(),
        "H6-C catalogue fixtures should be authoritative before ranking");

    RankerFeatureContext features;
    features.recordings.push_back(*catalogueShared.value);
    features.recordings.push_back(*catalogueAlternate.value);

    DeterministicRankerConfig rankConfig;
    rankConfig.version = QStringLiteral("w4-h-ranker-v1");
    rankConfig.nowMs = 100000;
    rankConfig.maxResults = 20;
    rankConfig.rawScoreCalibrations = QVector<RawScoreCalibration>{
        RawScoreCalibration{
            generator("calibrated.gen"),
            100.0,
            200}};

    const QByteArray ledgerBeforeRank = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "H6-C BA4 should capture before BE3");
    const QByteArray tasteBeforeRank = mustTastePersistence(
        FeedbackTastePersistence::capture(taste),
        "H6-C BE2 should capture before BE3");

    const auto firstRank = rankDeterministically(
        candidates,
        taste,
        derived,
        exposureLog,
        features,
        rankConfig);
    require(firstRank.ok(),
            "H6-C BE3 should consume BA4-derived/BE2/exposure evidence");
    const auto secondRank = rankDeterministically(
        candidates,
        taste,
        derived,
        exposureLog,
        features,
        rankConfig);
    require(
        secondRank.ok()
            && sameRankOutput(*firstRank.value, *secondRank.value),
        "H6-C same inputs/config/clock must produce exactly the same ordering twice");

    const QByteArray ledgerAfterRank = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "H6-C BA4 should capture after BE3");
    const QByteArray tasteAfterRank = mustTastePersistence(
        FeedbackTastePersistence::capture(taste),
        "H6-C BE2 should capture after BE3");
    require(
        ledgerBeforeRank == ledgerAfterRank,
        "H6-C BE3 must not mutate factual BA4");
    require(
        tasteBeforeRank == tasteAfterRank,
        "H6-C BE3 must not mutate BE2");

    const auto catalogueSharedAfter =
        catalogue.recordingById(sharedRecording);
    require(
        catalogueSharedAfter.ok()
            && catalogueSharedAfter.value->mbid
                == catalogueShared.value->mbid
            && catalogueSharedAfter.value->title
                == catalogueShared.value->title,
        "H6-C BE3 must not mutate catalogue state");

    const auto *rankedShared =
        findRanked(*firstRank.value, sharedRef);
    const auto *rankedAlternate =
        findRanked(*firstRank.value, alternateRef);
    const auto *rankedUnknown =
        findRanked(*firstRank.value, unknownRef);
    require(
        rankedShared != nullptr
            && rankedAlternate != nullptr
            && rankedUnknown != nullptr,
        "H6-C all unsuppressed candidates should remain rankable");

    require(
        hasEvidence(
            *rankedShared,
            RankEvidenceKind::CalibratedGeneratorScore),
        "H6-C calibrated generator raw score may participate when calibration exists");
    require(
        !hasEvidence(
            *rankedAlternate,
            RankEvidenceKind::CalibratedGeneratorScore),
        "H6-C uncalibrated raw provider/generator score must not participate");
    require(
        !hasEvidence(*rankedUnknown, RankEvidenceKind::ExplicitPositive)
            && !hasEvidence(*rankedUnknown, RankEvidenceKind::ExplicitNegative)
            && !hasEvidence(*rankedUnknown, RankEvidenceKind::PlayCount)
            && !hasEvidence(*rankedUnknown, RankEvidenceKind::RecentCompletion)
            && !hasEvidence(*rankedUnknown, RankEvidenceKind::EarlySkip),
        "H6-C missing evidence must remain unknown rather than fabricated");

    for (const auto &ranked : firstRank.value->ranked()) {
        for (const auto &evidence : ranked.explanation()) {
            require(
                !evidence.sourceId.isEmpty(),
                "H6-C explanation evidence must remain traceable to stored evidence or explicit policy");
        }
    }
}

} // namespace

int verifyMainJourneyRecommendationFacts(
    const vinyl::ListenEventLedger &ledger,
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &completedRecording,
    const vinyl::RecordingMbid &failedRecording)
{
    using namespace vinyl;
    using namespace vinyl::recommendation;

    const int before = g_checks;

    const QByteArray ledgerBefore = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "W4-H main BA4 journey should persist before derivation");

    RecommendationDerivationConfig config;
    config.version = kRecommendationDerivationVersion;
    config.nowMs = 100000;
    config.recentWindowMs = 100000;
    config.earlySkipPositionMs = 30000;

    const RecommendationDerivationOutput derived = mustDerive(
        deriveRecommendationInputs(ledger, config),
        "W4-H actual Play journey should feed W4-G derivation");
    require(
        derived.profileId() == profileId
            && derived.version() == kRecommendationDerivationVersion,
        "W4-H actual Play journey derivation must preserve profile/version");

    const CanonicalEntityRef completedRef =
        CanonicalEntityRef::from(completedRecording);
    const CanonicalEntityRef failedRef =
        CanonicalEntityRef::from(failedRecording);
    const auto *completed = derived.findAggregate(completedRef);
    const auto *failed = derived.findAggregate(failedRef);

    require(
        completed != nullptr && completed->playCount() == 1,
        "W4-H continuation source restart must not fabricate Replay/play-count evidence");
    require(
        countSignal(
            derived.observations(),
            completedRef,
            BehavioralSignalKind::Replay) == 0,
        "W4-H continuation transport must not derive Replay");
    require(
        failed != nullptr
            && failed->recentCompletionCount() == 0
            && failed->recentSkipCount() == 0
            && failed->naturalCompletionCount() == 0
            && failed->passiveAutoplayCompletionCount() == 0
            && failed->earlySkipCount() == 0,
        "W4-H actual playback Failure must not fabricate positive or negative taste outcomes");

    const QByteArray ledgerAfter = mustListenPersistence(
        VinylListenEventPersistence::capture(ledger),
        "W4-H main BA4 journey should persist after derivation");
    require(
        ledgerBefore == ledgerAfter,
        "W4-H recommendation derivation must not mutate actual factual BA4 journey");

    return g_checks - before;
}

int runComposedIntegrationProofs()
{
    const int before = g_checks;

    verifyCatalogueRedirectAndUnavailable();
    verifyTimedTextDocumentSwap();
    verifyRecommendationComposition();

    return g_checks - before;
}

} // namespace w4h
