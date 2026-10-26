#include "server1/policy/Scheduler.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using server1::policy::Scheduler;
using server1::policy::SchedulerEventType;
using server1::policy::Value;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        fail(message);
    }
}

void caseK0301()
{
    Scheduler scheduler(8);
    const auto low = scheduler.select(0, 5, Value::boolean(false), 4, 1);
    const auto firstEqual = scheduler.select(1, 6, Value::boolean(true), 5, 2);
    const auto high = scheduler.select(2, 7, Value::number(3), 6, 3);
    const auto secondEqual = scheduler.select(0, 3, Value::number(1), 2, 0);

    const auto ordered = scheduler.selections();
    expect(ordered.size() == 4, "K03-01 selection count");
    expect(ordered[0].id == high && ordered[1].id == firstEqual
               && ordered[2].id == secondEqual && ordered[3].id == low,
           "K03-01 priority order must be descending and stable for equal priority");
    expect(ordered[3].priority == 0.0, "K03-01 false priority converts to zero");
    expect(scheduler.updateReadWindow(low, 2, 3), "K03-01 update bounded read window");
    const auto updated = scheduler.find(low);
    expect(updated && updated->readFrom == 2 && updated->selectTo == 3,
           "K03-01 readFrom/selectTo update");
    expect(!scheduler.updateReadWindow(low, 4, 3), "K03-01 rejects inverted read window");
    expect(!scheduler.updateReadWindow(low, 0, 7), "K03-01 rejects window outside selection");
}

void caseK0302()
{
    Scheduler scheduler(10);
    static_cast<void>(scheduler.select(0, 3, Value::number(0)));
    static_cast<void>(scheduler.select(5, 8, Value::number(2), 7, 6));
    const std::vector<bool> available{true, true, true, true, true, true, false, true, true, true};

    const auto initial = scheduler.choosePiece(available, 0, true);
    expect(initial && *initial == 3,
           "K03-02 fresh peer scans the sorted selection list and pieces in reverse");
    const auto normal = scheduler.choosePiece(available, 1, true);
    expect(normal && *normal == 5,
           "K03-02 prior payload uses priority-sorted normal forward selection path");
    expect(!scheduler.choosePiece(available, 0, false),
           "K03-02 initial policy does not issue a second outstanding request");

    Scheduler priorities(8);
    const auto first = priorities.select(0, 1, Value::number(3));
    const auto second = priorities.select(2, 3, Value::number(2));
    const auto third = priorities.select(4, 5, Value::number(1));
    static_cast<void>(priorities.select(6, 7, Value::number(0)));
    expect(priorities.rotatePriorityAfterBudgetFill(first),
           "K03-02 a truthy priority rotates after request budget fill");
    const auto rotated = priorities.selections();
    expect(rotated[0].id == second && rotated[1].id == third && rotated[2].id == first,
           "K03-02 request-budget rotation moves source selection behind truthy priorities");
}

void caseK0303()
{
    Scheduler scheduler(4);
    const auto id = scheduler.select(0, 2, Value::number(1));
    auto events = scheduler.takeEvents();
    expect(events.size() == 1 && events[0].type == SchedulerEventType::Interested,
           "K03-03 first selection emits interested");

    scheduler.markPieceComplete(0);
    scheduler.markPieceComplete(1);
    scheduler.markPieceComplete(2);
    scheduler.collectGarbage();
    events = scheduler.takeEvents();
    expect(events.size() == 4, "K03-03 exact completion event count");
    expect(events[0].type == SchedulerEventType::SelectionNotify && events[0].selectionId == id,
           "K03-03 offset advance notifies selection");
    expect(events[1].type == SchedulerEventType::SelectionNotify && events[1].selectionId == id,
           "K03-03 finished selection receives terminal notify");
    expect(events[2].type == SchedulerEventType::Uninterested,
           "K03-03 final removal emits uninterested");
    expect(events[3].type == SchedulerEventType::Idle, "K03-03 final removal emits idle");
    expect(scheduler.selections().empty(), "K03-03 finished selection removed");

    scheduler.collectGarbage();
    expect(scheduler.takeEvents().empty(), "K03-03 repeated GC does not duplicate idle transition");

    scheduler.setCritical(3, 0);
    expect(scheduler.isCritical(3), "K03-03 width zero retains source default width of one");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        fail("expected one case id");
    }
    const std::string id = argv[1];
    if (id == "K03-01") {
        caseK0301();
    } else if (id == "K03-02") {
        caseK0302();
    } else if (id == "K03-03") {
        caseK0303();
    } else {
        fail("unknown case id");
    }
    std::cout << id << " PASS\n";
    return 0;
}
