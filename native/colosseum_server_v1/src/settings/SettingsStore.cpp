#include "server1/settings/SettingsStore.h"

#include "server1/policy/Value.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace server1::settings {
namespace {

using server1::policy::Value;

std::string settingsFileFor(std::string_view appPath, std::string_view settingsPathOverride)
{
    const std::string_view root = settingsPathOverride.empty() ? appPath : settingsPathOverride;
    return (std::filesystem::path(std::string(root)) / "server-settings.json").string();
}

[[noreturn]] void parseError(std::size_t position, std::string_view message)
{
    throw std::runtime_error(std::string(message) + " at byte " + std::to_string(position));
}

void appendCodePoint(std::string &output, std::uint32_t codePoint)
{
    if (codePoint <= 0x7f) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else {
        throw std::runtime_error("JSON string contains an invalid Unicode code point");
    }
}

class JsonParser final {
public:
    explicit JsonParser(std::string_view input)
        : input_(input)
    {
    }

    Value parse()
    {
        skipWhitespace();
        Value result = parseValue();
        skipWhitespace();
        if (position_ != input_.size())
            parseError(position_, "trailing JSON data");
        return result;
    }

private:
    void skipWhitespace()
    {
        while (position_ < input_.size()
               && std::isspace(static_cast<unsigned char>(input_[position_])))
            ++position_;
    }

    char take()
    {
        if (position_ >= input_.size())
            parseError(position_, "unexpected end of JSON");
        return input_[position_++];
    }

    void expect(char expected)
    {
        if (take() != expected)
            parseError(position_ - 1, "unexpected JSON token");
    }

    Value parseValue()
    {
        skipWhitespace();
        if (position_ >= input_.size())
            parseError(position_, "missing JSON value");

        switch (input_[position_]) {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"':
            return Value::string(parseString());
        case 't':
            parseLiteral("true");
            return Value::boolean(true);
        case 'f':
            parseLiteral("false");
            return Value::boolean(false);
        case 'n':
            parseLiteral("null");
            return Value::null();
        default:
            if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_])))
                return parseNumber();
            parseError(position_, "invalid JSON value");
        }
    }

    void parseLiteral(std::string_view literal)
    {
        if (input_.substr(position_, literal.size()) != literal)
            parseError(position_, "invalid JSON literal");
        position_ += literal.size();
    }

    Value parseObject()
    {
        expect('{');
        Value::Object object;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return Value::object(std::move(object));
        }

        while (true) {
            skipWhitespace();
            if (position_ >= input_.size() || input_[position_] != '"')
                parseError(position_, "JSON object key must be a string");
            const std::string key = parseString();
            skipWhitespace();
            expect(':');
            Value value = parseValue();

            bool replaced = false;
            for (auto &entry : object) {
                if (entry.first == key) {
                    entry.second = std::move(value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                object.emplace_back(key, std::move(value));

            skipWhitespace();
            const char delimiter = take();
            if (delimiter == '}')
                break;
            if (delimiter != ',')
                parseError(position_ - 1, "expected JSON object delimiter");
        }
        return Value::object(std::move(object));
    }

    Value parseArray()
    {
        expect('[');
        Value::Array array;
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return Value::array(std::move(array));
        }

        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            const char delimiter = take();
            if (delimiter == ']')
                break;
            if (delimiter != ',')
                parseError(position_ - 1, "expected JSON array delimiter");
        }
        return Value::array(std::move(array));
    }

    std::uint32_t parseHexQuad()
    {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char digit = take();
            value <<= 4;
            if (digit >= '0' && digit <= '9')
                value |= static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            else
                parseError(position_ - 1, "invalid JSON Unicode escape");
        }
        return value;
    }

    std::string parseString()
    {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(take());
            if (character == '"')
                return result;
            if (character < 0x20)
                parseError(position_ - 1, "unescaped JSON control character");
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }

            const char escape = take();
            switch (escape) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                const std::uint32_t high = parseHexQuad();
                if (high >= 0xd800 && high <= 0xdbff) {
                    if (take() != '\\' || take() != 'u')
                        parseError(position_, "unpaired JSON high surrogate");
                    const std::uint32_t low = parseHexQuad();
                    if (low < 0xdc00 || low > 0xdfff)
                        parseError(position_, "invalid JSON surrogate pair");
                    appendCodePoint(result, 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00));
                } else if (high >= 0xdc00 && high <= 0xdfff) {
                    parseError(position_, "unpaired JSON low surrogate");
                } else {
                    appendCodePoint(result, high);
                }
                break;
            }
            default:
                parseError(position_ - 1, "invalid JSON escape");
            }
        }
        parseError(position_, "unterminated JSON string");
    }

    Value parseNumber()
    {
        const std::size_t start = position_;
        if (input_[position_] == '-')
            ++position_;

        if (position_ >= input_.size())
            parseError(position_, "incomplete JSON number");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])))
                parseError(position_, "leading zero in JSON number");
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_])))
                parseError(position_, "invalid JSON number");
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
        }

        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
            if (position_ == fractionStart)
                parseError(position_, "missing JSON fraction digits");
        }

        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
            if (position_ == exponentStart)
                parseError(position_, "missing JSON exponent digits");
        }

        const std::string token(input_.substr(start, position_ - start));
        char *end = nullptr;
        errno = 0;
        const double number = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0')
            parseError(start, "invalid JSON number");
        return Value::number(number);
    }

    std::string_view input_;
    std::size_t position_ {0};
};

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

std::string numberToJson(double value)
{
    if (!std::isfinite(value))
        return "null";
    if (value == 0.0)
        return "0";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

void appendJson(std::string &output, const Value &value, std::size_t depth, bool missingAsNull)
{
    const auto indent = [](std::string &target, std::size_t count) { target.append(count * 4, ' '); };

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
        output += numberToJson(value.asNumber());
        return;
    case Value::Kind::String:
        appendJsonEscaped(output, value.asString());
        return;
    case Value::Kind::Bytes:
        output += "null";
        return;
    case Value::Kind::Array: {
        const auto &array = value.asArray();
        if (array.empty()) {
            output += "[]";
            return;
        }
        output += "[\n";
        for (std::size_t index = 0; index < array.size(); ++index) {
            if (index != 0)
                output += ",\n";
            indent(output, depth + 1);
            appendJson(output, array[index], depth + 1, true);
        }
        output += "\n";
        indent(output, depth);
        output.push_back(']');
        return;
    }
    case Value::Kind::Object: {
        const auto &object = value.asObject();
        std::size_t emitted = 0;
        for (const auto &entry : object) {
            if (!entry.second.isMissing())
                ++emitted;
        }
        if (emitted == 0) {
            output += "{}";
            return;
        }
        output += "{\n";
        std::size_t emittedIndex = 0;
        for (const auto &entry : object) {
            if (entry.second.isMissing())
                continue;
            if (emittedIndex++ != 0)
                output += ",\n";
            indent(output, depth + 1);
            appendJsonEscaped(output, entry.first);
            output += ": ";
            appendJson(output, entry.second, depth + 1, false);
        }
        output += "\n";
        indent(output, depth);
        output.push_back('}');
        return;
    }
    }
}

void logSettingsError(std::string_view path, std::string_view message)
{
    std::cerr << "Cannot update settings " << path << ": " << message << '\n';
}

void setNumberIfInvalid(policy::Settings &settings,
                        std::string_view key,
                        double fallback)
{
    const Value *current = settings.find(key);
    if (!current || !server1::policy::isPositiveInteger(*current))
        (void)settings.set(key, Value::number(fallback));
}

} // namespace

SettingsStore::SettingsStore(SettingsStoreConfig config)
    : appPath_(config.appPath)
    , settingsPath_(settingsFileFor(config.appPath, config.settingsPathOverride))
    , serverVersion_(config.serverVersion)
    , android_(config.android)
    , disableCaching_(config.disableCaching)
    , settings_(serverVersion_, Value::object(Value::Object {}))
{
    (void)settings_.set("appPath", Value::string(appPath_));
    if (!settings_.find("cacheRoot"))
        (void)settings_.set("cacheRoot", Value::string(appPath_));

    if (disableCaching_)
        (void)settings_.set("cacheSize", Value::number(0.0));
    else if (!settings_.find("cacheSize"))
        (void)settings_.set("cacheSize", Value::number(android_ ? 0.0 : 2147483648.0));

    setNumberIfInvalid(settings_, "btMaxConnections", 55.0);
    setNumberIfInvalid(settings_, "btHandshakeTimeout", 20000.0);

    const Value requestTimeout = settings_.find("btRequestTimeout")
                                     ? *settings_.find("btRequestTimeout")
                                     : Value::missing();
    const Value connectionTimeout = settings_.find("btConnectionTimeout")
                                        ? *settings_.find("btConnectionTimeout")
                                        : Value::missing();
    const Value timeoutCandidate = server1::policy::jsTruthy(requestTimeout)
                                       ? requestTimeout
                                       : connectionTimeout;
    if (server1::policy::isPositiveInteger(timeoutCandidate))
        (void)settings_.set("btRequestTimeout", requestTimeout);
    else
        (void)settings_.set("btRequestTimeout", Value::number(4000.0));

    setNumberIfInvalid(settings_, "btDownloadSpeedSoftLimit", 2621440.0);
    setNumberIfInvalid(settings_, "btDownloadSpeedHardLimit", 3670016.0);
    setNumberIfInvalid(settings_, "btMinPeersForStable", 5.0);

    if (const Value *remoteHttps = settings_.find("remoteHttps");
        !remoteHttps || remoteHttps->kind() != Value::Kind::String)
        (void)settings_.set("remoteHttps", Value::string(""));

    if (!settings_.find("localAddonEnabled"))
        (void)settings_.set("localAddonEnabled", Value::boolean(false));
    if (const Value *horsepower = settings_.find("transcodeHorsepower");
        !horsepower || !server1::policy::jsTruthy(*horsepower))
        (void)settings_.set("transcodeHorsepower", Value::number(.75));

    setNumberIfInvalid(settings_, "transcodeMaxBitRate", 0.0);
    setNumberIfInvalid(settings_, "transcodeConcurrency", 1.0);
    setNumberIfInvalid(settings_, "transcodeTrackConcurrency", 1.0);

    if (!settings_.find("transcodeHardwareAccel"))
        (void)settings_.set("transcodeHardwareAccel", Value::boolean(true));
    if (!settings_.find("transcodeProfile"))
        (void)settings_.set("transcodeProfile", Value::null());
    (void)settings_.set("allTranscodeProfiles", Value::array(Value::Array {}));
    if (!settings_.find("transcodeMaxWidth"))
        (void)settings_.set("transcodeMaxWidth", Value::number(1920.0));
    if (!settings_.find("proxyStreamsEnabled"))
        (void)settings_.set("proxyStreamsEnabled", Value::boolean(false));

    (void)load();
}

void SettingsStore::extend(const Value &extension)
{
    settings_.extend(extension);
}

bool SettingsStore::set(std::string_view key, Value value)
{
    return settings_.set(key, std::move(value));
}

bool SettingsStore::load()
{
    std::ifstream input(settingsPath_, std::ios::binary);
    if (!input) {
        const int error = errno;
        logSettingsError(settingsPath_, error != 0 ? std::strerror(error) : "file is unavailable");
        return false;
    }

    const std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    try {
        const Value loaded = JsonParser(text).parse();
        if (loaded.kind() != Value::Kind::Object)
            throw std::logic_error("settings JSON root must be an object");
        settings_.extend(loaded);
        (void)save();
        return true;
    } catch (const std::exception &error) {
        logSettingsError(settingsPath_, error.what());
        return false;
    }
}

bool SettingsStore::save() const
{
    std::ofstream output(settingsPath_, std::ios::binary | std::ios::trunc);
    if (!output) {
        const int error = errno;
        logSettingsError(settingsPath_, error != 0 ? std::strerror(error) : "file is not writable");
        return false;
    }

    output << serialized();
    if (!output) {
        logSettingsError(settingsPath_, "write failed");
        return false;
    }
    return true;
}

Value SettingsStore::value() const
{
    return settings_.value();
}

const Value *SettingsStore::find(std::string_view key) const noexcept
{
    return settings_.find(key);
}

const std::string &SettingsStore::appPath() const noexcept
{
    return appPath_;
}

const std::string &SettingsStore::settingsPath() const noexcept
{
    return settingsPath_;
}

const std::string &SettingsStore::serverVersion() const noexcept
{
    return serverVersion_;
}

std::string SettingsStore::serialized() const
{
    std::string output;
    appendJson(output, settings_.value(), 0, false);
    return output;
}

} // namespace server1::settings
