#include "slot_ring.h"

#include <cassert>
#include <iostream>

int main()
{
    SlotRing ring;

    assert(!ring.acquireLatestForConsumer().has_value());

    const auto first = ring.claimForProducer();
    assert(first && *first == 0);
    assert(ring.state(*first) == SlotState::Producing);
    assert(ring.publishProduced(*first, 1));

    const auto displayFirst = ring.acquireLatestForConsumer();
    assert(displayFirst);
    assert(displayFirst->slot == *first);
    assert(displayFirst->sequence == 1);
    assert(!displayFirst->retiringSlot.has_value());
    assert(ring.state(*first) == SlotState::Displaying);

    const auto second = ring.claimForProducer();
    assert(second && *second == 1);
    assert(ring.publishProduced(*second, 2));

    const auto displaySecond = ring.acquireLatestForConsumer();
    assert(displaySecond);
    assert(displaySecond->slot == *second);
    assert(displaySecond->sequence == 2);
    assert(displaySecond->retiringSlot == first);
    assert(ring.state(*first) == SlotState::Retiring);
    assert(ring.state(*second) == SlotState::Displaying);

    const auto third = ring.claimForProducer();
    assert(third && *third == 2);
    assert(!ring.claimForProducer().has_value());

    assert(ring.retireAfterConsumerSubmission(*first, 10));
    ring.markConsumerFenceComplete(9);
    assert(ring.state(*first) == SlotState::Retiring);
    ring.markConsumerFenceComplete(10);
    assert(ring.state(*first) == SlotState::Free);

    assert(!ring.publishProduced(*third, 2));
    assert(ring.publishProduced(*third, 3));

    std::cout << "slot_ring_test: PASS\n";
    return 0;
}
