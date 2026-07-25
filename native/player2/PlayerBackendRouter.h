#pragma once

#include "player2/core/Player2Types.h"

#include <QtCore/QString>

namespace Colosseum::Player2
{
// PlayerBackendRouter answers one question, once per playback, before anything opens: which video
// backend plays this — the shipped mpvqt player, or Player 2?
//
// It is deliberately PURE (no QObject, no engine, no GPU, no settings read). Callers gather the facts
// — what the viewer picked, what this binary/machine can actually do — and the router only judges. That
// keeps the policy testable without a window, a device or a file.
//
// Two rules the rest of the integration leans on:
//   * A fallback is never silent. If the viewer asked for Player 2 and did not get it, the decision
//     carries a human reason that reaches diagnostics.
//   * There is no mid-session backend swap once a frame has been presented. See classifyRuntimeFailure.

enum class PlayerBackend
{
    MpvQt,
    Player2
};

// What this binary and this machine can actually do — engine facts, not preferences.
struct Player2Capabilities
{
    // Player 2 was linked into this build (the in-app promotion flag). False in a stock build.
    bool compiledIn = false;
    // A usable D3D11 adapter was found for the zero-copy path.
    bool adapterUsable = false;
};

enum class BackendOutcome
{
    UseMpvQt,          // the default path; nobody asked for anything else
    UsePlayer2,        // opt-in granted
    FallbackToMpvQt,   // Player 2 was asked for and refused — `reason` says why
    TerminalError      // the request itself is unplayable; no backend can help
};

struct BackendDecision
{
    BackendOutcome outcome = BackendOutcome::UseMpvQt;
    PlayerBackend backend = PlayerBackend::MpvQt;
    // Non-empty exactly when the outcome is FallbackToMpvQt or TerminalError. The plain default choice
    // is not a fallback and carries no reason.
    QString reason;
};

// `requested` is the viewer's setting (default MpvQt). Kept separate from both the request and the
// capabilities on purpose: it is a preference, and folding a preference into the media request or into
// the engine's capability facts would blur what each of those means. (This is a documented, deliberate
// widening of the plan's two-argument signature.)
BackendDecision chooseBackend(const PlaybackRequest &request,
                              PlayerBackend requested,
                              const Player2Capabilities &capabilities);

enum class FailureResponse
{
    FallbackToMpvQt,     // nothing was on screen yet — mpvqt can take over invisibly
    RestartAsNewSession  // the picture was already up — surface it and start clean
};

// Player 2 failed while it owned the playback. The dividing line is the first PRESENTED frame: before
// it nothing is visible and no clock is mastering, so a fallback is free; after it, audio/video clocks
// are running and handing the same session to mpvqt would put two clocks on one playback. In that case
// the honest move is to report and retry as a new session — never a hot swap.
FailureResponse classifyRuntimeFailure(bool firstFrameSeen);
}
