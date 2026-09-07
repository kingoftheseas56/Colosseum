#include "server1/Runtime.h"

namespace server1 {

bool Runtime::initialize() noexcept
{
    initialized_ = true;
    return true;
}

void Runtime::shutdown() noexcept
{
    initialized_ = false;
}

bool Runtime::initialized() const noexcept
{
    return initialized_;
}

bool Runtime::streamingReady() const noexcept
{
    return false;
}

std::vector<std::string> Runtime::capabilities() const
{
    return {};
}

} // namespace server1
