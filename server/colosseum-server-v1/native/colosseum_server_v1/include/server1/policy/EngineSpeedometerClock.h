#pragma once
#include "server1/policy/EngineSpeedometer.h"
#include "server1/policy/EngineTiming.h"

namespace server1::policy {
// Create once per source-facing application and share this reader across engines.
// Tick starts at 1 and advances once per delivered 250ms interval, not wall time.
// Construction uses the app lane. Reads are safe on transport lanes. The final
// reader release cancels the owned timer through EngineTimer's lifetime contract.
EngineSpeedometer::Tick engineSpeedometerClock(const EngineRepeat &repeat);
}
