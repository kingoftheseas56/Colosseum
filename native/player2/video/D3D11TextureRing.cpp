#include "D3D11TextureRing.h"

namespace Colosseum::Player2 {

D3D11TextureRing::D3D11TextureRing(quint64 generation)
    : m_generation(generation)
{
}

std::optional<std::size_t> D3D11TextureRing::claimForProducer()
{
    std::scoped_lock lock(m_mutex);
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].state == TextureSlotState::Free) {
            m_slots[i].state = TextureSlotState::Producing;
            m_slots[i].claimedGeneration = m_generation;
            return i;
        }
    }
    ++m_producerStarvationCount;
    return std::nullopt;
}

bool D3D11TextureRing::cancelProducer(std::size_t slot)
{
    std::scoped_lock lock(m_mutex);
    if (slot >= m_slots.size() || m_slots[slot].state != TextureSlotState::Producing)
        return false;
    m_slots[slot] = Slot{};
    return true;
}

bool D3D11TextureRing::publishProduced(std::size_t slot, VideoFrameToken token)
{
    std::scoped_lock lock(m_mutex);
    if (slot >= m_slots.size() || m_slots[slot].state != TextureSlotState::Producing)
        return false;

    Slot &target = m_slots[slot];
    if (target.claimedGeneration != m_generation || token.generation != m_generation ||
        token.sequence == 0 || token.sequence <= m_lastPublishedSequence) {
        target = Slot{};
        return false;
    }

    target.token = token;
    target.state = TextureSlotState::Ready;
    m_lastPublishedSequence = token.sequence;
    return true;
}

std::optional<D3D11TextureRing::ConsumerSelection>
D3D11TextureRing::acquireLatestForConsumer(quint64 generation)
{
    std::scoped_lock lock(m_mutex);
    if (generation != m_generation)
        return std::nullopt;

    std::optional<std::size_t> latest;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].state == TextureSlotState::Ready &&
            m_slots[i].token.generation == generation &&
            (!latest || m_slots[i].token.sequence > m_slots[*latest].token.sequence)) {
            latest = i;
        }
    }
    if (!latest)
        return std::nullopt;

    ConsumerSelection selection{*latest, m_slots[*latest].token, std::nullopt};
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].state == TextureSlotState::Displaying) {
            m_slots[i].state = TextureSlotState::Retiring;
            m_slots[i].consumerFenceValue = 0;
            selection.retiringSlot = i;
        } else if (m_slots[i].state == TextureSlotState::Ready && i != *latest) {
            m_slots[i] = Slot{};
        }
    }
    m_slots[*latest].state = TextureSlotState::Displaying;
    return selection;
}

bool D3D11TextureRing::retireAfterConsumerSubmission(std::size_t slot,
                                                      quint64 consumerFenceValue)
{
    std::scoped_lock lock(m_mutex);
    if (slot >= m_slots.size() || m_slots[slot].state != TextureSlotState::Retiring ||
        consumerFenceValue == 0) {
        return false;
    }
    m_slots[slot].consumerFenceValue = consumerFenceValue;
    return true;
}

void D3D11TextureRing::markConsumerFenceComplete(quint64 completedValue)
{
    std::scoped_lock lock(m_mutex);
    for (Slot &slot : m_slots) {
        if (slot.state == TextureSlotState::Retiring && slot.consumerFenceValue != 0 &&
            slot.consumerFenceValue <= completedValue) {
            slot = Slot{};
        }
    }
}

void D3D11TextureRing::flush(quint64 nextGeneration)
{
    std::scoped_lock lock(m_mutex);
    m_generation = nextGeneration;
    m_lastPublishedSequence = 0;
    for (Slot &slot : m_slots) {
        if (slot.state == TextureSlotState::Ready)
            slot = Slot{};
    }
}

TextureSlotState D3D11TextureRing::state(std::size_t slot) const
{
    std::scoped_lock lock(m_mutex);
    return slot < m_slots.size() ? m_slots[slot].state : TextureSlotState::Free;
}

quint64 D3D11TextureRing::generation() const
{
    std::scoped_lock lock(m_mutex);
    return m_generation;
}

quint64 D3D11TextureRing::producerStarvationCount() const
{
    std::scoped_lock lock(m_mutex);
    return m_producerStarvationCount;
}

} // namespace Colosseum::Player2
