#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server1::http::router_detail {

struct QueryEntry {
    std::string key;
    std::string value;
};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string body;
    std::vector<QueryEntry> query;
    std::map<std::string, std::string> params;
};

struct Response final {
    int status = 404;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    bool close = false;
};

namespace {

constexpr char kEmpty[] = "";

std::string lower(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

std::string upper(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::toupper(character)));
    return result;
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

bool decode(std::string_view encoded, bool plusAsSpace, std::string &decoded)
{
    decoded.clear();
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            if (i + 2 >= encoded.size() || !isHex(encoded[i + 1]) || !isHex(encoded[i + 2]))
                return false;
            decoded.push_back(static_cast<char>((hexValue(encoded[i + 1]) << 4U)
                                                | hexValue(encoded[i + 2])));
            i += 2;
        } else if (plusAsSpace && encoded[i] == '+') {
            decoded.push_back(' ');
        } else {
            const unsigned char character = static_cast<unsigned char>(encoded[i]);
            if (character <= 0x20U || character == 0x7fU)
                return false;
            decoded.push_back(encoded[i]);
        }
    }
    return true;
}

bool parseTarget(std::string_view target, std::string &path, std::vector<QueryEntry> &query)
{
    if (target.empty())
        return false;
    for (const unsigned char character : target) {
        if (character <= 0x20U || character == 0x7fU || character == '#')
            return false;
    }
    if (target == "*") {
        path = "*";
        query.clear();
        return true;
    }
    std::string_view origin = target;
    if (origin.rfind("http://", 0) == 0 || origin.rfind("https://", 0) == 0) {
        const std::size_t authorityEnd = origin.find('/', origin.find("://") + 3);
        origin = authorityEnd == std::string_view::npos ? std::string_view("/")
                                                         : origin.substr(authorityEnd);
    }
    if (origin.front() != '/')
        return false;
    const std::size_t queryStart = origin.find('?');
    const std::string_view rawPath = queryStart == std::string_view::npos
        ? origin
        : origin.substr(0, queryStart);
    if (rawPath.empty())
        return false;
    for (std::size_t i = 0; i < rawPath.size(); ++i) {
        if (rawPath[i] == '%') {
            if (i + 2 >= rawPath.size() || !isHex(rawPath[i + 1]) || !isHex(rawPath[i + 2]))
                return false;
            i += 2;
        }
    }
    path.assign(rawPath);
    query.clear();
    if (queryStart == std::string_view::npos || queryStart + 1 == target.size())
        return true;
    const std::string_view encodedQuery = origin.substr(queryStart + 1);
    std::size_t begin = 0;
    while (begin <= encodedQuery.size()) {
        const std::size_t end = encodedQuery.find('&', begin);
        const std::size_t pairEnd = end == std::string_view::npos ? encodedQuery.size() : end;
        const std::string_view pair = encodedQuery.substr(begin, pairEnd - begin);
        const std::size_t equals = pair.find('=');
        const std::string_view rawKey = equals == std::string_view::npos ? pair : pair.substr(0, equals);
        const std::string_view rawValue = equals == std::string_view::npos ? std::string_view{}
                                                                           : pair.substr(equals + 1);
        QueryEntry entry;
        if (!decode(rawKey, true, entry.key) || !decode(rawValue, true, entry.value))
            return false;
        query.push_back(std::move(entry));
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return true;
}

std::string regexEscape(char character)
{
    switch (character) {
    case '\\':
    case '.':
    case '+':
    case '^':
    case '$':
    case '|':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
        return std::string("\\") + character;
    default:
        return std::string(1, character);
    }
}

struct CompiledPattern final {
    std::regex expression;
    std::vector<std::string> names;
    bool prefix = false;
};

bool readBalanced(std::string_view pattern, std::size_t &cursor, std::string &expression)
{
    if (cursor >= pattern.size() || pattern[cursor] != '(')
        return false;
    const std::size_t start = ++cursor;
    int depth = 1;
    while (cursor < pattern.size() && depth != 0) {
        if (pattern[cursor] == '(')
            ++depth;
        else if (pattern[cursor] == ')')
            --depth;
        ++cursor;
    }
    if (depth != 0 || cursor == start + 1)
        return false;
    expression.assign(pattern.substr(start, cursor - start - 1));
    return true;
}

bool compilePattern(std::string_view pattern, bool prefix, CompiledPattern &compiled,
                    std::string &error)
{
    if (pattern.empty() || pattern.front() != '/') {
        error = "route pattern must start with /";
        return false;
    }
    for (const unsigned char character : pattern) {
        if (character <= 0x20U || character == 0x7fU) {
            error = "route pattern contains control whitespace";
            return false;
        }
    }

    std::string expression = "^";
    std::size_t cursor = 0;
    while (cursor < pattern.size()) {
        const char character = pattern[cursor];
        if (character != ':') {
            expression += regexEscape(character);
            ++cursor;
            continue;
        }

        const std::size_t slashStart = cursor > 0 && pattern[cursor - 1] == '/' ? cursor - 1 : cursor;
        if (slashStart < cursor && expression.size() >= 2 && expression.back() == '/')
            expression.pop_back();
        ++cursor;
        const std::size_t nameStart = cursor;
        while (cursor < pattern.size()
               && (std::isalnum(static_cast<unsigned char>(pattern[cursor])) || pattern[cursor] == '_'))
            ++cursor;
        if (cursor == nameStart) {
            error = "route parameter name is empty";
            return false;
        }
        compiled.names.emplace_back(pattern.substr(nameStart, cursor - nameStart));
        std::string capture = "[^/]+?";
        if (cursor < pattern.size() && pattern[cursor] == '(') {
            if (!readBalanced(pattern, cursor, capture)) {
                error = "route parameter expression is unbalanced";
                return false;
            }
        }
        bool wildcard = false;
        bool optional = false;
        if (cursor < pattern.size() && (pattern[cursor] == '*' || pattern[cursor] == '?')) {
            wildcard = pattern[cursor] == '*';
            optional = pattern[cursor] == '?';
            ++cursor;
        }
        if (wildcard)
            capture = ".*";
        const std::string group = "(" + capture + ")";
        if (slashStart < nameStart && slashStart + 2 == nameStart) {
            expression += optional || wildcard ? "(?:/(" + capture + "))?" : "/" + group;
        } else {
            expression += optional ? group + "?" : group;
        }
    }
    try {
        if (prefix) {
            compiled.prefix = true;
            compiled.expression = std::regex(expression + "(?=/|\\.|$)", std::regex_constants::icase);
        } else {
            compiled.expression = std::regex(expression + "/?$", std::regex_constants::icase);
        }
    } catch (const std::regex_error &) {
        error = "route pattern is not a valid regular expression";
        return false;
    }
    return true;
}

struct Route final {
    std::string method;
    std::string pattern;
    std::string body;
    int status = 200;
    bool prefix = false;
    CompiledPattern compiled;
};

struct Router final {
    std::vector<Route> externalRoutes;
    std::vector<Route> rootRoutes;
    std::string error;
};

struct Match final {
    const Route *route = nullptr;
    std::map<std::string, std::string> params;
    bool invalidEncoding = false;
};

Match match(const Route &route, std::string_view path)
{
    Match result;
    if (route.prefix) {
        const std::string pathLower = lower(path);
        const std::string routeLower = lower(route.pattern);
        if (routeLower == "/" || (pathLower.rfind(routeLower, 0) == 0
                                   && (pathLower.size() == routeLower.size()
                                       || pathLower[routeLower.size()] == '/'
                                       || pathLower[routeLower.size()] == '.')))
            result.route = &route;
        return result;
    }

    std::smatch matches;
    const std::string pathString(path);
    if (!std::regex_match(pathString, matches, route.compiled.expression))
        return result;
    result.route = &route;
    std::size_t group = 1;
    for (const std::string &name : route.compiled.names) {
        if (group >= matches.size())
            break;
        std::string decoded;
        if (!decode(matches[group].str(), false, decoded)) {
            result.invalidEncoding = true;
            return result;
        }
        result.params[name] = std::move(decoded);
        ++group;
    }
    return result;
}

bool methodMatches(const Route &route, std::string_view method)
{
    if (route.method == "USE" || route.method == "ALL")
        return true;
    if (route.method == method)
        return true;
    return method == "HEAD" && route.method == "GET";
}

std::string queryValue(const Request &request, std::string_view key)
{
    for (const QueryEntry &entry : request.query) {
        if (entry.key == key)
            return entry.value;
    }
    return {};
}

std::string render(std::string body, const Request &request)
{
    std::size_t cursor = 0;
    while ((cursor = body.find('{', cursor)) != std::string::npos) {
        const std::size_t end = body.find('}', cursor + 1);
        if (end == std::string::npos)
            break;
        const std::string token = body.substr(cursor + 1, end - cursor - 1);
        std::string replacement;
        const auto parameter = request.params.find(token);
        if (parameter != request.params.end())
            replacement = parameter->second;
        else if (token.rfind("query:", 0) == 0)
            replacement = queryValue(request, token.substr(6));
        else if (token == "body")
            replacement = request.body;
        else {
            cursor = end + 1;
            continue;
        }
        body.replace(cursor, end - cursor + 1, replacement);
        cursor += replacement.size();
    }
    return body;
}

void addHeader(Response &response, std::string name, std::string value)
{
    response.headers.emplace_back(std::move(name), std::move(value));
}

Response makeResponse(const Route &route, const Request &request, const Match &matched)
{
    Response response;
    response.status = route.status;
    Request renderedRequest = request;
    renderedRequest.params = matched.params;
    response.body = render(route.body, renderedRequest);
    addHeader(response, "Content-Type", "text/plain");
    return response;
}

Response dispatch(Router &router, std::string_view method, std::string_view target,
                  std::string_view body)
{
    Request request;
    request.method = upper(method);
    request.target = target;
    request.body = body;
    if (!parseTarget(target, request.path, request.query))
        return Response{400, "Bad Request", {}, true};

    std::set<std::string> allowed;
    const auto inspect = [&](const std::vector<Route> &routes) -> std::unique_ptr<Response> {
        for (const Route &route : routes) {
            const Match matched = match(route, request.path);
            if (matched.invalidEncoding)
                return std::make_unique<Response>(Response{400, "Bad Request", {}, true});
            if (matched.route == nullptr)
                continue;
            if (request.method == "OPTIONS") {
                if (route.method != "USE" && route.method != "ALL")
                    allowed.insert(route.method);
                continue;
            }
            if (!methodMatches(route, request.method))
                continue;
            return std::make_unique<Response>(makeResponse(route, request, matched));
        }
        return nullptr;
    };

    if (const auto response = inspect(router.externalRoutes))
        return *response;
    if (const auto response = inspect(router.rootRoutes))
        return *response;
    if (request.method == "OPTIONS" && !allowed.empty()) {
        std::string allow;
        for (const std::string &verb : allowed) {
            if (!allow.empty())
                allow += ", ";
            allow += verb;
        }
        Response response;
        response.status = 200;
        response.body = allow;
        addHeader(response, "Allow", allow);
        addHeader(response, "Content-Type", "text/plain");
        addHeader(response, "X-Content-Type-Options", "nosniff");
        return response;
    }
    return Response{404, "Not Found", {}, false};
}

} // namespace

} // namespace server1::http::router_detail

extern "C" {

void *server1_http_router_create()
{
    return new server1::http::router_detail::Router();
}

void server1_http_router_destroy(void *opaqueRouter)
{
    delete static_cast<server1::http::router_detail::Router *>(opaqueRouter);
}

int server1_http_router_add_static(void *opaqueRouter, int external, int prefix, const char *method,
                                   const char *pattern, int status, const char *body)
{
    if (opaqueRouter == nullptr || pattern == nullptr || method == nullptr || body == nullptr)
        return 0;
    auto &router = *static_cast<server1::http::router_detail::Router *>(opaqueRouter);
    server1::http::router_detail::Route route;
    route.method = server1::http::router_detail::upper(method);
    route.pattern = pattern;
    route.body = body;
    route.status = status;
    route.prefix = prefix != 0;
    if (status < 100 || status > 599
        || !server1::http::router_detail::compilePattern(route.pattern, route.prefix,
                                                         route.compiled, router.error))
        return 0;
    (external != 0 ? router.externalRoutes : router.rootRoutes).push_back(std::move(route));
    router.error.clear();
    return 1;
}

const char *server1_http_router_error(void *opaqueRouter)
{
    return opaqueRouter == nullptr
        ? "null router"
        : static_cast<server1::http::router_detail::Router *>(opaqueRouter)->error.c_str();
}

void *server1_http_router_dispatch(void *opaqueRouter, const char *method, const char *target,
                                   const char *body)
{
    if (opaqueRouter == nullptr || method == nullptr || target == nullptr || body == nullptr)
        return new server1::http::router_detail::Response{500, "Router unavailable", {}, true};
    const auto result = server1::http::router_detail::dispatch(
        *static_cast<server1::http::router_detail::Router *>(opaqueRouter), method, target, body);
    return new server1::http::router_detail::Response(result);
}

void server1_http_response_destroy(void *opaqueResponse)
{
    delete static_cast<server1::http::router_detail::Response *>(opaqueResponse);
}

int server1_http_response_status(void *opaqueResponse)
{
    return opaqueResponse == nullptr
        ? 500
        : static_cast<server1::http::router_detail::Response *>(opaqueResponse)->status;
}

const char *server1_http_response_body(void *opaqueResponse)
{
    return opaqueResponse == nullptr
        ? ""
        : static_cast<server1::http::router_detail::Response *>(opaqueResponse)->body.c_str();
}

const char *server1_http_response_header(void *opaqueResponse, const char *name)
{
    if (opaqueResponse == nullptr || name == nullptr)
        return server1::http::router_detail::kEmpty;
    const std::string wanted = server1::http::router_detail::lower(name);
    const auto &headers = static_cast<server1::http::router_detail::Response *>(opaqueResponse)->headers;
    for (const auto &header : headers) {
        if (server1::http::router_detail::lower(header.first) == wanted)
            return header.second.c_str();
    }
    return server1::http::router_detail::kEmpty;
}

int server1_http_response_close(void *opaqueResponse)
{
    return opaqueResponse != nullptr
        && static_cast<server1::http::router_detail::Response *>(opaqueResponse)->close;
}

std::size_t server1_http_response_header_count(void *opaqueResponse)
{
    return opaqueResponse == nullptr
        ? 0
        : static_cast<server1::http::router_detail::Response *>(opaqueResponse)->headers.size();
}

const char *server1_http_response_header_name_at(void *opaqueResponse, std::size_t index)
{
    if (opaqueResponse == nullptr)
        return server1::http::router_detail::kEmpty;
    const auto &headers = static_cast<server1::http::router_detail::Response *>(opaqueResponse)->headers;
    return index < headers.size() ? headers[index].first.c_str() : server1::http::router_detail::kEmpty;
}

const char *server1_http_response_header_value_at(void *opaqueResponse, std::size_t index)
{
    if (opaqueResponse == nullptr)
        return server1::http::router_detail::kEmpty;
    const auto &headers = static_cast<server1::http::router_detail::Response *>(opaqueResponse)->headers;
    return index < headers.size() ? headers[index].second.c_str() : server1::http::router_detail::kEmpty;
}

}
