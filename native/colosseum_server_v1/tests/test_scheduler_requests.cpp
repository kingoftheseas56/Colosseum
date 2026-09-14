#include "server1/policy/Scheduler.h"
#include "../src/policy/SchedulerRequests.cpp"
#include "../src/policy/SchedulerHotswap.cpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using server1::policy::HotswapCandidate;
using server1::policy::PulseActionType;
using server1::policy::PulseDisposition;
using server1::policy::PulseGate;
using server1::policy::RequestLedger;
using server1::policy::RequestOutcome;
using server1::policy::Scheduler;

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

void caseK0401()
{
    const std::vector<std::pair<std::size_t, int>> cases{
        {0, 50}, {1, 50}, {2, 44}, {15, 8}, {30, 5}, {100, 5}};
    for (const auto &[unchoked, expected] : cases) {
        expect(Scheduler::requestBudget(unchoked) == expected, "K04-01 source request budget");
    }
}

void caseK0402()
{
    expect(!Scheduler::hotswapVictim(16383.0, {{1, 100.0, true}}),
           "K04-02 requester below 16 KiB/s cannot hotswap");
    const auto atRequestThreshold = Scheduler::hotswapVictim(16384.0, {{1, 8192.0, true}});
    expect(atRequestThreshold && *atRequestThreshold == 1,
           "K04-02 requester at 16 KiB/s and exactly 2x qualifies");
    expect(!Scheduler::hotswapVictim(200000.0, {{1, 49152.0, true}}),
           "K04-02 candidate at 48 KiB/s threshold is protected");
    const auto ties = Scheduler::hotswapVictim(
        40000.0, {{3, 10000.0, true}, {4, 10000.0, true}, {5, 1000.0, false}});
    expect(ties && *ties == 4, "K04-02 equal slow candidates select the later source reservation");
    expect(!Scheduler::hotswapVictim(40000.0, {{3, 1000.0, false}}),
           "K04-02 canceled reservation is not a candidate");
}

void caseK0403()
{
    RequestLedger ledger;
    expect(ledger.begin(1, 3, 0), "K04-03 begin request");
    expect(ledger.replacePiece(3) == 1, "K04-03 replacing piece terminalizes request");
    expect(!ledger.finish(1, RequestOutcome::Failed),
           "K04-03 late failure after replacement cannot create a second terminal outcome");
    expect(ledger.terminalCount(1) == 1 && ledger.outcome(1) == RequestOutcome::Replaced,
           "K04-03 one policy owner and one terminal outcome");

    expect(ledger.begin(2, 4, 0) && ledger.begin(3, 5, 0), "K04-03 corrupt group requests");
    const auto reset = ledger.invalidateGroup(4, 6);
    expect(reset == std::vector<std::size_t>{4, 5}, "K04-03 corrupt group exact reset range");
    expect(ledger.outcome(2) == RequestOutcome::CorruptReset
               && ledger.outcome(3) == RequestOutcome::CorruptReset,
           "K04-03 corrupt group terminal outcomes");

    expect(Scheduler::pulseDisposition(100, 100, 101.0, 100.0) == PulseDisposition::Debounced,
           "K04-03 flood equality and speed above pulse debounce");
    expect(Scheduler::pulseDisposition(99, 100, 1000.0, 100.0) == PulseDisposition::Immediate,
           "K04-03 below flood is immediate");
    expect(Scheduler::pulseDisposition(100, 100, 100.0, 100.0) == PulseDisposition::Immediate,
           "K04-03 pulse equality is immediate");

    PulseGate gate;
    gate.update(100, 100, 101.0, 100.0, 0);
    gate.update(100, 100, 101.0, 100.0, 100);
    expect(gate.takeActions().size() == 2, "K04-03 each pulse call emits update first");
    gate.advance(599, 100, 100, 101.0, 100.0);
    expect(gate.takeActions().empty(), "K04-03 debounce resets to latest call plus 500ms");
    gate.advance(600, 100, 100, 100.0, 100.0);
    auto actions = gate.takeActions();
    expect(actions == std::vector<PulseActionType>{PulseActionType::Update, PulseActionType::Select},
           "K04-03 debounced reevaluation emits update then selects when threshold clears");
    gate.update(0, 100, 0.0, 100.0, 700);
    actions = gate.takeActions();
    expect(actions == std::vector<PulseActionType>{PulseActionType::Update, PulseActionType::Select},
           "K04-03 below-threshold order is update then immediate select");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        fail("expected one case id");
    }
    const std::string id = argv[1];
    if (id == "K04-01") {
        caseK0401();
    } else if (id == "K04-02") {
        caseK0402();
    } else if (id == "K04-03") {
        caseK0403();
    } else {
        fail("unknown case id");
    }
    std::cout << id << " PASS\n";
    return 0;
}
