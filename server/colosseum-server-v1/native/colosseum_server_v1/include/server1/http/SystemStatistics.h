#pragma once

#include "server1/policy/Value.h"

namespace server1::http {

// The sys object used by the source /stats.json route: os.loadavg(), os.cpus().
// Uses libuv's native platform implementation, as Node does. CPU times are in
// milliseconds, speed is in MHz, and a failed CPU query yields an empty array.
[[nodiscard]] policy::Value systemStatistics();

} // namespace server1::http
