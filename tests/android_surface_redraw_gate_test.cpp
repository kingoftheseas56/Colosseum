#include "platform/AndroidSurfaceRedrawGate.h"
#include <iostream>

int main() {
    using Gate = Colosseum::Platform::AndroidSurfaceRedrawGate;
    int failures = 0;
    int checks = 0;
    auto expect = [&](bool condition, const char *message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Gate gate;
    expect(gate.presented(1240, 2772).action == Gate::Action::None, "idle frames must not acknowledge");
    gate.begin(1, 2772, 1240);
    expect(gate.presented(1240, 2772).action == Gate::Action::None, "old orientation must not acknowledge");
    expect(gate.presented(2772, 1239).action == Gate::Action::RequestFrame, "first matching swap requests redraw");
    auto complete = gate.presented(2772, 1240);
    expect(complete.action == Gate::Action::Complete && complete.token == 1, "second matching swap completes");
    expect(gate.presented(2772, 1240).action == Gate::Action::None, "completion must occur once");

    gate.begin(2, 2772, 1240);
    gate.presented(2772, 1240);
    gate.begin(3, 1240, 2772);
    expect(!gate.isCurrent(2) && gate.isCurrent(3), "a newer resize invalidates queued completions");
    expect(gate.presented(2772, 1240).action == Gate::Action::None, "superseded orientation must not finish new resize");
    expect(gate.presented(1240, 2772).action == Gate::Action::RequestFrame, "new resize resets presentation count");
    expect(gate.presented(1240, 2772).token == 3, "new resize owns its acknowledgement");

    gate.begin(4, 1240, 2772);
    gate.presented(1240, 2772);
    gate.begin(0, 0, 0);
    expect(!gate.isCurrent(4), "surface destruction invalidates pending callback");
    expect(gate.presented(1240, 2772).action == Gate::Action::None, "destroyed surface cannot acknowledge");
    gate.begin(5, 0, 2772);
    expect(gate.presented(0, 2772).action == Gate::Action::None, "zero-width surface must not acknowledge");
    gate.begin(6, 1240, 2772);
    expect(gate.presented(1235, 2772).action == Gate::Action::None, "fractional-DPR tolerance must stay bounded");
    expect(gate.presented(1240, 2771).action == Gate::Action::RequestFrame, "one-pixel DPR rounding is accepted");
    expect(gate.presented(1239, 2772).action == Gate::Action::Complete, "rounded frame completes at correct orientation");
    if (failures) return 1;
    std::cout << "ANDROID_SURFACE_REDRAW_GATE_OK checks=" << checks << '\n';
}
