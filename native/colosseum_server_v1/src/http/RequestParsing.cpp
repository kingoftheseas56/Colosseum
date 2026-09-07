#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server1::http::detail {

struct QueryEntry {
    std::string key;
    std::string value;
};

struct Header {
    std::string name;
    std::string value;
};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string version;
    std::string body;
    std::string jsonBody;
    std::vector<Header> headers;
    std::vector<QueryEntry> query;
    std::vector<QueryEntry> form;
    int bodyKind = 0;
    bool keepAlive = true;
};

enum class ParseState : int {
    NeedMore = 0,
    Complete = 1,
    Error = 2,
};

struct Parser final {
    explicit Parser(std::size_t maxBodyBytesIn)
        : maxBodyBytes(maxBodyBytesIn)
    {
    }

    std::size_t maxBodyBytes;
    std::string buffer;
    Request request;
    std::string remaining;
    std::string error;
    std::size_t consumed = 0;
    int errorStatus = 0;
    ParseState state = ParseState::NeedMore;
};

namespace {

constexpr std::size_t kMaxHeaderBytes = 64U * 1024U;
constexpr char kEmpty[] = "";

std::string lower(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

bool isTokenCharacter(unsigned char character)
{
    if (character <= 0x20U || character >= 0x7fU)
        return false;
    switch (character) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '"':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
        return false;
    default:
        return true;
    }
}

std::string trimOWS(std::string_view value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && (value[begin] == ' ' || value[begin] == '\t'))
        ++begin;
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t'))
        --end;
    return std::string(value.substr(begin, end - begin));
}

bool isHex(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')
        || (character >= 'A' && character <= 'F');
}

unsigned char hexValue(char character)
{
    if (character >= '0' && character <= '9')
        return static_cast<unsigned char>(character - '0');
    if (character >= 'a' && character <= 'f')
        return static_cast<unsigned char>(character - 'a' + 10);
    return static_cast<unsigned char>(character - 'A' + 10);
}

bool decodeComponent(std::string_view encoded, bool plusAsSpace, std::string &decoded)
{
    decoded.clear();
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char character = encoded[i];
        if (character == '%' ) {
            if (i + 2 >= encoded.size() || !isHex(encoded[i + 1]) || !isHex(encoded[i + 2]))
                return false;
            decoded.push_back(static_cast<char>((hexValue(encoded[i + 1]) << 4U)
                                                | hexValue(encoded[i + 2])));
            i += 2;
        } else if (plusAsSpace && character == '+') {
            decoded.push_back(' ');
        } else {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (byte < 0x20U || byte == 0x7fU)
                return false;
            decoded.push_back(character);
        }
    }
    return true;
}

bool parsePairs(std::string_view encoded, std::vector<QueryEntry> &entries)
{
    entries.clear();
    if (encoded.empty())
        return true;
    std::size_t begin = 0;
    while (begin <= encoded.size()) {
        const std::size_t end = encoded.find('&', begin);
        const std::size_t pairEnd = end == std::string_view::npos ? encoded.size() : end;
        const std::string_view pair = encoded.substr(begin, pairEnd - begin);
        const std::size_t equals = pair.find('=');
        const std::string_view rawKey = equals == std::string_view::npos ? pair : pair.substr(0, equals);
        const std::string_view rawValue = equals == std::string_view::npos ? std::string_view{}
                                                                           : pair.substr(equals + 1);
        QueryEntry entry;
        if (!decodeComponent(rawKey, true, entry.key) || !decodeComponent(rawValue, true, entry.value))
            return false;
        entries.push_back(std::move(entry));
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return true;
}

bool validateTarget(std::string_view target, std::string &path, std::string &query)
{
    if (target.empty())
        return false;
    for (const unsigned char character : target) {
        if (character <= 0x20U || character == 0x7fU || character == '#')
            return false;
    }

    std::string_view origin = target;
    const bool absolute = origin.rfind("http://", 0) == 0 || origin.rfind("https://", 0) == 0;
    if (absolute) {
        const std::size_t authorityEnd = origin.find('/', origin.find("://") + 3);
        origin = authorityEnd == std::string_view::npos ? std::string_view("/")
                                                         : origin.substr(authorityEnd);
    } else if (origin != "*" && origin.front() != '/') {
        return false;
    }

    if (origin == "*") {
        path = "*";
        query.clear();
        return true;
    }

    const std::size_t queryStart = origin.find('?');
    const std::string_view encodedPath = queryStart == std::string_view::npos
        ? origin
        : origin.substr(0, queryStart);
    query = queryStart == std::string_view::npos ? std::string{}
                                                  : std::string(origin.substr(queryStart + 1));
    if (encodedPath.empty() || encodedPath.front() != '/')
        return false;
    for (std::size_t i = 0; i < encodedPath.size(); ++i) {
        if (encodedPath[i] == '%') {
            if (i + 2 >= encodedPath.size() || !isHex(encodedPath[i + 1]) || !isHex(encodedPath[i + 2]))
                return false;
            i += 2;
        }
    }
    path.assign(encodedPath);
    return true;
}

const std::vector<std::string> headerValues(const Request &request, std::string_view name)
{
    const std::string wanted = lower(name);
    std::vector<std::string> values;
    for (const Header &header : request.headers) {
        if (header.name == wanted)
            values.push_back(header.value);
    }
    return values;
}

bool hasToken(const std::vector<std::string> &values, std::string_view token)
{
    const std::string wanted = lower(token);
    for (const std::string &value : values) {
        std::size_t begin = 0;
        while (begin <= value.size()) {
            const std::size_t end = value.find(',', begin);
            const std::size_t tokenEnd = end == std::string::npos ? value.size() : end;
            if (lower(trimOWS(std::string_view(value).substr(begin, tokenEnd - begin))) == wanted)
                return true;
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
    }
    return false;
}

bool parseDecimal(std::string_view value, std::size_t &result)
{
    if (value.empty())
        return false;
    std::size_t parsed = 0;
    const auto *begin = value.data();
    const auto *end = value.data() + value.size();
    const auto conversion = std::from_chars(begin, end, parsed, 10);
    if (conversion.ec != std::errc{} || conversion.ptr != end)
        return false;
    result = parsed;
    return true;
}

bool parseChunkSize(std::string_view value, std::size_t &result)
{
    const std::size_t extension = value.find(';');
    const std::string_view sizeText = trimOWS(value.substr(0, extension));
    if (sizeText.empty())
        return false;
    result = 0;
    for (const char character : sizeText) {
        if (!isHex(character))
            return false;
        const std::size_t digit = hexValue(character);
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 16U)
            return false;
        result = (result << 4U) | digit;
    }
    return true;
}

bool parseChunkedBody(std::string_view buffer, std::size_t bodyStart, std::size_t maxBody,
                      std::string &body, std::size_t &consumed, bool &needMore, bool &tooLarge,
                      bool &trailersTooLarge)
{
    body.clear();
    needMore = false;
    tooLarge = false;
    trailersTooLarge = false;
    std::size_t cursor = bodyStart;
    while (true) {
        const std::size_t lineEnd = buffer.find("\r\n", cursor);
        if (lineEnd == std::string_view::npos) {
            needMore = true;
            return true;
        }
        std::size_t chunkSize = 0;
        if (!parseChunkSize(buffer.substr(cursor, lineEnd - cursor), chunkSize))
            return false;
        if (lineEnd > std::numeric_limits<std::size_t>::max() - 2U)
            return false;
        cursor = lineEnd + 2;
        if (body.size() > maxBody || chunkSize > maxBody - body.size()) {
            tooLarge = true;
            return false;
        }
        if (chunkSize == 0) {
            std::size_t trailerBytes = 0;
            while (true) {
                if (cursor > buffer.size()) {
                    needMore = true;
                    return true;
                }
                const std::size_t available = buffer.size() - cursor;
                const std::size_t trailerEnd = buffer.find("\r\n", cursor);
                if (trailerEnd == std::string_view::npos) {
                    if (available > kMaxHeaderBytes) {
                        trailersTooLarge = true;
                        return false;
                    }
                    needMore = true;
                    return true;
                }
                if (trailerEnd < cursor || trailerEnd > std::numeric_limits<std::size_t>::max() - 2U)
                    return false;
                const std::size_t lineBytes = trailerEnd - cursor;
                if (trailerBytes > kMaxHeaderBytes - 2U
                    || lineBytes > kMaxHeaderBytes - trailerBytes - 2U) {
                    trailersTooLarge = true;
                    return false;
                }
                trailerBytes += lineBytes + 2U;
                if (trailerEnd == cursor) {
                    consumed = cursor + 2;
                    needMore = false;
                    return true;
                }
                cursor = trailerEnd + 2;
            }
        }
        if (cursor > buffer.size()) {
            needMore = true;
            return true;
        }
        const std::size_t available = buffer.size() - cursor;
        if (available < 2 || chunkSize > available - 2) {
            needMore = true;
            return true;
        }
        body.append(buffer.substr(cursor, chunkSize));
        if (buffer[cursor + chunkSize] != '\r' || buffer[cursor + chunkSize + 1] != '\n')
            return false;
        cursor += chunkSize + 2;
    }
}

bool isSupportedTransferEncoding(const std::vector<std::string> &values)
{
    std::size_t codingCount = 0;
    for (const std::string &value : values) {
        std::size_t begin = 0;
        while (begin <= value.size()) {
            const std::size_t end = value.find(',', begin);
            const std::size_t codingEnd = end == std::string::npos ? value.size() : end;
            const std::string coding = lower(trimOWS(
                std::string_view(value).substr(begin, codingEnd - begin)));
            if (coding != "chunked" || ++codingCount != 1)
                return false;
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
    }
    return codingCount == 1;
}

void fail(Parser &parser, int status, std::string message)
{
    parser.state = ParseState::Error;
    parser.errorStatus = status;
    parser.error = std::move(message);
}

ParseState tryParse(Parser &parser)
{
    if (parser.state != ParseState::NeedMore)
        return parser.state;

    const std::size_t headerEnd = parser.buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (parser.buffer.size() > kMaxHeaderBytes)
            fail(parser, 431, "request headers too large");
        return parser.state;
    }
    if (headerEnd > kMaxHeaderBytes - 4U) {
        fail(parser, 431, "request headers too large");
        return parser.state;
    }

    const std::size_t requestLineEnd = parser.buffer.find("\r\n");
    if (requestLineEnd == std::string::npos || requestLineEnd > headerEnd) {
        fail(parser, 400, "malformed request line");
        return parser.state;
    }
    const std::string_view requestLine(parser.buffer.data(), requestLineEnd);
    const std::size_t firstSpace = requestLine.find(' ');
    const std::size_t secondSpace = requestLine.find(' ', firstSpace == std::string_view::npos
                                                              ? 0
                                                              : firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos
        || firstSpace == 0 || secondSpace <= firstSpace + 1
        || requestLine.find(' ', secondSpace + 1) != std::string_view::npos) {
        fail(parser, 400, "malformed request line");
        return parser.state;
    }

    Request request;
    request.method.assign(requestLine.substr(0, firstSpace));
    request.target.assign(requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));
    request.version.assign(requestLine.substr(secondSpace + 1));
    for (const unsigned char character : request.method) {
        if (!isTokenCharacter(character)) {
            fail(parser, 400, "invalid method token");
            return parser.state;
        }
    }
    if (request.version != "HTTP/1.1" && request.version != "HTTP/1.0") {
        fail(parser, 505, "unsupported HTTP version");
        return parser.state;
    }

    std::string query;
    if (!validateTarget(request.target, request.path, query) || !parsePairs(query, request.query)) {
        fail(parser, 400, "malformed request target");
        return parser.state;
    }

    std::size_t headerCursor = requestLineEnd + 2;
    while (headerCursor < headerEnd) {
        const std::size_t lineEnd = parser.buffer.find("\r\n", headerCursor);
        if (lineEnd == std::string::npos || lineEnd > headerEnd) {
            fail(parser, 400, "malformed header block");
            return parser.state;
        }
        const std::string_view line(parser.buffer.data() + headerCursor, lineEnd - headerCursor);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            fail(parser, 400, "malformed header");
            return parser.state;
        }
        const std::string_view rawName = line.substr(0, colon);
        for (const unsigned char character : rawName) {
            if (!isTokenCharacter(character)) {
                fail(parser, 400, "invalid header name");
                return parser.state;
            }
        }
        request.headers.push_back({lower(rawName), trimOWS(line.substr(colon + 1))});
        headerCursor = lineEnd + 2;
    }

    const std::vector<std::string> contentLengths = headerValues(request, "content-length");
    const std::vector<std::string> transferEncodings = headerValues(request, "transfer-encoding");
    std::size_t bodyBytes = 0;
    const std::size_t bodyStart = headerEnd + 4;
    bool chunked = false;
    if (!transferEncodings.empty()) {
        if (!isSupportedTransferEncoding(transferEncodings)) {
            fail(parser, 400, "unsupported transfer encoding");
            return parser.state;
        }
        chunked = true;
    }
    if (chunked && !contentLengths.empty()) {
        fail(parser, 400, "content-length with chunked transfer encoding");
        return parser.state;
    }

    std::string body;
    if (chunked) {
        bool needMore = false;
        bool tooLarge = false;
        bool trailersTooLarge = false;
        std::size_t chunkedConsumed = 0;
        if (!parseChunkedBody(parser.buffer, bodyStart, parser.maxBodyBytes, body, chunkedConsumed,
                              needMore, tooLarge, trailersTooLarge)) {
            fail(parser, trailersTooLarge ? 431 : (tooLarge ? 413 : 400),
                 trailersTooLarge
                     ? "request trailers too large"
                     : (tooLarge ? "request body too large" : "malformed chunked body"));
            return parser.state;
        }
        if (needMore)
            return parser.state;
        bodyBytes = body.size();
        parser.consumed = chunkedConsumed;
    } else {
        if (contentLengths.size() > 1) {
            std::size_t firstLength = 0;
            if (!parseDecimal(trimOWS(contentLengths.front()), firstLength)) {
                fail(parser, 400, "invalid content-length");
                return parser.state;
            }
            for (const std::string &value : contentLengths) {
                std::size_t currentLength = 0;
                if (!parseDecimal(trimOWS(value), currentLength) || currentLength != firstLength) {
                    fail(parser, 400, "conflicting content-length");
                    return parser.state;
                }
            }
            bodyBytes = firstLength;
        } else if (!contentLengths.empty() && !parseDecimal(trimOWS(contentLengths.front()), bodyBytes)) {
            fail(parser, 400, "invalid content-length");
            return parser.state;
        }
        if (bodyBytes > parser.maxBodyBytes) {
            fail(parser, 413, "request body too large");
            return parser.state;
        }
        if (parser.buffer.size() < bodyStart + bodyBytes)
            return parser.state;
        body.assign(parser.buffer.data() + bodyStart, bodyBytes);
        parser.consumed = bodyStart + bodyBytes;
    }

    request.body = std::move(body);
    const std::vector<std::string> contentTypes = headerValues(request, "content-type");
    if (!contentTypes.empty()) {
        const std::string contentType = lower(trimOWS(contentTypes.front()));
        if (contentType.rfind("application/json", 0) == 0) {
            if (!request.body.empty()) {
                QJsonParseError jsonError{};
                const QJsonDocument document = QJsonDocument::fromJson(
                    QByteArray::fromStdString(request.body), &jsonError);
                if (jsonError.error != QJsonParseError::NoError || document.isNull()) {
                    fail(parser, 400, "invalid JSON body");
                    return parser.state;
                }
                request.jsonBody = document.toJson(QJsonDocument::Compact).toStdString();
            }
            request.bodyKind = 1;
        } else if (contentType.rfind("application/x-www-form-urlencoded", 0) == 0) {
            if (!parsePairs(request.body, request.form)) {
                fail(parser, 400, "invalid urlencoded body");
                return parser.state;
            }
            request.bodyKind = 2;
        }
    }

    const std::vector<std::string> connection = headerValues(request, "connection");
    request.keepAlive = request.version == "HTTP/1.1" ? !hasToken(connection, "close")
                                                        : hasToken(connection, "keep-alive");
    parser.request = std::move(request);
    parser.remaining.assign(parser.buffer.data() + parser.consumed,
                            parser.buffer.size() - parser.consumed);
    parser.state = ParseState::Complete;
    return parser.state;
}

} // namespace

} // namespace server1::http::detail

extern "C" {

void *server1_http_parser_create(std::size_t maxBodyBytes)
{
    return new server1::http::detail::Parser(maxBodyBytes);
}

void server1_http_parser_destroy(void *opaqueParser)
{
    delete static_cast<server1::http::detail::Parser *>(opaqueParser);
}

int server1_http_parser_feed(void *opaqueParser, const char *data, std::size_t size)
{
    if (opaqueParser == nullptr || (data == nullptr && size != 0))
        return static_cast<int>(server1::http::detail::ParseState::Error);
    auto &parser = *static_cast<server1::http::detail::Parser *>(opaqueParser);
    if (parser.state == server1::http::detail::ParseState::NeedMore && size != 0)
        parser.buffer.append(data, size);
    return static_cast<int>(server1::http::detail::tryParse(parser));
}

int server1_http_parser_error_status(void *opaqueParser)
{
    return opaqueParser == nullptr ? 400
                                    : static_cast<server1::http::detail::Parser *>(opaqueParser)->errorStatus;
}

const char *server1_http_parser_error(void *opaqueParser)
{
    return opaqueParser == nullptr ? "null parser"
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->error.c_str();
}

std::size_t server1_http_parser_consumed(void *opaqueParser)
{
    return opaqueParser == nullptr ? 0
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->consumed;
}

const char *server1_http_parser_remaining(void *opaqueParser)
{
    return opaqueParser == nullptr ? ""
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->remaining.c_str();
}

std::size_t server1_http_parser_remaining_size(void *opaqueParser)
{
    return opaqueParser == nullptr ? 0
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->remaining.size();
}

const char *server1_http_parser_method(void *opaqueParser)
{
    return opaqueParser == nullptr ? ""
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.method.c_str();
}

const char *server1_http_parser_target(void *opaqueParser)
{
    return opaqueParser == nullptr ? ""
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.target.c_str();
}

const char *server1_http_parser_path(void *opaqueParser)
{
    return opaqueParser == nullptr ? ""
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.path.c_str();
}

const char *server1_http_parser_body(void *opaqueParser)
{
    return opaqueParser == nullptr ? ""
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.body.c_str();
}

int server1_http_parser_body_kind(void *opaqueParser)
{
    return opaqueParser == nullptr ? 0
                                   : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.bodyKind;
}

bool server1_http_parser_keep_alive(void *opaqueParser)
{
    return opaqueParser != nullptr
        && static_cast<server1::http::detail::Parser *>(opaqueParser)->request.keepAlive;
}

std::size_t server1_http_parser_header_count(void *opaqueParser, const char *name)
{
    if (opaqueParser == nullptr || name == nullptr)
        return 0;
    const std::string wanted = server1::http::detail::lower(name);
    const auto &headers = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.headers;
    return static_cast<std::size_t>(std::count_if(headers.begin(), headers.end(), [&](const auto &header) {
        return header.name == wanted;
    }));
}

const char *server1_http_parser_header_value_at(void *opaqueParser, const char *name, std::size_t index)
{
    if (opaqueParser == nullptr || name == nullptr)
        return server1::http::detail::kEmpty;
    const std::string wanted = server1::http::detail::lower(name);
    const auto &headers = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.headers;
    for (const auto &header : headers) {
        if (header.name == wanted) {
            if (index == 0)
                return header.value.c_str();
            --index;
        }
    }
    return server1::http::detail::kEmpty;
}

std::size_t server1_http_parser_query_entry_count(void *opaqueParser)
{
    return opaqueParser == nullptr
        ? 0
        : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.query.size();
}

const char *server1_http_parser_query_key_at(void *opaqueParser, std::size_t index)
{
    if (opaqueParser == nullptr)
        return server1::http::detail::kEmpty;
    const auto &entries = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.query;
    return index < entries.size() ? entries[index].key.c_str() : server1::http::detail::kEmpty;
}

const char *server1_http_parser_query_value_at(void *opaqueParser, std::size_t index)
{
    if (opaqueParser == nullptr)
        return server1::http::detail::kEmpty;
    const auto &entries = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.query;
    return index < entries.size() ? entries[index].value.c_str() : server1::http::detail::kEmpty;
}

std::size_t server1_http_parser_form_entry_count(void *opaqueParser)
{
    return opaqueParser == nullptr
        ? 0
        : static_cast<server1::http::detail::Parser *>(opaqueParser)->request.form.size();
}

const char *server1_http_parser_form_key_at(void *opaqueParser, std::size_t index)
{
    if (opaqueParser == nullptr)
        return server1::http::detail::kEmpty;
    const auto &entries = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.form;
    return index < entries.size() ? entries[index].key.c_str() : server1::http::detail::kEmpty;
}

const char *server1_http_parser_form_value_at(void *opaqueParser, std::size_t index)
{
    if (opaqueParser == nullptr)
        return server1::http::detail::kEmpty;
    const auto &entries = static_cast<server1::http::detail::Parser *>(opaqueParser)->request.form;
    return index < entries.size() ? entries[index].value.c_str() : server1::http::detail::kEmpty;
}

}
