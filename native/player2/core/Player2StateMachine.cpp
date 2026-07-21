#include "Player2StateMachine.h"

#include <QtCore/QStringLiteral>

namespace Colosseum::Player2
{
Player2StateMachine::Player2StateMachine(Player2State initialState) noexcept
    : m_state(initialState)
{
}

Player2State Player2StateMachine::state() const noexcept
{
    return m_state;
}

StateTransitionResult Player2StateMachine::transitionTo(Player2State nextState)
{
    StateTransitionResult result;
    result.previous = m_state;
    result.current = m_state;

    if (!canTransition(m_state, nextState)) {
        result.error = Player2Error{
            Player2ErrorCode::InvalidCommand,
            QStringLiteral("Illegal Player 2 state transition"),
            false,
        };
        return result;
    }

    result.accepted = true;
    result.changed = m_state != nextState;
    m_state = nextState;
    result.current = m_state;
    return result;
}

bool Player2StateMachine::canTransition(Player2State from, Player2State to) noexcept
{
    if (from == to)
        return true;

    switch (from) {
    case Player2State::Idle:
        return to == Player2State::Opening;

    case Player2State::Opening:
        return to == Player2State::Buffering || to == Player2State::Playing ||
               to == Player2State::Paused || to == Player2State::Ended ||
               to == Player2State::Recovering || to == Player2State::Error ||
               to == Player2State::Idle;

    case Player2State::Buffering:
    case Player2State::Playing:
    case Player2State::Paused:
    case Player2State::Seeking:
        return to == Player2State::Buffering || to == Player2State::Playing ||
               to == Player2State::Paused || to == Player2State::Seeking ||
               to == Player2State::Ended || to == Player2State::Recovering ||
               to == Player2State::Error || to == Player2State::Idle;

    case Player2State::Ended:
        return to == Player2State::Opening || to == Player2State::Seeking ||
               to == Player2State::Idle;

    case Player2State::Recovering:
        return to == Player2State::Opening || to == Player2State::Buffering ||
               to == Player2State::Playing || to == Player2State::Paused ||
               to == Player2State::Seeking || to == Player2State::Ended ||
               to == Player2State::Error || to == Player2State::Idle;

    case Player2State::Error:
        return to == Player2State::Opening || to == Player2State::Recovering ||
               to == Player2State::Idle;
    }

    return false;
}
}
