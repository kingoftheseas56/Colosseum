#include "server1/policy/SchedulerActions.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace server1::policy;
[[noreturn]] void fail(const std::string &message) { std::cerr << message << '\n'; std::exit(1); }
void expect(bool condition, const std::string &message) { if (!condition) fail(message); }

NormalRequestCandidate candidate(SelectionId selection, std::uint64_t generation,
                                 std::size_t piece, std::size_t block)
{
    return {selection, generation, piece, block, block * 16384, 16384, true, false};
}

void caseK0401()
{
    expect(Scheduler::requestBudget(0) == 50 && Scheduler::requestBudget(30) == 5,
           "K04-01 source request-budget bounds");
    SchedulerActionContract contract;
    RequestDecisionContext context{12, 7, 30, 3, 32768.0};
    const auto actions = contract.decide(context,
        {candidate(11, 7, 2, 0), candidate(11, 7, 2, 1), candidate(12, 6, 3, 0)}, {});
    expect(actions.size() == 2, "K04-01 first pass fills remaining normal request budget");
    expect(actions[0].type == SchedulerActionType::Request
               && actions[0].request.peer == 12
               && actions[0].request.generation == 7
               && actions[0].request.selectionId == 11
               && actions[0].request.piece == 2 && actions[0].request.block == 0,
           "K04-01 request action preserves generation/peer/selection/block identity");
}

void caseK0402()
{
    SchedulerActionContract contract;
    RequestIdentity victim{91, 4, 3, 8, 8, 2, 32768, 16384};
    expect(contract.track(victim), "K04-02 victim tracked");
    HotswapRequestCandidate swap{victim, 8192.0, candidate(3, 4, 8, 2), true};
    RequestDecisionContext context{9, 4, 30, 5, 16384.0};
    const auto actions = contract.decide(context, {}, {swap});
    expect(actions.size() == 2 && actions[0].type == SchedulerActionType::Cancel
               && actions[0].request.requestId == 91 && actions[0].requestWireCancel
               && actions[1].type == SchedulerActionType::Request
               && actions[1].request.peer == 9,
           "K04-02 second pass emits explicit victim cancel then replacement request");
    expect(contract.outcome(91) == RequestOutcome::Replaced && contract.terminalCount(91) == 1,
           "K04-02 hotswap records exactly one terminal outcome");
    expect(!contract.finish(91, 4, RequestOutcome::Failed),
           "K04-02 late failure cannot terminalize replaced request twice");
}

void caseK0403()
{
    SchedulerActionContract contract;
    const RequestIdentity first{1, 5, 2, 7, 3, 0, 0, 4};
    const RequestIdentity stale{2, 4, 2, 7, 3, 1, 4, 4};
    expect(contract.track(first) && contract.track(stale), "K04-03 requests tracked");
    expect(contract.replacePiece(3, 5) == 1, "K04-03 replacement is generation aware");
    expect(contract.outcome(2) == RequestOutcome::Active,
           "K04-03 replacement leaves another generation untouched");
    expect(!contract.finish(2, 5, RequestOutcome::Failed)
               && contract.finish(2, 4, RequestOutcome::Failed),
           "K04-03 completion rejects the wrong generation");

    contract.pulse(100, 100, 101.0, 100.0, 0);
    contract.pulse(100, 100, 101.0, 100.0, 100);
    expect(contract.takeActions().size() == 2, "K04-03 each pulse emits update first");
    contract.advancePulse(599, 100, 100, 101.0, 100.0);
    expect(contract.takeActions().empty(), "K04-03 debounce resets to latest pulse plus 500ms");
    contract.advancePulse(600, 100, 100, 100.0, 100.0);
    const auto actions = contract.takeActions();
    expect(actions.size() == 2 && actions[0].type == SchedulerActionType::Update
               && actions[1].type == SchedulerActionType::Select,
           "K04-03 debounced reevaluation updates before selecting");
}
}

int main(int argc, char **argv)
{
    if (argc != 2) fail("expected one case id");
    const std::string id = argv[1];
    if (id == "K04-01") caseK0401();
    else if (id == "K04-02") caseK0402();
    else if (id == "K04-03") caseK0403();
    else fail("unknown case id");
    std::cout << id << " PASS\n";
}
