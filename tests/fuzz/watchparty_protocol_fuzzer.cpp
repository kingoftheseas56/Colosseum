// libFuzzer target for the Watch Party wire protocol — the highest-value network
// attack surface: in a Watch Party, a REMOTE PEER's messages arrive over a
// WebSocket and land as raw bytes in Colosseum::WatchParty::decodeMessage(). A
// hostile or buggy peer controls these bytes in real time, so any crash here is
// at minimum a remote DoS of the client and at worst a memory-safety compromise.
//
// This drives the exact production ingestion path the socket layer uses:
//
//   decodeMessage(QByteArray)      -> 64KB ceiling + QJsonDocument::fromJson +
//                                     envelope/version/type parse
//   validateMessage(...)           -> both directions
//   *FromJson(payload, out)        -> every field extractor
//
// Every extractor is run against each decoded payload (a mismatched payload just
// returns false) to widen coverage of the parsing/validation code per input.
// Built with clang-cl -fsanitize=fuzzer-no-link,address + a /MD-CRT libFuzzer so
// it can link against the /MD Qt6Core release library. A defect surfaces as an
// ASan report.

#include "watchparty/WatchPartyProtocol.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonObject>

#include <cstddef>
#include <cstdint>

using namespace Colosseum::WatchParty;

namespace {
QCoreApplication* g_app = nullptr;
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    g_app = new QCoreApplication(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const QByteArray bytes(reinterpret_cast<const char*>(data),
                           static_cast<qsizetype>(size));

    const DecodeResult decoded = decodeMessage(bytes);
    if (!decoded.ok)
        return 0; // rejected envelope/JSON/size — nothing decoded to exercise

    validateMessage(decoded.message, MessageDirection::ClientToServer);
    validateMessage(decoded.message, MessageDirection::ServerToClient);

    const QJsonObject& payload = decoded.message.payload;

    SourceDescriptor sourceDescriptor;
    sourceDescriptorFromJson(payload, &sourceDescriptor);
    ParticipantIdentity participantIdentity;
    participantIdentityFromJson(payload, &participantIdentity);
    ParticipantState participantState;
    participantStateFromJson(payload, &participantState);
    TimelineState timelineState;
    timelineStateFromJson(payload, &timelineState);
    TimelineCommand timelineCommand;
    timelineCommandFromJson(payload, &timelineCommand);
    ChatEvent chatEvent;
    chatEventFromJson(payload, &chatEvent);
    ReactionEvent reactionEvent;
    reactionEventFromJson(payload, &reactionEvent);
    RoomSnapshot roomSnapshot;
    roomSnapshotFromJson(payload, &roomSnapshot);
    SessionEstablished sessionEstablished;
    sessionEstablishedFromJson(payload, &sessionEstablished);

    return 0;
}
