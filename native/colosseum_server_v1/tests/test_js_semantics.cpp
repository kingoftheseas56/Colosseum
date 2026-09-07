#include "server1/policy/Value.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using server1::policy::Settings;
using server1::policy::Value;
using server1::policy::bytesToString;
using server1::policy::checkedSize;
using server1::policy::finiteNumber;
using server1::policy::hasOwnProperty;
using server1::policy::isPositiveInteger;
using server1::policy::isSafePositiveInteger;
using server1::policy::jsonStringify;
using server1::policy::jsNumber;
using server1::policy::jsParseInt;
using server1::policy::jsToInt32;
using server1::policy::jsToUint32;
using server1::policy::jsTruthy;
using server1::policy::shallowExtend;
using server1::policy::stringToBytes;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void requireNear(double actual, double expected, std::string_view message)
{
    require(std::isnan(actual) ? std::isnan(expected) : actual == expected, message);
}

const Value *property(const Value &object, std::string_view key)
{
    return object.find(key);
}

void caseK00_01()
{
    requireNear(jsNumber(Value::missing()), std::numeric_limits<double>::quiet_NaN(),
                "Number(undefined) is NaN");
    requireNear(jsNumber(Value::null()), 0.0, "Number(null) is zero");
    requireNear(jsNumber(Value::boolean(false)), 0.0, "Number(false) is zero");
    requireNear(jsNumber(Value::boolean(true)), 1.0, "Number(true) is one");
    requireNear(jsNumber(Value::number(0.0)), 0.0, "Number(0) is zero");
    requireNear(jsNumber(Value::string("")), 0.0, "Number(empty string) is zero");
    requireNear(jsNumber(Value::string("  42.5  ")), 42.5, "Number(number string)");
    requireNear(jsNumber(Value::string("NaN-like")), std::numeric_limits<double>::quiet_NaN(),
                "Number(malformed string) is NaN");

    requireNear(jsParseInt(Value::string("12trailing")), 12.0,
                "parseInt stops at trailing text");
    require(std::isnan(jsParseInt(Value::string("NaN-like"))),
            "parseInt(malformed string) is NaN");

    const Value priorityZero = Value::number(jsParseInt(Value::string("0")));
    const Value priorityTrue = Value::number(jsParseInt(Value::string("2")));
    const Value priorityMalformed = Value::number(jsParseInt(Value::string("false")));
    require(jsTruthy(priorityZero) == false, "zero priority follows JavaScript falsiness");
    require(jsTruthy(priorityTrue), "positive priority is truthy");
    require(jsTruthy(priorityMalformed) == false, "NaN priority is falsy");
    require(!jsTruthy(Value::missing()), "missing is falsy");
    require(!jsTruthy(Value::null()), "null is falsy");
    require(!jsTruthy(Value::boolean(false)), "false is falsy");
    require(!jsTruthy(Value::string("")), "empty string is falsy");
    require(jsTruthy(Value::string("0")), "non-empty string is truthy");

    const Value priorityOption = Value::object({{"priority", Value::boolean(false)}});
    require(hasOwnProperty(priorityOption, "priority"),
            "option presence distinguishes false from omitted");
    require(!hasOwnProperty(Value::object({}), "priority"), "omitted option is absent");

    const Value wire = Value::object({
        {"missing", Value::missing()},
        {"null", Value::null()},
        {"false", Value::boolean(false)},
        {"zero", Value::number(0.0)},
        {"empty", Value::string("")},
        {"nan", Value::number(std::numeric_limits<double>::quiet_NaN())},
        {"array", Value::array({Value::missing(), Value::null()})},
    });
    require(jsonStringify(wire)
                == R"({"null":null,"false":false,"zero":0,"empty":"","nan":null,"array":[null,null]})",
            "JSON omission and non-finite number rules");

    std::cout << "K00-01 PASS\n";
}

void caseK00_02()
{
    const Value base = Value::object({
        {"serverVersion", Value::string("4.21.0")},
        {"known", Value::number(1.0)},
    });
    const Value extension = Value::object({
        {"unknown", Value::string("retained")},
        {"known", Value::number(2.0)},
        {"serverVersion", Value::string("9.9.9")},
    });

    const Value extended = shallowExtend(base, extension);
    require(hasOwnProperty(extended, "unknown"), "unknown shallow-extension key survives");
    require(property(extended, "known") && property(extended, "known")->asNumber() == 2.0,
            "known shallow-extension key is replaced");
    require(property(extended, "serverVersion")
                && property(extended, "serverVersion")->asString() == "9.9.9",
            "plain object extension remains plain-object compatible");
    require(jsonStringify(shallowExtend(base, Value::missing())) == jsonStringify(base),
            "omitted shallow extension is a no-op");

    Settings settings("4.21.0", base);
    settings.extend(extension);
    require(settings.serverVersion() == "4.21.0", "settings serverVersion getter is stable");
    require(!settings.set("serverVersion", Value::string("9.9.9")),
            "settings serverVersion setter is readonly");
    require(settings.serverVersion() == "4.21.0", "readonly setter does not mutate version");
    require(settings.find("unknown") && settings.find("unknown")->asString() == "retained",
            "settings keeps unknown extension key");

    const Value ordered = settings.value();
    require(jsonStringify(ordered)
                == R"({"serverVersion":"4.21.0","known":2,"unknown":"retained"})",
            "ordered settings values preserve insertion order");

    std::cout << "K00-02 PASS\n";
}

void caseK00_03()
{
    requireNear(jsNumber(Value::string("1e309")), std::numeric_limits<double>::infinity(),
                "overflowing number text remains infinity before boundary validation");
    require(!finiteNumber(Value::number(std::numeric_limits<double>::quiet_NaN())),
            "NaN is rejected by finite-number handling");
    require(!finiteNumber(Value::number(std::numeric_limits<double>::infinity())),
            "infinity is rejected by finite-number handling");

    require(jsToInt32(Value::number(4294967297.0)) == 1,
            "bitwise coercion wraps through signed 32-bit space");
    require(jsToInt32(Value::number(-1.9)) == -1,
            "bitwise coercion truncates toward zero");
    require(jsToInt32(Value::missing()) == 0, "missing bitwise operand becomes zero");
    require(jsToUint32(Value::number(-1.0)) == std::numeric_limits<std::uint32_t>::max(),
            "unsigned bitwise coercion wraps negative one");

    const auto utf8 = stringToBytes("hé");
    require(bytesToString(utf8) == "hé", "byte/string conversion preserves UTF-8");

    require(checkedSize(Value::number(16.0), 1024) == 16,
            "valid native size crosses boundary explicitly");
    require(!checkedSize(Value::number(-1.0), 1024), "negative native size is rejected");
    require(!checkedSize(Value::number(std::numeric_limits<double>::quiet_NaN()), 1024),
            "NaN native size is rejected");
    require(!checkedSize(Value::number(std::numeric_limits<double>::infinity()), 1024),
            "infinite native size is rejected");
    require(!checkedSize(Value::string("18446744073709551615"), 1024),
            "oversized native size is rejected without overflow");
    require(!checkedSize(Value::string("16"), 1024),
            "string native size is rejected until explicitly converted");
    require(isPositiveInteger(Value::number(55.0)), "positive integer recognizes number values");
    require(!isPositiveInteger(Value::string("55")), "positive integer preserves JS number typing");
    require(isSafePositiveInteger(Value::number(55.0)), "safe positive integer accepts bounded number");
    require(!isSafePositiveInteger(Value::number(9007199254740992.0)),
            "safe positive integer rejects values above the JS safe integer limit");

    std::cout << "K00-03 PASS\n";
}

std::string traceNumber(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value < 0.0 ? "-Infinity" : "Infinity";
    std::ostringstream stream;
    stream << std::setprecision(15) << std::defaultfloat << value;
    return stream.str();
}

void emitTrace()
{
    std::cout << "K00-01 Number(missing)=" << traceNumber(jsNumber(Value::missing())) << '\n';
    std::cout << "K00-01 Number(null)=" << traceNumber(jsNumber(Value::null())) << '\n';
    std::cout << "K00-01 Number(false)=" << traceNumber(jsNumber(Value::boolean(false))) << '\n';
    std::cout << "K00-01 Number(0)=" << traceNumber(jsNumber(Value::number(0.0))) << '\n';
    std::cout << "K00-01 Number(empty)=" << traceNumber(jsNumber(Value::string(""))) << '\n';
    std::cout << "K00-01 Number(number-string)=" << traceNumber(jsNumber(Value::string("42.5")))
              << '\n';
    std::cout << "K00-01 Number(NaN-like)="
              << traceNumber(jsNumber(Value::string("NaN-like"))) << '\n';
    std::cout << "K00-01 parseInt(trailing)=" << traceNumber(jsParseInt(Value::string("12trailing")))
              << '\n';
    std::cout << "K00-01 priority(0)||1="
              << (jsTruthy(Value::number(jsParseInt(Value::string("0"))))
                     ? traceNumber(jsParseInt(Value::string("0")))
                     : "1")
              << '\n';
    std::cout << "K00-01 priority(2)||1="
              << (jsTruthy(Value::number(jsParseInt(Value::string("2"))))
                     ? traceNumber(jsParseInt(Value::string("2")))
                     : "1")
              << '\n';
    std::cout << "K00-01 priority(false)||1="
              << (jsTruthy(Value::number(jsParseInt(Value::string("false"))))
                     ? traceNumber(jsParseInt(Value::string("false")))
                     : "1")
              << '\n';
    std::cout << "K00-01 option-priority-false-present="
              << (hasOwnProperty(Value::object({{"priority", Value::boolean(false)}}), "priority")
                      ? "true"
                      : "false")
              << '\n';
    std::cout << "K00-01 option-priority-absent="
              << (hasOwnProperty(Value::object({}), "priority") ? "true" : "false") << '\n';
    const Value wire = Value::object({
        {"missing", Value::missing()},
        {"null", Value::null()},
        {"false", Value::boolean(false)},
        {"zero", Value::number(0.0)},
        {"empty", Value::string("")},
        {"nan", Value::number(std::numeric_limits<double>::quiet_NaN())},
        {"array", Value::array({Value::missing(), Value::null()})},
    });
    std::cout << "K00-01 JSON=" << jsonStringify(wire) << '\n';

    const Value base = Value::object({
        {"serverVersion", Value::string("4.21.0")},
        {"known", Value::number(1.0)},
    });
    const Value extension = Value::object({
        {"unknown", Value::string("retained")},
        {"known", Value::number(2.0)},
        {"serverVersion", Value::string("9.9.9")},
    });
    Settings settings("4.21.0", base);
    settings.extend(extension);
    std::cout << "K00-02 unknown=" << settings.find("unknown")->asString() << '\n';
    std::cout << "K00-02 serverVersion=" << settings.serverVersion() << '\n';
    std::cout << "K00-02 JSON=" << jsonStringify(settings.value()) << '\n';

    std::cout << "K00-03 Number(1e309)="
              << traceNumber(jsNumber(Value::string("1e309"))) << '\n';
    std::cout << "K00-03 bitwise(4294967297|0)=" << jsToInt32(Value::number(4294967297.0)) << '\n';
    std::cout << "K00-03 bitwise(-1.9|0)=" << jsToInt32(Value::number(-1.9)) << '\n';
    std::cout << "K00-03 bitwise(undefined|0)=" << jsToInt32(Value::missing()) << '\n';
    std::cout << "K00-03 bitwise(-1>>>0)=" << jsToUint32(Value::number(-1.0)) << '\n';
    std::cout << "K00-03 utf8=" << bytesToString(stringToBytes("hé")) << '\n';
    std::cout << "K00-03 size(16)=" << *checkedSize(Value::number(16.0), 1024) << '\n';
    std::cout << "K00-03 size(huge)="
              << (checkedSize(Value::string("18446744073709551615"), 1024) ? "accepted" : "rejected")
              << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::string requested = argc > 1 ? argv[1] : "all";
        if (requested == "--trace") {
            emitTrace();
            return 0;
        }
        if (requested == "all" || requested == "K00-01")
            caseK00_01();
        if (requested == "all" || requested == "K00-02")
            caseK00_02();
        if (requested == "all" || requested == "K00-03")
            caseK00_03();
        if (requested != "all" && requested != "K00-01" && requested != "K00-02"
            && requested != "K00-03")
            throw std::runtime_error("unknown K00 case");
    } catch (const std::exception &error) {
        std::cerr << "K00 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
