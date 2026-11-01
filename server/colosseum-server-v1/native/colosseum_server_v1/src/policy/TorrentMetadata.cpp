#include "server1/policy/TorrentMetadata.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace server1::policy {
namespace {

class BencodeReader final {
public:
    explicit BencodeReader(const ByteBuffer &input)
        : input_(input)
    {
    }

    Value read()
    {
        if (input_.empty())
            throw std::runtime_error("empty bencode input");
        // M181 returns the first decoded value and does not require the input
        // cursor to reach the end of the buffer.
        return readValue();
    }

private:
    [[nodiscard]] bool atEnd() const noexcept
    {
        return position_ >= input_.size();
    }

    [[nodiscard]] std::uint8_t peek() const
    {
        if (atEnd())
            throw std::runtime_error("unexpected end of bencode input");
        return input_[position_];
    }

    Value readValue()
    {
        switch (peek()) {
        case 'i':
            return Value::number(readInteger());
        case 'l':
            return readList();
        case 'd':
            return readDictionary();
        default:
            if (peek() >= '0' && peek() <= '9')
                return Value::bytes(readBytes());
            throw std::runtime_error("invalid bencode token");
        }
    }

    double readInteger()
    {
        ++position_; // i
        if (atEnd())
            throw std::runtime_error("unterminated bencode integer");

        const bool negative = input_[position_] == '-';
        const bool signedPrefix = negative || input_[position_] == '+';
        if (signedPrefix)
            ++position_;
        const std::size_t firstDigit = position_;
        while (!atEnd() && input_[position_] >= '0' && input_[position_] <= '9')
            ++position_;
        if ((!signedPrefix && firstDigit == position_) || atEnd() || input_[position_] != 'e')
            throw std::runtime_error("invalid bencode integer");

        long double result = 0.0L;
        for (std::size_t index = firstDigit; index < position_; ++index) {
            result = result * 10.0L + static_cast<long double>(input_[index] - '0');
            if (result > static_cast<long double>(std::numeric_limits<double>::max()))
                throw std::runtime_error("bencode integer is outside native number range");
        }
        ++position_; // e
        const double converted = static_cast<double>(result);
        return negative ? -converted : converted;
    }

    ByteBuffer readBytes()
    {
        const bool negative = input_[position_] == '-';
        const bool signedPrefix = negative || input_[position_] == '+';
        if (signedPrefix)
            ++position_;
        const std::size_t firstDigit = position_;
        while (!atEnd() && input_[position_] >= '0' && input_[position_] <= '9')
            ++position_;
        if ((!signedPrefix && firstDigit == position_) || atEnd() || input_[position_] != ':')
            throw std::runtime_error("invalid bencode byte-string length");

        std::uint64_t magnitude = 0;
        for (std::size_t index = firstDigit; index < position_; ++index) {
            const std::size_t digit = input_[index] - '0';
            if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
                throw std::runtime_error("bencode byte-string is too large");
            magnitude = magnitude * 10 + digit;
        }
        if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            throw std::runtime_error("bencode byte-string is too large");
        ++position_; // colon
        const std::size_t start = position_;
        if (negative) {
            const std::size_t distance = static_cast<std::size_t>(magnitude);
            position_ = distance > start ? 0 : start - distance;
            return {};
        }
        const std::size_t length = static_cast<std::size_t>(magnitude);
        if (length > input_.size() - position_)
            throw std::runtime_error("bencode byte-string exceeds input");
        ByteBuffer result(input_.begin() + static_cast<std::ptrdiff_t>(position_),
                          input_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
        position_ += length;
        return result;
    }

    Value readList()
    {
        ++position_; // l
        Value::Array result;
        while (true) {
            if (atEnd())
                throw std::runtime_error("unterminated bencode list");
            if (input_[position_] == 'e') {
                ++position_;
                return Value::array(std::move(result));
            }
            result.push_back(readValue());
        }
    }

    Value readDictionary()
    {
        ++position_; // d
        Value::Object result;
        while (true) {
            if (atEnd())
                throw std::runtime_error("unterminated bencode dictionary");
            if (input_[position_] == 'e') {
                ++position_;
                return Value::object(std::move(result));
            }

            const ByteBuffer keyBytes = readBytes();
            const std::string key(keyBytes.begin(), keyBytes.end());
            Value value = readValue();
            bool replaced = false;
            for (auto &entry : result) {
                if (entry.first == key) {
                    entry.second = std::move(value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                result.emplace_back(key, std::move(value));
        }
    }

    const ByteBuffer &input_;
    std::size_t position_ = 0;
};

void appendBencode(ByteBuffer &output, const Value &value);

void appendByteString(ByteBuffer &output, const ByteBuffer &bytes)
{
    const std::string length = std::to_string(bytes.size());
    output.insert(output.end(), length.begin(), length.end());
    output.push_back(':');
    output.insert(output.end(), bytes.begin(), bytes.end());
}

std::string integerText(double value)
{
    if (!std::isfinite(value) || std::trunc(value) != value)
        throw std::runtime_error("bencode value is not an integer");
    const long double converted = static_cast<long double>(value);
    if (converted < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || converted > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::runtime_error("bencode integer is outside native range");

    if (converted < 0.0L)
        return std::to_string(static_cast<std::int64_t>(value));
    return std::to_string(static_cast<std::uint64_t>(value));
}

void appendBencode(ByteBuffer &output, const Value &value)
{
    switch (value.kind()) {
    case Value::Kind::Bytes:
        appendByteString(output, value.asBytes());
        return;
    case Value::Kind::String: {
        const ByteBuffer bytes(value.asString().begin(), value.asString().end());
        appendByteString(output, bytes);
        return;
    }
    case Value::Kind::Number: {
        const std::string text = integerText(value.asNumber());
        output.push_back('i');
        output.insert(output.end(), text.begin(), text.end());
        output.push_back('e');
        return;
    }
    case Value::Kind::Array:
        output.push_back('l');
        for (const auto &element : value.asArray())
            appendBencode(output, element);
        output.push_back('e');
        return;
    case Value::Kind::Object: {
        std::vector<const Value::ObjectEntry *> entries;
        entries.reserve(value.asObject().size());
        for (const auto &entry : value.asObject())
            entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(), [](const auto *left, const auto *right) {
            return left->first < right->first;
        });
        output.push_back('d');
        for (const auto *entry : entries) {
            const ByteBuffer key(entry->first.begin(), entry->first.end());
            appendByteString(output, key);
            appendBencode(output, entry->second);
        }
        output.push_back('e');
        return;
    }
    case Value::Kind::Missing:
        throw std::runtime_error("missing value cannot be bencoded");
    case Value::Kind::Null:
    case Value::Kind::Boolean:
        throw std::runtime_error("null or boolean value cannot be bencoded");
    }
    throw std::runtime_error("unknown value kind cannot be bencoded");
}

ByteBuffer encodeBencode(const Value &value)
{
    ByteBuffer output;
    appendBencode(output, value);
    return output;
}

std::string valueText(const Value &value)
{
    switch (value.kind()) {
    case Value::Kind::Bytes:
        return bytesToString(value.asBytes());
    case Value::Kind::String:
        return value.asString();
    case Value::Kind::Number:
        return integerText(value.asNumber());
    case Value::Kind::Boolean:
        return value.asBoolean() ? "true" : "false";
    case Value::Kind::Null:
        return "null";
    case Value::Kind::Missing:
        return "undefined";
    case Value::Kind::Array:
    case Value::Kind::Object:
        throw std::runtime_error("compound torrent field cannot be converted to text");
    }
    throw std::runtime_error("unknown torrent field cannot be converted to text");
}

const Value &requiredProperty(const Value &object, std::string_view key, std::string_view label)
{
    const Value *value = object.find(key);
    if (!value || !jsTruthy(*value))
        throw std::runtime_error("Torrent is missing required field: " + std::string(label));
    return *value;
}

const Value &presentProperty(const Value &object, std::string_view key, std::string_view label)
{
    const Value *value = object.find(key);
    if (!value)
        throw std::runtime_error("Torrent is missing required field: " + std::string(label));
    return *value;
}

std::uint64_t unsignedInteger(const Value &value, std::string_view label, bool requirePositive)
{
    if (value.kind() != Value::Kind::Number || !std::isfinite(value.asNumber())
        || std::trunc(value.asNumber()) != value.asNumber()
        || value.asNumber() < 0.0
        || (requirePositive && value.asNumber() == 0.0)) {
        throw std::runtime_error("Torrent field is not a valid integer: " + std::string(label));
    }
    const long double converted = static_cast<long double>(value.asNumber());
    if (converted > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::runtime_error("Torrent field exceeds native range: " + std::string(label));
    return static_cast<std::uint64_t>(value.asNumber());
}

std::int64_t signedInteger(const Value &value, std::string_view label)
{
    if (value.kind() != Value::Kind::Number || !std::isfinite(value.asNumber())
        || std::trunc(value.asNumber()) != value.asNumber()) {
        throw std::runtime_error("Torrent field is not a valid integer: " + std::string(label));
    }
    const long double converted = static_cast<long double>(value.asNumber());
    if (converted < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || converted > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error("Torrent field exceeds native range: " + std::string(label));
    return static_cast<std::int64_t>(value.asNumber());
}

std::int64_t addSigned(std::int64_t left, std::int64_t right, std::string_view label)
{
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
        throw std::overflow_error("Torrent field overflows native range: " + std::string(label));
    return left + right;
}

std::optional<VirtualPieceMap> nativeGeometry(std::int64_t totalLength,
                                              std::int64_t pieceLength,
                                              std::string &error)
{
    if (totalLength < 0) {
        error = "native geometry rejects a negative total length";
        return std::nullopt;
    }
    if (pieceLength <= 0) {
        error = "native geometry requires a positive verification piece length";
        return std::nullopt;
    }
    try {
        return VirtualPieceMap::create(static_cast<std::uint64_t>(totalLength),
                                       static_cast<std::uint64_t>(pieceLength));
    } catch (const std::exception &exception) {
        error = "native geometry validation failed: " + std::string(exception.what());
        return std::nullopt;
    }
}

std::string joinPath(const std::vector<std::string> &parts)
{
#ifdef _WIN32
    constexpr char separator = '\\';
#else
    constexpr char separator = '/';
#endif
    std::vector<std::string> normalized;
    for (const auto &part : parts) {
        std::size_t begin = 0;
        while (begin <= part.size()) {
            const std::size_t end = part.find_first_of("/\\", begin);
            const std::string component = part.substr(begin, end == std::string::npos
                                                               ? std::string::npos
                                                               : end - begin);
            if (component.empty() || component == ".") {
                // Node path.join ignores empty and current-directory components.
            } else if (component == "..") {
                if (!normalized.empty())
                    normalized.pop_back();
            } else {
                normalized.push_back(component);
            }
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
    }

    std::string result;
    for (const auto &component : normalized) {
        if (!result.empty())
            result.push_back(separator);
        result += component;
    }
    return result;
}

std::vector<std::string> pathParts(const Value &file, const std::string &rootName)
{
    const Value *utf8 = file.find("path.utf-8");
    const Value *plain = file.find("path");
    const Value *selected = utf8 && jsTruthy(*utf8) ? utf8 : plain;

    std::vector<std::string> parts;
    parts.push_back(rootName);
    if (!selected)
        return parts;
    if (selected->kind() == Value::Kind::Array) {
        for (const auto &part : selected->asArray())
            parts.push_back(valueText(part));
    } else {
        parts.push_back(valueText(*selected));
    }
    return parts;
}

ByteBuffer sha1(const ByteBuffer &input)
{
    std::array<std::uint32_t, 5> state {
        0x67452301U,
        0xEFCDAB89U,
        0x98BADCFEU,
        0x10325476U,
        0xC3D2E1F0U,
    };

    ByteBuffer padded = input;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(padded.size()) * 8U;
    padded.push_back(0x80U);
    while (padded.size() % 64 != 56)
        padded.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        padded.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffU));

    const auto rotateLeft = [](std::uint32_t value, unsigned int count) {
        return (value << count) | (value >> (32U - count));
    };
    for (std::size_t block = 0; block < padded.size(); block += 64) {
        std::array<std::uint32_t, 80> words {};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t offset = block + index * 4;
            words[index] = (static_cast<std::uint32_t>(padded[offset]) << 24U)
                           | (static_cast<std::uint32_t>(padded[offset + 1]) << 16U)
                           | (static_cast<std::uint32_t>(padded[offset + 2]) << 8U)
                           | static_cast<std::uint32_t>(padded[offset + 3]);
        }
        for (std::size_t index = 16; index < 80; ++index)
            words[index] = rotateLeft(words[index - 3] ^ words[index - 8] ^ words[index - 14]
                                          ^ words[index - 16],
                                      1);

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        for (std::size_t index = 0; index < 80; ++index) {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (index < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999U;
            } else if (index < 40) {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1U;
            } else if (index < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCU;
            } else {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6U;
            }
            const std::uint32_t next = rotateLeft(a, 5) + function + e + constant + words[index];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    ByteBuffer result;
    result.reserve(20);
    for (const auto word : state) {
        result.push_back(static_cast<std::uint8_t>((word >> 24U) & 0xffU));
        result.push_back(static_cast<std::uint8_t>((word >> 16U) & 0xffU));
        result.push_back(static_cast<std::uint8_t>((word >> 8U) & 0xffU));
        result.push_back(static_cast<std::uint8_t>(word & 0xffU));
    }
    return result;
}

std::string hex(const ByteBuffer &bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[(byte >> 4U) & 0x0fU]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

std::vector<std::string> pieceHashes(const ByteBuffer &pieces)
{
    std::vector<std::string> result;
    for (std::size_t offset = 0; offset < pieces.size(); offset += 20) {
        const std::size_t length = std::min<std::size_t>(20, pieces.size() - offset);
        result.push_back(hex(ByteBuffer(pieces.begin() + static_cast<std::ptrdiff_t>(offset),
                                        pieces.begin() + static_cast<std::ptrdiff_t>(offset + length))));
    }
    return result;
}

void appendUnique(std::vector<std::string> &values, std::string value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(std::move(value));
}

} // namespace

TorrentMetadata::TorrentMetadata(Value info,
                                 ByteBuffer infoBuffer,
                                 ByteBuffer infoHashBytes,
                                 std::string infoHash,
                                 std::string name,
                                 std::vector<TorrentFile> files,
                                 std::vector<std::string> pieces,
                                 std::vector<std::string> announce,
                                 std::vector<std::string> urlList,
                                 std::int64_t length,
                                 std::int64_t pieceLength,
                                 std::int64_t lastPieceLength,
                                 std::optional<bool> privateValue,
                                 std::optional<std::int64_t> creationDate,
                                 std::optional<std::string> createdBy,
                                 std::optional<std::string> comment,
                                 std::optional<VirtualPieceMap> geometry,
                                 std::string geometryError)
    : info_(std::move(info))
    , infoBuffer_(std::move(infoBuffer))
    , infoHashBytes_(std::move(infoHashBytes))
    , infoHash_(std::move(infoHash))
    , name_(std::move(name))
    , files_(std::move(files))
    , pieces_(std::move(pieces))
    , announce_(std::move(announce))
    , urlList_(std::move(urlList))
    , length_(length)
    , pieceLength_(pieceLength)
    , lastPieceLength_(lastPieceLength)
    , private_(privateValue)
    , creationDate_(std::move(creationDate))
    , createdBy_(std::move(createdBy))
    , comment_(std::move(comment))
    , geometry_(std::move(geometry))
    , geometryError_(std::move(geometryError))
{
}

std::optional<TorrentMetadata> TorrentMetadata::parse(const ByteBuffer &torrent, std::string *error)
{
    if (error)
        error->clear();
    try {
        const Value root = BencodeReader(torrent).read();
        if (root.kind() != Value::Kind::Object)
            throw std::runtime_error("torrent root must be a dictionary");

        const Value &info = requiredProperty(root, "info", "info");
        if (info.kind() != Value::Kind::Object)
            throw std::runtime_error("torrent info must be a dictionary");

        const Value *utf8Name = info.find("name.utf-8");
        const Value *plainName = info.find("name");
        const Value *nameValue = utf8Name && jsTruthy(*utf8Name) ? utf8Name : plainName;
        if (!nameValue || !jsTruthy(*nameValue))
            throw std::runtime_error("Torrent is missing required field: info.name");
        const std::string name = valueText(*nameValue);

        const Value &pieceLengthValue = requiredProperty(info, "piece length", "info['piece length']");
        const std::int64_t pieceLength = signedInteger(pieceLengthValue, "info['piece length']");
        if (pieceLength == 0)
            throw std::runtime_error("Torrent is missing required field: info['piece length']");
        const Value &piecesValue = requiredProperty(info, "pieces", "info.pieces");
        if (piecesValue.kind() != Value::Kind::Bytes)
            throw std::runtime_error("Torrent field is not a byte string: info.pieces");

        std::vector<TorrentFile> files;
        const Value *filesValue = info.find("files");
        if (filesValue) {
            if (filesValue->kind() != Value::Kind::Array || filesValue->asArray().empty())
                throw std::runtime_error("Torrent field is not a non-empty file list: info.files");
            files.reserve(filesValue->asArray().size());
            for (std::size_t index = 0; index < filesValue->asArray().size(); ++index) {
                const Value &file = filesValue->asArray()[index];
                if (file.kind() != Value::Kind::Object)
                    throw std::runtime_error("Torrent file entry is not a dictionary");
                const std::string label = "info.files[" + std::to_string(index) + "]";
                const Value &lengthValue = presentProperty(file, "length", label + ".length");
                const std::int64_t length = signedInteger(lengthValue, label + ".length");
                const Value *utf8Path = file.find("path.utf-8");
                const Value *plainPath = file.find("path");
                const Value *selectedPath = utf8Path && jsTruthy(*utf8Path) ? utf8Path : plainPath;
                if (!selectedPath || !jsTruthy(*selectedPath))
                    throw std::runtime_error("Torrent is missing required field: " + label + ".path");
                const auto parts = pathParts(file, name);
                const std::string path = joinPath(parts);
                TorrentFile output;
                output.path = path;
                output.name = parts.back();
                output.length = length;
                output.offset = files.empty()
                                    ? 0
                                    : addSigned(files.back().offset,
                                                files.back().length,
                                                label + ".offset");
                files.push_back(std::move(output));
            }
        } else {
            const Value &lengthValue = presentProperty(info, "length", "info.length");
            const std::int64_t length = signedInteger(lengthValue, "info.length");
            files.push_back({name, name, length, 0});
        }

        std::int64_t totalLength = 0;
        for (const auto &file : files) {
            totalLength = addSigned(totalLength, file.length, "torrent length");
        }

        std::vector<std::string> announce;
        const Value *announceListValue = root.find("announce-list");
        if (announceListValue && announceListValue->kind() == Value::Kind::Array
            && !announceListValue->asArray().empty()) {
            for (const auto &tier : announceListValue->asArray()) {
                if (tier.kind() != Value::Kind::Array)
                    throw std::runtime_error("Torrent field is not an announce tier list");
                for (const auto &url : tier.asArray())
                    appendUnique(announce, valueText(url));
            }
        } else if (const Value *announceValue = root.find("announce")) {
            if (jsTruthy(*announceValue))
                appendUnique(announce, valueText(*announceValue));
        }

        std::vector<std::string> urlList;
        if (const Value *urlListValue = root.find("url-list")) {
            if (urlListValue->kind() == Value::Kind::Bytes) {
                if (!urlListValue->asBytes().empty())
                    appendUnique(urlList, valueText(*urlListValue));
            } else if (urlListValue->kind() == Value::Kind::Array) {
                for (const auto &url : urlListValue->asArray())
                    appendUnique(urlList, valueText(url));
            } else {
                throw std::runtime_error("Torrent field is not a URL list");
            }
        }

        std::optional<std::int64_t> creationDate;
        if (const Value *created = root.find("creation date")) {
            if (jsTruthy(*created)) {
                const std::uint64_t value = unsignedInteger(*created, "creation date", false);
                if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    throw std::runtime_error("creation date exceeds native range");
                creationDate = static_cast<std::int64_t>(value);
            }
        }
        std::optional<std::string> createdBy;
        if (const Value *created = root.find("created by")) {
            if (jsTruthy(*created))
                createdBy = valueText(*created);
        }
        std::optional<std::string> comment;
        if (const Value *commentValue = root.find("comment")) {
            if (commentValue->kind() == Value::Kind::Bytes)
                comment = valueText(*commentValue);
        }
        // M303 27769 reads torrent.info.private and preserves property presence before !!.
        std::optional<bool> privateValue;
        if (const Value *privateField = info.find("private"))
            privateValue = jsTruthy(*privateField);

        ByteBuffer infoBuffer = encodeBencode(info);
        ByteBuffer infoHashBytes = sha1(infoBuffer);
        const std::string infoHash = hex(infoHashBytes);
        std::string geometryError;
        const auto geometry = nativeGeometry(totalLength, pieceLength, geometryError);
        const std::int64_t remainder = totalLength % pieceLength;
        const std::int64_t lastPieceLength = remainder == 0 ? pieceLength : remainder;

        return TorrentMetadata(info,
                               std::move(infoBuffer),
                               std::move(infoHashBytes),
                               infoHash,
                               name,
                               std::move(files),
                               pieceHashes(piecesValue.asBytes()),
                               std::move(announce),
                               std::move(urlList),
                               totalLength,
                               pieceLength,
                               lastPieceLength,
                               privateValue,
                               std::move(creationDate),
                               std::move(createdBy),
                               std::move(comment),
                               geometry,
                               std::move(geometryError));
    } catch (const std::exception &exception) {
        if (error)
            *error = exception.what();
        return std::nullopt;
    }
}

const Value &TorrentMetadata::info() const noexcept
{
    return info_;
}

const ByteBuffer &TorrentMetadata::infoBuffer() const noexcept
{
    return infoBuffer_;
}

const ByteBuffer &TorrentMetadata::infoHashBytes() const noexcept
{
    return infoHashBytes_;
}

const std::string &TorrentMetadata::infoHash() const noexcept
{
    return infoHash_;
}

const std::string &TorrentMetadata::name() const noexcept
{
    return name_;
}

const std::vector<TorrentFile> &TorrentMetadata::files() const noexcept
{
    return files_;
}

const std::vector<std::string> &TorrentMetadata::pieces() const noexcept
{
    return pieces_;
}

const std::vector<std::string> &TorrentMetadata::announce() const noexcept
{
    return announce_;
}

const std::vector<std::string> &TorrentMetadata::urlList() const noexcept
{
    return urlList_;
}

std::int64_t TorrentMetadata::length() const noexcept
{
    return length_;
}

std::int64_t TorrentMetadata::pieceLength() const noexcept
{
    return pieceLength_;
}

std::int64_t TorrentMetadata::lastPieceLength() const noexcept
{
    return lastPieceLength_;
}

bool TorrentMetadata::isPrivate() const noexcept
{
    return private_.value_or(false);
}

const std::optional<bool> &TorrentMetadata::privateValue() const noexcept
{
    return private_;
}

const std::optional<std::int64_t> &TorrentMetadata::creationDate() const noexcept
{
    return creationDate_;
}

const std::optional<std::string> &TorrentMetadata::createdBy() const noexcept
{
    return createdBy_;
}

const std::optional<std::string> &TorrentMetadata::comment() const noexcept
{
    return comment_;
}

const VirtualPieceMap &TorrentMetadata::geometry() const
{
    if (!geometry_)
        throw std::logic_error(geometryError_.empty() ? "native geometry is unavailable"
                                                      : geometryError_);
    return *geometry_;
}

} // namespace server1::policy
