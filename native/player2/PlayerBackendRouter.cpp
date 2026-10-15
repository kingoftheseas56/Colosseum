#include "player2/PlayerBackendRouter.h"

namespace Colosseum::Player2
{
namespace
{
BackendDecision fallback(const QString &reason)
{
    return BackendDecision{BackendOutcome::FallbackToMpvQt, PlayerBackend::MpvQt, reason};
}
}

BackendDecision chooseBackend(const PlaybackRequest &request,
                              PlayerBackend requested,
                              const Player2Capabilities &capabilities)
{
    // Unplayable by anyone. Reporting this as a fallback would send the viewer to a second player that
    // also cannot open it, and hide the real fault.
    if (request.source.isEmpty()) {
        return BackendDecision{BackendOutcome::TerminalError, PlayerBackend::MpvQt,
                               QStringLiteral("no playable source in the request")};
    }

    if (requested != PlayerBackend::Player2)
        return BackendDecision{BackendOutcome::UseMpvQt, PlayerBackend::MpvQt, QString()};

    if (!capabilities.compiledIn)
        return fallback(QStringLiteral("Player 2 is not built into this binary"));

    if (!capabilities.adapterUsable)
        return fallback(QStringLiteral("no usable D3D11 adapter for the Player 2 zero-copy path"));

    // Live/DVR is a written parity exception: Player 2 has no live guide or DVR surface yet. Routing
    // live to it would trade a working feature for a missing one, so it goes to mpvqt WITH a reason.
    if (request.live)
        return fallback(QStringLiteral("live/DVR playback is not built in Player 2 yet"));

    return BackendDecision{BackendOutcome::UsePlayer2, PlayerBackend::Player2, QString()};
}

FailureResponse classifyRuntimeFailure(bool firstFrameSeen)
{
    return firstFrameSeen ? FailureResponse::RestartAsNewSession
                          : FailureResponse::FallbackToMpvQt;
}
}
