#if defined(P08_T5_NEGATE_UPLOAD_ID)
#define UploadRequestId UploadRequestId_removed
#elif defined(P08_T5_NEGATE_UPLOAD_OWNERSHIP)
#define UploadOwnership UploadOwnership_removed
#elif defined(P08_T5_NEGATE_ADVERTISE_ACTION)
#define AdvertisePieceAction AdvertisePieceAction_removed
#elif defined(P08_T5_NEGATE_RESPONSE_ACTION)
#define UploadResponseAction UploadResponseAction_removed
#elif defined(P08_T5_NEGATE_ABORT_ACTION)
#define UploadAbortAction UploadAbortAction_removed
#elif defined(P08_T5_NEGATE_REQUEST_OBSERVATION)
#define UploadRequestObservation UploadRequestObservation_removed
#elif defined(P08_T5_NEGATE_CANCEL_OBSERVATION)
#define UploadCancelObservation UploadCancelObservation_removed
#elif defined(P08_T5_NEGATE_ACTION_VARIANT)
#define TorrentAction TorrentAction_removed
#elif defined(P08_T5_NEGATE_OBSERVATION_VARIANT)
#define TorrentObservation TorrentObservation_removed
#elif defined(P08_T5_NEGATE_PEER_CAP)
#define kMaxOutstandingUploadsPerPeer kMaxOutstandingUploadsPerPeer_removed
#elif defined(P08_T5_NEGATE_GLOBAL_CAP)
#define kMaxOutstandingUploadsGlobal kMaxOutstandingUploadsGlobal_removed
#elif defined(P08_T5_NEGATE_OUTSTANDING_STAT)
#define ownedUploadsOutstanding ownedUploadsOutstanding_removed
#elif defined(P08_T5_NEGATE_ACCEPTED_STAT)
#define uploadRequestsAccepted uploadRequestsAccepted_removed
#elif defined(P08_T5_NEGATE_REJECTED_STAT)
#define uploadRequestsRejected uploadRequestsRejected_removed
#elif defined(P08_T5_NEGATE_RESPONSE_STAT)
#define uploadResponsesFramed uploadResponsesFramed_removed
#elif defined(P08_T5_NEGATE_CANCEL_STAT)
#define uploadRequestsCancelled uploadRequestsCancelled_removed
#elif defined(P08_T5_NEGATE_ABORT_STAT)
#define uploadAbortsFramed uploadAbortsFramed_removed
#endif

#include "server1/ports/TorrentTransport.h"

#if defined(P08_T5_NEGATE_UPLOAD_ID)
#undef UploadRequestId
#elif defined(P08_T5_NEGATE_UPLOAD_OWNERSHIP)
#undef UploadOwnership
#elif defined(P08_T5_NEGATE_ADVERTISE_ACTION)
#undef AdvertisePieceAction
#elif defined(P08_T5_NEGATE_RESPONSE_ACTION)
#undef UploadResponseAction
#elif defined(P08_T5_NEGATE_ABORT_ACTION)
#undef UploadAbortAction
#elif defined(P08_T5_NEGATE_REQUEST_OBSERVATION)
#undef UploadRequestObservation
#elif defined(P08_T5_NEGATE_CANCEL_OBSERVATION)
#undef UploadCancelObservation
#elif defined(P08_T5_NEGATE_ACTION_VARIANT)
#undef TorrentAction
#elif defined(P08_T5_NEGATE_OBSERVATION_VARIANT)
#undef TorrentObservation
#elif defined(P08_T5_NEGATE_PEER_CAP)
#undef kMaxOutstandingUploadsPerPeer
#elif defined(P08_T5_NEGATE_GLOBAL_CAP)
#undef kMaxOutstandingUploadsGlobal
#elif defined(P08_T5_NEGATE_OUTSTANDING_STAT)
#undef ownedUploadsOutstanding
#elif defined(P08_T5_NEGATE_ACCEPTED_STAT)
#undef uploadRequestsAccepted
#elif defined(P08_T5_NEGATE_REJECTED_STAT)
#undef uploadRequestsRejected
#elif defined(P08_T5_NEGATE_RESPONSE_STAT)
#undef uploadResponsesFramed
#elif defined(P08_T5_NEGATE_CANCEL_STAT)
#undef uploadRequestsCancelled
#elif defined(P08_T5_NEGATE_ABORT_STAT)
#undef uploadAbortsFramed
#endif

#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

using namespace server1::ports;

static_assert(std::is_same_v<UploadRequestId, std::uint64_t>);
static_assert(std::is_same_v<decltype(UploadOwnership::requestId), UploadRequestId>);
static_assert(std::is_same_v<decltype(UploadOwnership::generation), EngineGeneration>);
static_assert(std::is_same_v<decltype(AdvertisePieceAction::generation), EngineGeneration>);
static_assert(std::is_same_v<decltype(AdvertisePieceAction::piece), std::uint32_t>);
static_assert(std::is_same_v<decltype(UploadResponseAction::ownership), UploadOwnership>);
static_assert(std::is_same_v<decltype(UploadResponseAction::peer), PeerHandle>);
static_assert(std::is_same_v<decltype(UploadResponseAction::block), BlockSpan>);
static_assert(std::is_same_v<decltype(UploadResponseAction::payload), std::vector<std::uint8_t>>);
static_assert(std::is_same_v<decltype(UploadAbortAction::ownership), UploadOwnership>);
static_assert(std::is_same_v<decltype(UploadAbortAction::peer), PeerHandle>);
static_assert(std::is_same_v<decltype(UploadAbortAction::block), BlockSpan>);
static_assert(std::is_same_v<decltype(UploadRequestObservation::ownership), UploadOwnership>);
static_assert(std::is_same_v<decltype(UploadRequestObservation::peer), PeerHandle>);
static_assert(std::is_same_v<decltype(UploadRequestObservation::block), BlockSpan>);
static_assert(std::is_same_v<decltype(UploadCancelObservation::ownership), UploadOwnership>);
static_assert(std::is_same_v<decltype(UploadCancelObservation::peer), PeerHandle>);
static_assert(std::is_same_v<decltype(UploadCancelObservation::block), BlockSpan>);

static_assert(kMaxOutstandingUploadsPerPeer == 4);
static_assert(kMaxOutstandingUploadsGlobal == 20);

static_assert(std::variant_size_v<TorrentAction> == 9);
static_assert(std::is_same_v<std::variant_alternative_t<0, TorrentAction>, RequestAction>);
static_assert(std::is_same_v<std::variant_alternative_t<1, TorrentAction>, CancelAction>);
static_assert(std::is_same_v<std::variant_alternative_t<2, TorrentAction>, InterestAction>);
static_assert(std::is_same_v<std::variant_alternative_t<3, TorrentAction>, ChokeAction>);
static_assert(std::is_same_v<std::variant_alternative_t<4, TorrentAction>, ConnectAction>);
static_assert(std::is_same_v<std::variant_alternative_t<5, TorrentAction>, PauseAction>);
static_assert(std::is_same_v<std::variant_alternative_t<6, TorrentAction>, AdvertisePieceAction>);
static_assert(std::is_same_v<std::variant_alternative_t<7, TorrentAction>, UploadResponseAction>);
static_assert(std::is_same_v<std::variant_alternative_t<8, TorrentAction>, UploadAbortAction>);

static_assert(std::variant_size_v<TorrentObservation> == 9);
static_assert(std::is_same_v<std::variant_alternative_t<0, TorrentObservation>, BlockObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<1, TorrentObservation>, PeerObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<2, TorrentObservation>, FailureObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<3, TorrentObservation>, ClosedObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<4, TorrentObservation>, MetadataReadyObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<5, TorrentObservation>, SourceFailureObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<6, TorrentObservation>, AvailablePiecesObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<7, TorrentObservation>, UploadRequestObservation>);
static_assert(std::is_same_v<std::variant_alternative_t<8, TorrentObservation>, UploadCancelObservation>);

static_assert(std::is_same_v<decltype(TransportStatistics::ownedUploadsOutstanding), std::uint32_t>);
static_assert(std::is_same_v<decltype(TransportStatistics::uploadRequestsAccepted), std::uint64_t>);
static_assert(std::is_same_v<decltype(TransportStatistics::uploadRequestsRejected), std::uint64_t>);
static_assert(std::is_same_v<decltype(TransportStatistics::uploadResponsesFramed), std::uint64_t>);
static_assert(std::is_same_v<decltype(TransportStatistics::uploadRequestsCancelled), std::uint64_t>);
static_assert(std::is_same_v<decltype(TransportStatistics::uploadAbortsFramed), std::uint64_t>);

int main()
{
    const UploadOwnership ownership{17, 9};
    const BlockSpan tail{3, 2, 2 * kWireBlockLength, 73};
    const TorrentAction advertise = AdvertisePieceAction{9, 3};
    const TorrentAction response = UploadResponseAction{ownership, 44, tail, std::vector<std::uint8_t>(73, 0x5a)};
    const TorrentAction abort = UploadAbortAction{ownership, 44, tail};
    const TorrentObservation request = UploadRequestObservation{ownership, 44, tail};
    const TorrentObservation cancel = UploadCancelObservation{ownership, 44, tail};
    TransportStatistics stats{};
    stats.ownedUploadsOutstanding = 1;
    stats.uploadRequestsAccepted = 1;
    stats.uploadRequestsRejected = 2;
    stats.uploadResponsesFramed = 3;
    stats.uploadRequestsCancelled = 4;
    stats.uploadAbortsFramed = 5;
    return std::get<AdvertisePieceAction>(advertise).piece == 3
            && std::get<UploadResponseAction>(response).payload.size() == 73
            && std::get<UploadAbortAction>(abort).peer == 44
            && std::get<UploadRequestObservation>(request).ownership.requestId == 17
            && std::get<UploadCancelObservation>(cancel).ownership.generation == 9
            && stats.ownedUploadsOutstanding == 1
            && stats.uploadRequestsAccepted == 1
            && stats.uploadRequestsRejected == 2
            && stats.uploadResponsesFramed == 3
            && stats.uploadRequestsCancelled == 4
            && stats.uploadAbortsFramed == 5 ? 0 : 1;
}
