#include "player2/video/D3D11TextureRing.h"
#include "player2/video/D3D11VideoPipeline.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;

namespace
{
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

VideoFrameToken token(quint64 generation, quint64 sequence, qint64 ptsUs,
                      int sourceWidth = 0, int sourceHeight = 0)
{
    return VideoFrameToken{generation, sequence, ptsUs, sourceWidth, sourceHeight};
}

void latestReadyFrameWinsAndOlderReadyFramesAreReleased()
{
    D3D11TextureRing ring(7);
    const auto first = ring.claimForProducer();
    const auto second = ring.claimForProducer();
    require(first.has_value() && second.has_value(), "two producer slots must be available");
    require(ring.publishProduced(*first, token(7, 1, 10'000)), "first frame must publish");
    require(ring.publishProduced(*second, token(7, 2, 20'000)), "second frame must publish");

    const auto selection = ring.acquireLatestForConsumer(7);
    require(selection.has_value(), "latest frame must be selectable");
    require(selection->slot == *second, "newest sequence must win");
    require(selection->token.sequence == 2, "selected token must be preserved");
    require(!selection->retiringSlot.has_value(), "first display has no retiring slot");
    require(ring.state(*first) == TextureSlotState::Free, "older ready slot must be released");
    require(ring.state(*second) == TextureSlotState::Displaying, "newest slot must display");
}

void producerStarvationIsObservable()
{
    D3D11TextureRing ring(1);
    for (std::size_t i = 0; i < D3D11TextureRing::SlotCount; ++i)
        require(ring.claimForProducer().has_value(), "each physical slot must be claimable once");

    require(!ring.claimForProducer().has_value(), "a fourth producer claim must starve");
    require(ring.producerStarvationCount() == 1, "starvation counter must increment exactly once");
}

void consumerFenceControlsRetiringSlotReuse()
{
    D3D11TextureRing ring(3);
    const auto first = ring.claimForProducer();
    require(first.has_value(), "first producer slot missing");
    require(ring.publishProduced(*first, token(3, 1, 10'000)), "first publish failed");
    require(ring.acquireLatestForConsumer(3).has_value(), "first display failed");

    const auto second = ring.claimForProducer();
    require(second.has_value(), "second producer slot missing");
    require(ring.publishProduced(*second, token(3, 2, 20'000)), "second publish failed");
    const auto selection = ring.acquireLatestForConsumer(3);
    require(selection.has_value() && selection->retiringSlot == first,
            "previous display must enter retirement");
    require(ring.state(*first) == TextureSlotState::Retiring, "old display must be retiring");
    require(ring.retireAfterConsumerSubmission(*first, 9), "retirement fence must attach");

    ring.markConsumerFenceComplete(8);
    require(ring.state(*first) == TextureSlotState::Retiring,
            "slot must not recycle before its fence completes");
    ring.markConsumerFenceComplete(9);
    require(ring.state(*first) == TextureSlotState::Free,
            "slot must recycle when its fence completes");
}

void flushInvalidatesOldGenerationWithoutReusingInFlightSlots()
{
    D3D11TextureRing ring(11);
    const auto ready = ring.claimForProducer();
    const auto producing = ring.claimForProducer();
    require(ready.has_value() && producing.has_value(), "flush setup slots missing");
    require(ring.publishProduced(*ready, token(11, 1, 10'000)), "ready setup publish failed");

    ring.flush(12);
    require(ring.generation() == 12, "flush must advance the generation");
    require(ring.state(*ready) == TextureSlotState::Free,
            "unconsumed ready frame may be released on flush");
    require(ring.state(*producing) == TextureSlotState::Producing,
            "an in-flight producer slot must not be recycled underneath the producer");
    require(!ring.acquireLatestForConsumer(11).has_value(), "old generation cannot acquire");
    require(!ring.publishProduced(*producing, token(11, 2, 20'000)),
            "old producer completion must be rejected");
    require(ring.state(*producing) == TextureSlotState::Free,
            "rejected stale completion must release its slot");

    const auto current = ring.claimForProducer();
    require(current.has_value(), "new generation slot missing");
    require(ring.publishProduced(*current, token(12, 1, 30'000)),
            "sequence numbering must restart in a new generation");
    const auto selected = ring.acquireLatestForConsumer(12);
    require(selected.has_value() && selected->token.generation == 12,
            "only the current generation may display");
}

void flushKeepsDisplayedTextureAliveUntilConsumerRetiresIt()
{
    D3D11TextureRing ring(20);
    const auto oldDisplay = ring.claimForProducer();
    require(oldDisplay.has_value(), "old display slot missing");
    require(ring.publishProduced(*oldDisplay, token(20, 1, 10'000)), "old display publish failed");
    require(ring.acquireLatestForConsumer(20).has_value(), "old display acquire failed");

    ring.flush(21);
    require(ring.state(*oldDisplay) == TextureSlotState::Displaying,
            "flush must not recycle a texture still owned by the scene graph");

    const auto newDisplay = ring.claimForProducer();
    require(newDisplay.has_value(), "new display slot missing");
    require(ring.publishProduced(*newDisplay, token(21, 1, 20'000)), "new display publish failed");
    const auto selected = ring.acquireLatestForConsumer(21);
    require(selected.has_value() && selected->retiringSlot == oldDisplay,
            "first new-generation frame must retire the old display");
}

// The seek-freeze regression: a seek advances the ring's generation (flush), but the QML
// item kept presenting with the generation it was born with — so after the first seek the
// ring rejected every acquire and the picture froze on the last pre-seek frame while audio
// played on. The self-generation acquire presents whatever generation the ring currently
// owns; the consumer never needs to be told about seeks.
void consumerFollowsRingGenerationAcrossSeeks()
{
    D3D11TextureRing ring(1);
    const auto preSeek = ring.claimForProducer();
    require(preSeek.has_value(), "pre-seek producer slot missing");
    require(ring.publishProduced(*preSeek, token(1, 1, 10'000)), "pre-seek publish failed");
    require(ring.acquireLatestForConsumer(1).has_value(), "pre-seek display failed");

    ring.flush(2); // the seek: generation advances, Ready slots are cleared
    const auto postSeek = ring.claimForProducer();
    require(postSeek.has_value(), "post-seek producer slot missing");
    require(ring.publishProduced(*postSeek, token(2, 1, 60'000'000)),
            "post-seek publish failed");

    // The frozen-picture bug, preserved as documentation: a stale-generation consumer
    // acquires nothing, forever.
    require(!ring.acquireLatestForConsumer(1).has_value(),
            "stale-generation consumer must still be rejected");
    // The fix: the self-generation acquire follows the ring across the seek.
    const auto selection = ring.acquireLatestForConsumer();
    require(selection.has_value(), "self-generation consumer must see the post-seek frame");
    require(selection->token.generation == 2 && selection->token.ptsUs == 60'000'000,
            "the presented frame must be the post-seek frame");
}

// Dimensions answer "what picture has the render thread presented?", not "how far has demux
// reached?" or "what did the producer submit?". They therefore travel with the acquired token and
// become diagnostic state only when that exact, still-current presentation frame is acknowledged.
void dimensionsPublishOnlyForAcknowledgedCurrentGenerationPresentation()
{
    D3D11TextureRing ring(1);
    const auto produced = ring.claimForProducer();
    require(produced.has_value(), "dimension-test producer slot missing");
    require(ring.publishProduced(*produced, token(1, 1, 250'000, 426, 240)),
            "dimension-test frame must publish");
    const auto acquired = ring.acquireLatestForConsumer();
    require(acquired.has_value(), "dimension-test frame must be acquired");
    require(acquired->token.sourceWidth == 426 && acquired->token.sourceHeight == 240,
            "acquired presentation token must carry its source dimensions separately from PTS");

    D3D11VideoPipeline pipeline;
    auto diagnostics = pipeline.diagnostics();
    require(diagnostics.sourceWidth == 0 && diagnostics.sourceHeight == 0 &&
                diagnostics.presented == 0,
            "producer publication/acquisition must not expose dimensions as presented");

    const D3D11VideoPipeline::PresentationFrame current{
        acquired->slot, acquired->token, acquired->retiringSlot};
    require(pipeline.notePresented(current),
            "current-generation render acknowledgement must be accepted");
    diagnostics = pipeline.diagnostics();
    require(diagnostics.sourceWidth == 426 && diagnostics.sourceHeight == 240 &&
                diagnostics.presented == 1,
            "successful presentation must publish that frame's exact dimensions");

    pipeline.flush(2);
    diagnostics = pipeline.diagnostics();
    require(diagnostics.sourceWidth == 0 && diagnostics.sourceHeight == 0,
            "flush must clear dimensions with the generation they describe");
    require(!pipeline.notePresented(current),
            "a flushed old-generation presentation acknowledgement must be rejected");
    diagnostics = pipeline.diagnostics();
    require(diagnostics.sourceWidth == 0 && diagnostics.sourceHeight == 0 &&
                diagnostics.presented == 1,
            "stale acknowledgement must not republish dimensions or increment presented");

    const D3D11VideoPipeline::PresentationFrame next{
        0, token(2, 2, 500'000, 640, 360), std::nullopt};
    require(pipeline.notePresented(next),
            "new-generation render acknowledgement must be accepted");
    diagnostics = pipeline.diagnostics();
    require(diagnostics.sourceWidth == 640 && diagnostics.sourceHeight == 360 &&
                diagnostics.presented == 2,
            "new generation must publish only its own acknowledged frame dimensions");
}

void invalidOperationsDoNotCorruptOwnership()
{
    D3D11TextureRing ring(4);
    const auto cancelled = ring.claimForProducer();
    require(cancelled.has_value() && ring.cancelProducer(*cancelled),
            "producer cancellation must release an in-flight slot");
    require(ring.state(*cancelled) == TextureSlotState::Free,
            "cancelled producer slot must be reusable");
    require(!ring.cancelProducer(*cancelled),
            "a free slot cannot be cancelled twice");
    require(!ring.publishProduced(D3D11TextureRing::SlotCount, token(4, 1, 0)),
            "out-of-range publish must fail");
    require(!ring.retireAfterConsumerSubmission(0, 1),
            "a non-retiring slot cannot accept a fence");
    require(!ring.retireAfterConsumerSubmission(D3D11TextureRing::SlotCount, 1),
            "an out-of-range slot cannot accept a fence");
    require(!ring.acquireLatestForConsumer(5).has_value(),
            "a non-current generation cannot acquire");
}
}

int main()
{
    try {
        latestReadyFrameWinsAndOlderReadyFramesAreReleased();
        producerStarvationIsObservable();
        consumerFenceControlsRetiringSlotReuse();
        flushInvalidatesOldGenerationWithoutReusingInFlightSlots();
        flushKeepsDisplayedTextureAliveUntilConsumerRetiresIt();
        consumerFollowsRingGenerationAcrossSeeks();
        dimensionsPublishOnlyForAcknowledgedCurrentGenerationPresentation();
        invalidOperationsDoNotCorruptOwnership();
    } catch (const std::exception &error) {
        std::cerr << "player2_texture_ring_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "player2_texture_ring_test: PASS\n";
    return EXIT_SUCCESS;
}
