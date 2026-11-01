#include "server1/http/SystemStatistics.h"

#include <uv.h>

#include <memory>
#include <string>
#include <utility>

namespace server1::http {
namespace {
using policy::Value;

struct CpuInfoDeleter {
    int count;
    void operator()(uv_cpu_info_t *information) const noexcept
    {
        uv_free_cpu_info(information, count);
    }
};

std::string nodeModel(const char *model)
{
    // Node's native os binding uses OneByteString, not UTF-8 decoding. Value
    // stores UTF-8, so preserve the corresponding Latin-1 code points here.
    std::string result;
    for (const auto *byte = reinterpret_cast<const unsigned char *>(model); *byte; ++byte) {
        if (*byte < 0x80) {
            result.push_back(static_cast<char>(*byte));
        } else {
            result.push_back(static_cast<char>(0xc0 | (*byte >> 6)));
            result.push_back(static_cast<char>(0x80 | (*byte & 0x3f)));
        }
    }
    return result;
}

Value cpuStatistics()
{
    uv_cpu_info_t *information = nullptr;
    int count = 0;
    if (uv_cpu_info(&information, &count) != 0) {
        // Node's os.cpus() uses [] when its native query fails.
        return Value::array({});
    }
    const std::unique_ptr<uv_cpu_info_t, CpuInfoDeleter> owner(information, {count});
    Value::Array cpus;
    cpus.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto &cpu = information[index];
        const auto &times = cpu.cpu_times;
        cpus.push_back(Value::object({
            {"model", Value::string(nodeModel(cpu.model))},
            {"speed", Value::number(cpu.speed)},
            {"times", Value::object({
                {"user", Value::number(static_cast<double>(times.user))},
                {"nice", Value::number(static_cast<double>(times.nice))},
                {"sys", Value::number(static_cast<double>(times.sys))},
                {"idle", Value::number(static_cast<double>(times.idle))},
                {"irq", Value::number(static_cast<double>(times.irq))},
            })},
        }));
    }
    return Value::array(std::move(cpus));
}
} // namespace

policy::Value systemStatistics()
{
    double averages[3] = {};
    uv_loadavg(averages);
    // Preserve source evaluation order: load averages precede the CPU query.
    auto cpus = cpuStatistics();
    return Value::object({
        {"loadavg", Value::array({Value::number(averages[0]),
                                 Value::number(averages[1]),
                                 Value::number(averages[2])})},
        {"cpus", std::move(cpus)},
    });
}

} // namespace server1::http
