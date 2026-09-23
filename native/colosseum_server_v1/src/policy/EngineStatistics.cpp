#include "server1/policy/EngineLifecycle.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace server1::policy {
namespace {

// ToString(ToPropertyKey(idx)) names an element of an n-element array.
bool canonicalArrayIndex(const Value &idx, std::size_t size, std::size_t &out)
{
    std::string key;
    if (idx.kind() == Value::Kind::String)
        key = idx.asString();
    else if (idx.kind() == Value::Kind::Number)
        key = jsNumberToString(idx.asNumber());
    else
        return false;
    if (key.empty() || key.size() > 10 || (key.size() > 1 && key[0] == '0'))
        return false;
    std::uint64_t value = 0;
    for (const char c : key) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value >= size)
        return false;
    out = static_cast<std::size_t>(value);
    return true;
}

void appendQuoted(std::string &out, const std::string &text)
{
    static const char *hex = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

void appendJson(std::string &out, const Value &value)
{
    switch (value.kind()) {
    case Value::Kind::Missing:
    case Value::Kind::Null:
    case Value::Kind::Bytes:
        out += "null";
        return;
    case Value::Kind::Boolean:
        out += value.asBoolean() ? "true" : "false";
        return;
    case Value::Kind::Number:
        out += std::isfinite(value.asNumber()) ? jsNumberToString(value.asNumber()) : "null";
        return;
    case Value::Kind::String:
        appendQuoted(out, value.asString());
        return;
    case Value::Kind::Array: {
        out.push_back('[');
        bool first = true;
        for (const auto &element : value.asArray()) {
            if (!first)
                out.push_back(',');
            first = false;
            appendJson(out, element);
        }
        out.push_back(']');
        return;
    }
    case Value::Kind::Object: {
        out.push_back('{');
        bool first = true;
        for (const auto &[key, element] : value.asObject()) {
            if (element.isMissing())
                continue;
            if (!first)
                out.push_back(',');
            first = false;
            appendQuoted(out, key);
            out.push_back(':');
            appendJson(out, element);
        }
        out.push_back('}');
        return;
    }
    }
}

Value statisticsValue(const EngineStatsSnapshot &s, const Value *idx, const Value::Object &extras)
{
    std::size_t unchoked = 0;
    Value::Array wires;
    for (const auto &wire : s.wires) {
        if (wire.peerChoking)
            continue;
        ++unchoked;
        wires.push_back(Value::object({
            {"requests", Value::number(static_cast<double>(wire.requests))},
            {"address", wire.address},
            {"amInterested", Value::boolean(wire.amInterested)},
            {"isSeeder", Value::boolean(wire.isSeeder)},
            {"downSpeed", Value::number(wire.downSpeed)},
            {"upSpeed", Value::number(wire.upSpeed)},
        }));
    }
    Value::Object object{
        {"infoHash", Value::string(s.infoHash)},
        // e.torrent && e.torrent.name: null without a torrent.
        {"name", s.hasTorrent ? s.torrentName : Value::null()},
        {"peers", Value::number(static_cast<double>(s.wires.size()))},
        {"unchoked", Value::number(static_cast<double>(unchoked))},
        {"queued", s.queued},
        {"unique", Value::number(static_cast<double>(s.uniquePeers))},
        {"connectionTries", s.connectionTries},
        {"swarmPaused", Value::boolean(s.swarmPaused)},
        {"swarmConnections", Value::number(static_cast<double>(s.swarmConnections))},
        {"swarmSize", s.swarmSize},
        {"selections", s.selections},
        {"wires", idx ? Value::null() : Value::array(std::move(wires))},
        {"files", s.hasTorrent ? s.torrentFiles : Value::null()},
        {"downloaded", s.downloaded},
        {"uploaded", s.uploaded},
        {"downloadSpeed", Value::number(s.swarmDownloadSpeed)},
        // M172 L18327: uploadSpeed is assigned e.swarm.downloadSpeed().
        {"uploadSpeed", Value::number(s.swarmDownloadSpeed)},
        {"sources", s.hasPeerSearch ? s.peerSearchStats : Value::missing()},
        {"peerSearchRunning", s.hasPeerSearch ? Value::boolean(s.peerSearchRunning) : Value::missing()},
        {"opts", s.options},
    };
    std::size_t fileIndex = 0;
    if (idx && !std::isnan(jsNumber(*idx)) && s.hasTorrent
        && canonicalArrayIndex(*idx, s.fileGeometry.size(), fileIndex)) {
        const auto &file = s.fileGeometry[fileIndex];
        const double pieceLength = static_cast<double>(s.pieceLength);
        const double offset = static_cast<double>(file.offset);
        const double length = static_cast<double>(file.length);
        const auto start = jsToInt32(Value::number(offset / pieceLength));
        const auto end = jsToInt32(Value::number((offset + length - 1) / pieceLength));
        double available = 0.0;
        for (std::int64_t piece = start; piece <= end; ++piece)
            if (piece >= 0 && static_cast<std::size_t>(piece) < s.have.size()
                && s.have[static_cast<std::size_t>(piece)])
                available += 1.0;
        const double filePieces = std::ceil(length / pieceLength);
        // util._extend copies {streamLen, streamName, streamProgress} last key
        // first.
        object.emplace_back("streamProgress", Value::number(available / filePieces));
        object.emplace_back("streamName", Value::string(file.name));
        object.emplace_back("streamLen", Value::number(length));
    }
    for (const auto &extra : extras)
        object.push_back(extra);
    return Value::object(std::move(object));
}

} // namespace

std::string jsNumberToString(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (value == 0.0)
        return "0";
    if (std::isinf(value))
        return value < 0 ? "-Infinity" : "Infinity";
    if (value < 0)
        return "-" + jsNumberToString(-value);
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::scientific);
    std::string text(buffer, result.ptr);
    const auto e = text.find('e');
    std::string digits = text.substr(0, e);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int exponent = std::atoi(text.c_str() + e + 1);
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;
    if (k <= n && n <= 21)
        return digits + std::string(static_cast<std::size_t>(n - k), '0');
    if (0 < n && n <= 21)
        return digits.substr(0, static_cast<std::size_t>(n)) + "."
            + digits.substr(static_cast<std::size_t>(n));
    if (-6 < n && n <= 0)
        return "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    std::string out(1, digits[0]);
    if (k > 1)
        out += "." + digits.substr(1);
    const int shown = n - 1;
    out += shown >= 0 ? "e+" : "e-";
    out += std::to_string(shown >= 0 ? shown : -shown);
    return out;
}

std::string jsJsonStringify(const Value &value)
{
    std::string out;
    appendJson(out, value);
    return out;
}

std::string serializeEngineStatistics(const EngineStatsSnapshot *snapshot, const Value *idx,
                                      const Value::Object &extras)
{
    if (!snapshot)
        return "null";
    return jsJsonStringify(statisticsValue(*snapshot, idx, extras));
}

std::string serializeAllEngineStatistics(
    const std::vector<std::pair<std::string, const EngineStatsSnapshot *>> &engines,
    const Value *sys)
{
    Value::Object object;
    if (sys)
        object.emplace_back("sys", *sys);
    for (const auto &[hash, snapshot] : engines)
        object.emplace_back(hash, snapshot ? statisticsValue(*snapshot, nullptr, {})
                                           : Value::null());
    return jsJsonStringify(Value::object(std::move(object)));
}

} // namespace server1::policy
