#include "server1/http/SystemStatistics.h"

#include <uv.h>

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using server1::policy::Value;

void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

void checkKeys(const Value &value, std::initializer_list<std::string_view> keys)
{
    check(value.kind() == Value::Kind::Object, "expected an object");
    check(value.asObject().size() == keys.size(), "unexpected object keys");
    std::size_t index = 0;
    for (const auto key : keys) {
        check(value.asObject()[index++].first == key, "unexpected key order");
    }
}

void checkNumber(const Value &value)
{
    check(value.kind() == Value::Kind::Number, "expected a number");
    check(std::isfinite(value.asNumber()) && value.asNumber() >= 0,
          "expected a finite nonnegative number");
}

void checkShape(const Value &statistics)
{
    checkKeys(statistics, {"loadavg", "cpus"});
    const auto &averages = *statistics.find("loadavg");
    check(averages.kind() == Value::Kind::Array && averages.asArray().size() == 3,
          "load averages must have three entries");
    for (const auto &average : averages.asArray()) checkNumber(average);
#ifdef _WIN32
    for (const auto &average : averages.asArray()) {
        check(average.asNumber() == 0, "Windows load averages must be zero");
    }
#endif
    const auto &cpus = *statistics.find("cpus");
    check(cpus.kind() == Value::Kind::Array, "CPUs must be an array");
    // A restricted environment may legitimately have no readable CPU data.
    for (const auto &cpu : cpus.asArray()) {
        checkKeys(cpu, {"model", "speed", "times"});
        check(cpu.find("model")->kind() == Value::Kind::String, "CPU model must be a string");
        checkNumber(*cpu.find("speed"));
        check(std::floor(cpu.find("speed")->asNumber()) == cpu.find("speed")->asNumber(),
              "CPU MHz must be an integer");
        const auto &times = *cpu.find("times");
        checkKeys(times, {"user", "nice", "sys", "idle", "irq"});
        for (const auto &entry : times.asObject()) {
            checkNumber(entry.second);
            check(std::floor(entry.second.asNumber()) == entry.second.asNumber(),
                  "CPU time must be integer milliseconds");
        }
    }
}
} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--uv-version") {
            std::cout << uv_version_string() << '\n';
            return 0;
        }
        const auto statistics = server1::http::systemStatistics();
        checkShape(statistics);
        if (argc == 2 && std::string_view(argv[1]) == "--json") {
            std::cout << server1::policy::jsonStringify(statistics) << '\n';
        } else {
            // Repeated calls also exercise ownership of libuv's allocations.
            for (int attempt = 0; attempt < 10; ++attempt) {
                checkShape(server1::http::systemStatistics());
            }
            std::cout << "system statistics live shape passed\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
