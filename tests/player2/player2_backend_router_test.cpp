// Task 17 — the backend router contract. The app has two video backends: the shipped mpvqt player and
// Player 2. This test IS the spec for which one a given playback gets, and what happens when Player 2
// fails.
//
// Two decisions, both pure (no Qt objects, no engine, no GPU — this runs anywhere):
//   1. chooseBackend()          — before anything opens: which backend, and if the viewer asked for
//                                 Player 2 and isn't getting it, WHY (a reason string that reaches
//                                 diagnostics, never a silent downgrade).
//   2. classifyRuntimeFailure() — Player 2 died mid-flight: may we swap to mpvqt, or must we restart?
//                                 The dividing line is the FIRST PRESENTED FRAME. Before it, nothing
//                                 is on screen and a fallback is invisible; after it, clocks/audio are
//                                 running and a hot swap would mean two clocks mastering one playback.
//                                 The plan's rule: report and retry as a NEW session, never hot-swap.

#include "player2/PlayerBackendRouter.h"
#include "player2/core/Player2Types.h"

#include <QtCore/QUrl>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// A request that is fine by every backend: a real local file, not live.
PlaybackRequest playableRequest()
{
    PlaybackRequest request;
    request.source = QUrl::fromLocalFile(QStringLiteral("C:/media/the-wire-s04e10.mkv"));
    request.mediaId = QStringLiteral("tt0306414:4:10");
    request.title = QStringLiteral("The Wire — S4E10");
    return request;
}

// Player 2 fully able: built into this binary and holding a usable adapter.
Player2Capabilities capableEngine()
{
    Player2Capabilities capabilities;
    capabilities.compiledIn = true;
    capabilities.adapterUsable = true;
    return capabilities;
}

// --- chooseBackend ---------------------------------------------------------------------------

// The shipped player stays the default. No opt-in => mpvqt, and this is NOT a fallback: nobody asked
// for Player 2, so there is no reason to report.
void testDefaultIsMpvQt()
{
    const BackendDecision decision =
        chooseBackend(playableRequest(), PlayerBackend::MpvQt, capableEngine());

    require(decision.outcome == BackendOutcome::UseMpvQt, "default must route to mpvqt");
    require(decision.backend == PlayerBackend::MpvQt, "default backend must be mpvqt");
    require(decision.reason.isEmpty(), "the default choice is not a fallback and carries no reason");
}

// The opt-in is honored when the engine can actually run.
void testExplicitOptInUsesPlayer2()
{
    const BackendDecision decision =
        chooseBackend(playableRequest(), PlayerBackend::Player2, capableEngine());

    require(decision.outcome == BackendOutcome::UsePlayer2, "opt-in with a capable engine must use Player 2");
    require(decision.backend == PlayerBackend::Player2, "backend must be Player 2");
    require(decision.reason.isEmpty(), "a granted opt-in carries no fallback reason");
}

// Built out of the binary (the promotion flag was off): the opt-in cannot be honored, and the viewer's
// diagnostics must say so rather than pretending mpvqt was the choice.
void testNotCompiledInFallsBack()
{
    Player2Capabilities capabilities = capableEngine();
    capabilities.compiledIn = false;

    const BackendDecision decision =
        chooseBackend(playableRequest(), PlayerBackend::Player2, capabilities);

    require(decision.outcome == BackendOutcome::FallbackToMpvQt, "an absent engine must fall back");
    require(decision.backend == PlayerBackend::MpvQt, "fallback lands on mpvqt");
    require(!decision.reason.isEmpty(), "a fallback must always carry a reason");
}

// The unsupported-adapter case: the binary has Player 2 but this machine's GPU path is unusable.
void testUnsupportedAdapterFallsBack()
{
    Player2Capabilities capabilities = capableEngine();
    capabilities.adapterUsable = false;

    const BackendDecision decision =
        chooseBackend(playableRequest(), PlayerBackend::Player2, capabilities);

    require(decision.outcome == BackendOutcome::FallbackToMpvQt, "an unusable adapter must fall back");
    require(decision.backend == PlayerBackend::MpvQt, "fallback lands on mpvqt");
    require(decision.reason.contains(QStringLiteral("adapter")),
            "the adapter fallback must name the adapter so diagnostics are actionable");
}

// Live/DVR is a written parity exception for Player 2 (lab ledger: live guide + DVR panels not built).
// A live request therefore routes to mpvqt even under opt-in — surfaced as a reason, not a silent
// downgrade, and not a lie that Player 2 played it.
void testLiveFallsBackWhileDvrIsUnbuilt()
{
    PlaybackRequest request = playableRequest();
    request.live = true;

    const BackendDecision decision =
        chooseBackend(request, PlayerBackend::Player2, capableEngine());

    require(decision.outcome == BackendOutcome::FallbackToMpvQt, "live must fall back while DVR is unbuilt");
    require(decision.reason.contains(QStringLiteral("live")), "the live fallback must name live");
}

// A request neither backend can play is not a backend decision — it is a bad request. Reporting it as
// "fell back to mpvqt" would send the viewer to a player that also cannot open it.
void testEmptySourceIsTerminal()
{
    PlaybackRequest request;
    request.mediaId = QStringLiteral("tt0306414");

    const BackendDecision decision =
        chooseBackend(request, PlayerBackend::Player2, capableEngine());

    require(decision.outcome == BackendOutcome::TerminalError, "an empty source is terminal, not a fallback");
    require(!decision.reason.isEmpty(), "a terminal error must carry a reason");
}

// The terminal verdict does not depend on which backend was asked for.
void testEmptySourceIsTerminalUnderDefaultToo()
{
    PlaybackRequest request;

    const BackendDecision decision =
        chooseBackend(request, PlayerBackend::MpvQt, capableEngine());

    require(decision.outcome == BackendOutcome::TerminalError,
            "an unplayable request is terminal regardless of the requested backend");
}

// --- classifyRuntimeFailure ------------------------------------------------------------------

// Player 2 failed to initialize before it ever painted. Nothing is on screen, no clock is mastering,
// so mpvqt can take over invisibly — this is the one legal hot fallback.
void testFailureBeforeFirstFrameFallsBack()
{
    const FailureResponse response = classifyRuntimeFailure(/*firstFrameSeen=*/false);

    require(response == FailureResponse::FallbackToMpvQt,
            "a pre-first-frame failure may fall back invisibly");
}

// Player 2 died with the picture already up. Audio/video clocks are running; handing the same session
// to mpvqt would mean two clocks mastering one playback (the plan's explicit prohibition). The only
// honest move is to surface it and restart as a NEW session.
void testFailureAfterFirstFrameRestartsInsteadOfHotSwapping()
{
    const FailureResponse response = classifyRuntimeFailure(/*firstFrameSeen=*/true);

    require(response == FailureResponse::RestartAsNewSession,
            "a post-first-frame failure must restart as a new session, never hot-swap clocks");
    require(response != FailureResponse::FallbackToMpvQt,
            "a post-first-frame failure must NOT silently swap backends mid-session");
}

} // namespace

int main()
{
    try {
        testDefaultIsMpvQt();
        testExplicitOptInUsesPlayer2();
        testNotCompiledInFallsBack();
        testUnsupportedAdapterFallsBack();
        testLiveFallsBackWhileDvrIsUnbuilt();
        testEmptySourceIsTerminal();
        testEmptySourceIsTerminalUnderDefaultToo();
        testFailureBeforeFirstFrameFallsBack();
        testFailureAfterFirstFrameRestartsInsteadOfHotSwapping();
    } catch (const std::exception &error) {
        std::cerr << "player2_backend_router_test FAILED: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "player2_backend_router_test: PASS" << std::endl;
    return EXIT_SUCCESS;
}
