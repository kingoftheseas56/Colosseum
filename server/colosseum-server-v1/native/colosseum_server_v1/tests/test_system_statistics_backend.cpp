#include "server1/http/SystemStatistics.h"

#include <uv.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

// This executable links the production helper with deterministic replacements
// for only the three libuv calls it consumes. Never link these into the server.
namespace {
using server1::policy::Value;
enum class Sample { Normal, Empty, Failure, Windows };
Sample sample = Sample::Normal;
std::string calls;
int releases = 0;
char firstModel[] = "CPU \"first\"\n\xae";
char secondModel[] = "";
uv_cpu_info_t information[2] = {
    {firstModel, 3200, {11, 22, 33, 44, 55}},
    {secondModel, 0, {9007199254740993ULL, 0, 1, std::numeric_limits<std::uint64_t>::max(), 2}},
};

void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

Value expected(bool windows)
{
    return Value::object({
        {"loadavg", Value::array({Value::number(windows ? 0 : 1.25),
                                 Value::number(windows ? 0 : 2.5),
                                 Value::number(windows ? 0 : 3.75)})},
        {"cpus", Value::array({
            Value::object({
                {"model", Value::string("CPU \"first\"\n\xc2\xae")},
                {"speed", Value::number(3200)},
                {"times", Value::object({
                    {"user", Value::number(11)}, {"nice", Value::number(windows ? 0 : 22)},
                    {"sys", Value::number(33)}, {"idle", Value::number(44)},
                    {"irq", Value::number(55)},
                })},
            }),
            Value::object({
                {"model", Value::string("")},
                {"speed", Value::number(0)},
                {"times", Value::object({
                    {"user", Value::number(9007199254740992.0)}, {"nice", Value::number(0)},
                    {"sys", Value::number(1)}, {"idle", Value::number(18446744073709551616.0)},
                    {"irq", Value::number(2)},
                })},
            }),
        })},
    });
}
} // namespace

extern "C" void uv_loadavg(double average[3])
{
    calls += 'L';
    average[0] = sample == Sample::Windows ? 0 : 1.25;
    average[1] = sample == Sample::Windows ? 0 : 2.5;
    average[2] = sample == Sample::Windows ? 0 : 3.75;
}

extern "C" int uv_cpu_info(uv_cpu_info_t **result, int *count)
{
    calls += 'C';
    if (sample == Sample::Failure) return UV_EACCES;
    *result = information;
    *count = sample == Sample::Empty ? 0 : 2;
    information[0].cpu_times.nice = sample == Sample::Windows ? 0 : 22;
    return 0;
}

extern "C" void uv_free_cpu_info(uv_cpu_info_t *result, int count)
{
    calls += 'F';
    if (result != information || count != (sample == Sample::Empty ? 0 : 2)) {
        std::terminate();
    }
    ++releases;
}

int main(int argc, char **argv)
{
    try {
        if (argc == 2) {
            const std::string mode(argv[1]);
            if (mode == "--failure") sample = Sample::Failure;
            else if (mode == "--empty") sample = Sample::Empty;
            else if (mode == "--windows") sample = Sample::Windows;
            else if (mode != "--fixture") throw std::runtime_error("unknown fixture mode");
            std::cout << server1::policy::jsonStringify(server1::http::systemStatistics()) << '\n';
            return 0;
        }
        for (const auto mode : {Sample::Normal, Sample::Empty, Sample::Failure, Sample::Windows}) {
            sample = mode;
            calls.clear();
            const auto before = releases;
            const auto actual = server1::http::systemStatistics();
            check(calls == (mode == Sample::Failure ? "LC" : "LCF"), "query/release order mismatch");
            check(releases == before + (mode == Sample::Failure ? 0 : 1), "CPU allocation ownership mismatch");
            auto reference = expected(mode == Sample::Windows);
            if (mode == Sample::Empty || mode == Sample::Failure) {
                reference = Value::object({
                    {"loadavg", *reference.find("loadavg")}, {"cpus", Value::array({})},
                });
            }
            check(server1::policy::jsonStringify(actual) == server1::policy::jsonStringify(reference),
                  "backend mapping differs from Node's OS shape/number/string semantics");
        }
        std::cout << "system statistics deterministic backend passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
