// tests/guided_types_harness.cpp
#include "guided/GuidedTypes.h"
#include <QJsonDocument>
#include <cstdio>

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

int main() {
    using namespace guided;
    GuidedPath p;
    p.canvasFingerprint = "sha256:abc";
    p.modelVersion = "panel-yolo26n-535bbe1";
    p.plannerVersion = "guided-v1";
    p.outcome = PlanOutcome::Trusted;
    p.steps = {
        {StepKind::Overview, -1, {0, 0, 1, 1}, 0.8, 0.35, 1.0},
        {StepKind::Panel, 7, {0.5, 0, 0.5, 0.5}, 2.4, 0.62, 0.91},
        {StepKind::Overview, -1, {0, 0, 1, 1}, 0.8, 0.62, 1.0}
    };
    const QByteArray bytes = serializePath(p);
    const auto roundTrip = deserializePath(bytes);
    CHECK(roundTrip.has_value(), "path JSON round-trip");
    CHECK(roundTrip->steps.size() == 3, "three immutable steps");
    CHECK(roundTrip->steps[1].camera.contains({0.75, 0.25}), "normalized geometry");
    CHECK(toCode(FallbackCode::LayoutAmbiguous) == "layout_ambiguous", "stable error code");

    // Round-trip must be faithful field-by-field: both Panel Step and Auto Read
    // deserialize byte-identical paths from these bytes.
    CHECK(roundTrip->canvasFingerprint == p.canvasFingerprint, "fingerprint survives");
    CHECK(roundTrip->modelVersion == p.modelVersion, "model version survives");
    CHECK(roundTrip->plannerVersion == p.plannerVersion, "planner version survives");
    CHECK(roundTrip->outcome == PlanOutcome::Trusted, "outcome survives");
    CHECK(roundTrip->steps[1].kind == StepKind::Panel, "step kind survives");
    CHECK(roundTrip->steps[1].sourcePanelId == 7, "source panel id survives");
    CHECK(qFuzzyCompare(roundTrip->steps[1].holdSecondsAt1x, 2.4), "hold time survives");
    CHECK(qFuzzyCompare(roundTrip->steps[1].plannerConfidence, 0.91), "confidence survives");
    CHECK(serializePath(*roundTrip) == bytes, "re-serialization is byte-identical");

    // Strict parser: reject unparseable input and unknown schema versions so a
    // future schema bump can never be silently misread as v1.
    CHECK(!deserializePath(QByteArray("not json at all")).has_value(), "malformed rejected");
    CHECK(!deserializePath(QByteArray(R"({"steps":[]})")).has_value(), "missing schema rejected");
    CHECK(!deserializePath(QByteArray(R"({"schema":2,"steps":[]})")).has_value(), "unknown schema rejected");
    CHECK(deserializePath(QByteArray(R"({"schema":1,"steps":[]})")).has_value(), "empty v1 path accepted");

    std::puts("GUIDED_TYPES_OK");
    return 0;
}
