#pragma once
#include "server1/policy/EngineCounters.h"
#include <exception>
class QObject;
namespace server1::policy {
// Uses the existing Qt event loop. Factory calls are app-lane only.
// Handles permit cancellation/destruction on another thread; QObject cleanup
// is deferred to the owning event loop, which must outlive normal teardown.
// Owner destruction cancels delivery. No new thread or worker pool is created.
// Required error sink must not throw. Callback exceptions cannot cross Qt dispatch.
using EngineTimerError = std::function<void(std::exception_ptr)>;
EngineAfter qtEngineAfter(QObject *owner, EngineTimerError onError);
}
