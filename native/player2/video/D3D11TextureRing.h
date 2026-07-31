#pragma once

#include <QtCore/QtTypes>

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

namespace Colosseum::Player2 {

struct VideoFrameToken
{
    quint64 generation = 0;
    quint64 sequence = 0;
    qint64 ptsUs = 0;
    // Presentation metadata travels with the frame identity. These are deliberately separate from
    // ptsUs: dimensions describe the acquired picture, while PTS describes its media timeline.
    int sourceWidth = 0;
    int sourceHeight = 0;
};

enum class TextureSlotState
{
    Free,
    Producing,
    Ready,
    Displaying,
    Retiring
};

class D3D11TextureRing
{
public:
    static constexpr std::size_t SlotCount = 3;

    struct ConsumerSelection
    {
        std::size_t slot = 0;
        VideoFrameToken token;
        std::optional<std::size_t> retiringSlot;
    };

    explicit D3D11TextureRing(quint64 generation = 0);

    std::optional<std::size_t> claimForProducer();
    bool cancelProducer(std::size_t slot);
    bool publishProduced(std::size_t slot, VideoFrameToken token);
    std::optional<ConsumerSelection> acquireLatestForConsumer(quint64 generation);
    // Consumer follows the ring: presents the latest Ready frame of the ring's CURRENT
    // generation, whatever seeks/track-switches have advanced it to. The paint item uses
    // this so it never needs to be told about generation changes (the seek-freeze fix).
    std::optional<ConsumerSelection> acquireLatestForConsumer();
    bool retireAfterConsumerSubmission(std::size_t slot, quint64 consumerFenceValue);
    void markConsumerFenceComplete(quint64 completedValue);
    void flush(quint64 nextGeneration);

    TextureSlotState state(std::size_t slot) const;
    quint64 generation() const;
    quint64 producerStarvationCount() const;

private:
    std::optional<ConsumerSelection> acquireLatestForConsumerLocked(quint64 generation);

    struct Slot
    {
        TextureSlotState state = TextureSlotState::Free;
        VideoFrameToken token;
        quint64 claimedGeneration = 0;
        quint64 consumerFenceValue = 0;
    };

    mutable std::mutex m_mutex;
    std::array<Slot, SlotCount> m_slots{};
    quint64 m_generation = 0;
    quint64 m_lastPublishedSequence = 0;
    quint64 m_producerStarvationCount = 0;
};

} // namespace Colosseum::Player2
