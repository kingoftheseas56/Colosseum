#pragma once

#include "Player2Types.h"

#include <optional>

namespace Colosseum::Player2
{
enum class NetworkState; // defined in player2/network/HttpMediaSource.h

// Pure mapping from a streaming transport event to the player state it should drive, given the
// current player state. Returns no value when the event should not change the player state (e.g. a
// Streaming heartbeat while already Playing). Buffering pauses playback; Recovering surfaces a
// reconnect; Streaming resumes; Failed is a terminal network error.
std::optional<Player2State> networkStateTarget(Player2State current, NetworkState network);

struct StateTransitionResult
{
    bool accepted = false;
    bool changed = false;
    Player2State previous = Player2State::Idle;
    Player2State current = Player2State::Idle;
    std::optional<Player2Error> error;
};

class Player2StateMachine
{
public:
    explicit Player2StateMachine(Player2State initialState = Player2State::Idle) noexcept;

    Player2State state() const noexcept;
    StateTransitionResult transitionTo(Player2State nextState);

    static bool canTransition(Player2State from, Player2State to) noexcept;

private:
    Player2State m_state = Player2State::Idle;
};
}
