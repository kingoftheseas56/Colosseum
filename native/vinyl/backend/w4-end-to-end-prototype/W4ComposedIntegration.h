#pragma once

#include "VinylListenEventLedger.h"

namespace w4h {

int verifyMainJourneyRecommendationFacts(
    const vinyl::ListenEventLedger &ledger,
    const vinyl::ProfileScopeId &profileId,
    const vinyl::RecordingMbid &completedRecording,
    const vinyl::RecordingMbid &failedRecording);

int runComposedIntegrationProofs();

} // namespace w4h
