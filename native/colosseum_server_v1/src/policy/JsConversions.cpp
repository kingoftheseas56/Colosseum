#include "server1/policy/Value.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace server1::policy {
namespace {

constexpr double kUint32Modulo = 4294967296.0;
constexpr double kMaxSafeInteger = 9007199254740991.0;

std::string trimLeading(std::string_view value)
{
    std::size_t first = 0;
    while (first < value.size()
           && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    return std::string(value.substr(first));
}

std::string trimWhitespace(std::string_view value)
{
    std::size_t first = 0;
    while (first < value.size()
           && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    std::size_t last = value.size();
    while (last > first
           && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
    return std::string(value.substr(first, last - first));
}

std::string numberToString(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0.0)
        return "0";

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string valueToString(const Value &value)
{
    switch (value.kind()) {
    case Value::Kind::Missing:
        return "undefined";
    case Value::Kind::Null:
        return "null";
    case Value::Kind::Boolean:
        return value.asBoolean() ? "true" : "false";
    case Value::Kind::Number:
        return numberToString(value.asNumber());
    case Value::Kind::String:
        return value.asString();
    case Value::Kind::Bytes:
        return bytesToString(value.asBytes());
    case Value::Kind::Array:
    case Value::Kind::Object:
        throw std::logic_error("compound value cannot be stringified by this bounded helper");
    }
    return "undefined";
}

int digitValue(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'z')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'Z')
        return digit - 'A' + 10;
    return -1;
}

double parseUnsignedNumber(std::string_view digits, bool negative, int radix)
{
    long double result = 0.0L;
    for (const char digit : digits) {
        const int value = digitValue(digit);
        if (value < 0 || value >= radix)
            break;
        result = result * radix + value;
        if (result > std::numeric_limits<double>::max())
            return negative ? -std::numeric_limits<double>::infinity()
                            : std::numeric_limits<double>::infinity();
    }
    const double converted = static_cast<double>(result);
    return negative ? -converted : converted;
}

void appendJsonEscaped(std::string &output, std::string_view value)
{
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20) {
                std::ostringstream escaped;
                escaped.imbue(std::locale::classic());
                escaped << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                        << static_cast<unsigned int>(character);
                output += escaped.str();
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void appendJson(std::string &output, const Value &value, bool missingAsNull)
{
    switch (value.kind()) {
    case Value::Kind::Missing:
        if (missingAsNull)
            output += "null";
        return;
    case Value::Kind::Null:
        output += "null";
        return;
    case Value::Kind::Boolean:
        output += value.asBoolean() ? "true" : "false";
        return;
    case Value::Kind::Number:
        if (!std::isfinite(value.asNumber())) {
            output += "null";
        } else {
            output += numberToString(value.asNumber());
        }
        return;
    case Value::Kind::String:
        appendJsonEscaped(output, value.asString());
        return;
    case Value::Kind::Bytes:
        output += "null";
        return;
    case Value::Kind::Array: {
        output.push_back('[');
        bool first = true;
        for (const auto &element : value.asArray()) {
            if (!first)
                output.push_back(',');
            first = false;
            appendJson(output, element, true);
        }
        output.push_back(']');
        return;
    }
    case Value::Kind::Object: {
        output.push_back('{');
        bool first = true;
        for (const auto &entry : value.asObject()) {
            if (entry.second.isMissing())
                continue;
            if (!first)
                output.push_back(',');
            first = false;
            appendJsonEscaped(output, entry.first);
            output.push_back(':');
            appendJson(output, entry.second, false);
        }
        output.push_back('}');
        return;
    }
    }
}

} // namespace

double jsNumber(const Value &value)
{
    switch (value.kind()) {
    case Value::Kind::Missing:
        return std::numeric_limits<double>::quiet_NaN();
    case Value::Kind::Null:
        return 0.0;
    case Value::Kind::Boolean:
        return value.asBoolean() ? 1.0 : 0.0;
    case Value::Kind::Number:
        return value.asNumber();
    case Value::Kind::String: {
        const std::string text = trimWhitespace(value.asString());
        if (text.empty())
            return 0.0;

        char *end = nullptr;
        errno = 0;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end && *end == '\0')
            return parsed;

        int base = 0;
        std::size_t prefix = 0;
        if (text.size() > 2 && text[0] == '0'
            && (text[1] == 'x' || text[1] == 'X' || text[1] == 'b' || text[1] == 'B'
                || text[1] == 'o' || text[1] == 'O')) {
            base = text[1] == 'x' || text[1] == 'X' ? 16 : text[1] == 'b' || text[1] == 'B' ? 2 : 8;
            prefix = 2;
            unsigned long long result = 0;
            for (std::size_t i = prefix; i < text.size(); ++i) {
                const int digit = digitValue(text[i]);
                if (digit < 0 || digit >= base)
                    return std::numeric_limits<double>::quiet_NaN();
                result = result * static_cast<unsigned long long>(base)
                         + static_cast<unsigned long long>(digit);
            }
            return static_cast<double>(result);
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
    case Value::Kind::Bytes:
    case Value::Kind::Array:
    case Value::Kind::Object:
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double jsParseInt(const Value &value, int radix)
{
    std::string text = trimLeading(valueToString(value));
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.erase(text.begin());
    }

    if (radix != 0 && (radix < 2 || radix > 36))
        return std::numeric_limits<double>::quiet_NaN();
    if (radix == 0)
        radix = text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
    if (radix == 16 && text.size() > 1 && text[0] == '0'
        && (text[1] == 'x' || text[1] == 'X'))
        text.erase(0, 2);

    std::size_t count = 0;
    while (count < text.size()) {
        const int digit = digitValue(text[count]);
        if (digit < 0 || digit >= radix)
            break;
        ++count;
    }
    if (count == 0)
        return std::numeric_limits<double>::quiet_NaN();
    return parseUnsignedNumber(std::string_view(text).substr(0, count), negative, radix);
}

bool jsTruthy(const Value &value) noexcept
{
    switch (value.kind()) {
    case Value::Kind::Missing:
    case Value::Kind::Null:
        return false;
    case Value::Kind::Boolean:
        return value.asBoolean();
    case Value::Kind::Number:
        return value.asNumber() != 0.0 && !std::isnan(value.asNumber());
    case Value::Kind::String:
        return !value.asString().empty();
    case Value::Kind::Bytes:
    case Value::Kind::Array:
    case Value::Kind::Object:
        return true;
    }
    return false;
}

std::optional<double> finiteNumber(const Value &value)
{
    const double converted = jsNumber(value);
    if (!std::isfinite(converted))
        return std::nullopt;
    return converted;
}

std::uint32_t jsToUint32(const Value &value)
{
    const auto converted = finiteNumber(value);
    if (!converted || *converted == 0.0)
        return 0;

    double truncated = std::trunc(*converted);
    double modulo = std::fmod(truncated, kUint32Modulo);
    if (modulo < 0.0)
        modulo += kUint32Modulo;
    return static_cast<std::uint32_t>(modulo);
}

std::int32_t jsToInt32(const Value &value)
{
    const std::uint32_t converted = jsToUint32(value);
    if (converted >= 0x80000000U)
        return static_cast<std::int32_t>(static_cast<std::int64_t>(converted) - 0x100000000LL);
    return static_cast<std::int32_t>(converted);
}

std::vector<std::uint8_t> stringToBytes(std::string_view value)
{
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

std::string bytesToString(const std::vector<std::uint8_t> &value)
{
    return std::string(value.begin(), value.end());
}

bool hasOwnProperty(const Value &object, std::string_view key) noexcept
{
    return object.kind() == Value::Kind::Object && object.find(key) != nullptr;
}

Value shallowExtend(const Value &base, const Value &extension)
{
    if (base.kind() != Value::Kind::Object)
        throw std::logic_error("shallowExtend requires object values");
    if (extension.isMissing() || extension.isNull())
        return base;
    if (extension.kind() != Value::Kind::Object)
        throw std::logic_error("shallowExtend requires object values");

    Value::Object result = base.asObject();
    for (const auto &incoming : extension.asObject()) {
        bool replaced = false;
        for (auto &existing : result) {
            if (existing.first == incoming.first) {
                existing.second = incoming.second;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            result.push_back(incoming);
    }
    return Value::object(std::move(result));
}

std::string jsonStringify(const Value &value)
{
    std::string output;
    appendJson(output, value, false);
    return output;
}

bool isPositiveInteger(const Value &value)
{
    if (value.kind() != Value::Kind::Number)
        return false;
    const double number = value.asNumber();
    return std::isfinite(number) && number > 0.0 && std::trunc(number) == number;
}

bool isSafePositiveInteger(const Value &value)
{
    return isPositiveInteger(value) && value.asNumber() <= kMaxSafeInteger;
}

std::optional<std::size_t> checkedSize(const Value &value, std::size_t maximum)
{
    if (value.kind() != Value::Kind::Number)
        return std::nullopt;
    const auto converted = finiteNumber(value);
    if (!converted || *converted < 0.0 || std::trunc(*converted) != *converted)
        return std::nullopt;

    const long double size = static_cast<long double>(*converted);
    if (size > static_cast<long double>(maximum))
        return std::nullopt;
    return static_cast<std::size_t>(*converted);
}

} // namespace server1::policy
