#pragma once

#include <QtCore/QtTypes>

#include <atomic>

namespace Colosseum::Player2 {

class PlaybackGeneration
{
public:
    quint64 current() const noexcept { return m_value.load(std::memory_order_acquire); }
    quint64 advance() noexcept
    {
        return m_value.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    bool accepts(quint64 candidate) const noexcept { return candidate == current(); }

private:
    std::atomic<quint64> m_value{0};
};

} // namespace Colosseum::Player2
