#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

enum class SlotState
{
    Free,
    Producing,
    Ready,
    Displaying,
    Retiring
};

class SlotRing
{
public:
    static constexpr std::size_t SlotCount = 3;

    struct ConsumerSelection
    {
        std::size_t slot = 0;
        std::uint64_t sequence = 0;
        std::optional<std::size_t> retiringSlot;
    };

    std::optional<std::size_t> claimForProducer()
    {
        std::scoped_lock lock(m_mutex);
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].state == SlotState::Free) {
                m_slots[i].state = SlotState::Producing;
                return i;
            }
        }
        return std::nullopt;
    }

    bool publishProduced(std::size_t slot, std::uint64_t sequence)
    {
        std::scoped_lock lock(m_mutex);
        if (slot >= m_slots.size() || m_slots[slot].state != SlotState::Producing ||
            sequence <= m_lastPublishedSequence) {
            return false;
        }
        m_slots[slot].sequence = sequence;
        m_slots[slot].state = SlotState::Ready;
        m_lastPublishedSequence = sequence;
        return true;
    }

    std::optional<ConsumerSelection> acquireLatestForConsumer()
    {
        std::scoped_lock lock(m_mutex);
        std::optional<std::size_t> latest;
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].state == SlotState::Ready &&
                (!latest || m_slots[i].sequence > m_slots[*latest].sequence)) {
                latest = i;
            }
        }
        if (!latest)
            return std::nullopt;

        ConsumerSelection selection;
        selection.slot = *latest;
        selection.sequence = m_slots[*latest].sequence;

        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].state == SlotState::Displaying) {
                m_slots[i].state = SlotState::Retiring;
                m_slots[i].consumerFenceValue = 0;
                selection.retiringSlot = i;
            } else if (m_slots[i].state == SlotState::Ready && i != *latest) {
                m_slots[i] = Slot{};
            }
        }

        m_slots[*latest].state = SlotState::Displaying;
        return selection;
    }

    bool retireAfterConsumerSubmission(std::size_t slot, std::uint64_t consumerFenceValue)
    {
        std::scoped_lock lock(m_mutex);
        if (slot >= m_slots.size() || m_slots[slot].state != SlotState::Retiring ||
            consumerFenceValue == 0) {
            return false;
        }
        m_slots[slot].consumerFenceValue = consumerFenceValue;
        return true;
    }

    void markConsumerFenceComplete(std::uint64_t completedValue)
    {
        std::scoped_lock lock(m_mutex);
        for (auto &slot : m_slots) {
            if (slot.state == SlotState::Retiring && slot.consumerFenceValue != 0 &&
                slot.consumerFenceValue <= completedValue) {
                slot = Slot{};
            }
        }
    }

    SlotState state(std::size_t slot) const
    {
        std::scoped_lock lock(m_mutex);
        return slot < m_slots.size() ? m_slots[slot].state : SlotState::Free;
    }

private:
    struct Slot
    {
        SlotState state = SlotState::Free;
        std::uint64_t sequence = 0;
        std::uint64_t consumerFenceValue = 0;
    };

    mutable std::mutex m_mutex;
    std::array<Slot, SlotCount> m_slots{};
    std::uint64_t m_lastPublishedSequence = 0;
};
