#pragma once

#include "Player2Types.h"

#include <optional>

namespace Colosseum::Player2
{
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
