#include "server1/policy/FileSelection.h"

#include "server1/policy/EngineLifecycle.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace server1::policy {
namespace {

// ---- JavaScript string helpers (ASCII semantics; see the ledger for
// non-ASCII) ----

bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isAlpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isWord(unsigned char c) { return isDigit(c) || isAlpha(c) || c == '_'; }
char lowerChar(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
char upperChar(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), lowerChar);
    return text;
}

bool equalsIgnoreCase(std::string_view text, std::size_t at, std::string_view word)
{
    if (at + word.size() > text.size())
        return false;
    for (std::size_t i = 0; i < word.size(); ++i)
        if (lowerChar(text[at + i]) != lowerChar(word[i]))
            return false;
    return true;
}

// String.prototype.split with a character-class separator.
std::vector<std::string> splitAny(std::string_view text, std::string_view separators)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (separators.find(c) != std::string_view::npos) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string join(const std::vector<std::string> &parts, std::string_view separator)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += separator;
        out += parts[i];
    }
    return out;
}

// String.prototype.slice with JavaScript index normalization.
std::string slice(const std::string &text, long long start, long long end)
{
    const long long size = static_cast<long long>(text.size());
    if (start < 0)
        start = std::max(size + start, 0LL);
    if (end < 0)
        end = std::max(size + end, 0LL);
    start = std::min(start, size);
    end = std::min(end, size);
    if (start >= end)
        return {};
    return text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

double parseIntText(const std::string &text)
{
    return jsParseInt(Value::string(text), 10);
}

bool isNaNText(const std::string &text)
{
    return std::isnan(jsNumber(Value::string(text)));
}

// Property order of a plain object, with delete and re-insertion.
class OrderedObject final {
public:
    void set(const std::string &key, Value value)
    {
        for (auto &entry : entries_)
            if (entry.first == key) {
                entry.second = std::move(value);
                return;
            }
        entries_.emplace_back(key, std::move(value));
    }
    void erase(const std::string &key)
    {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const auto &entry) { return entry.first == key; }),
                       entries_.end());
    }
    [[nodiscard]] bool has(const std::string &key) const
    {
        return std::any_of(entries_.begin(), entries_.end(),
                           [&](const auto &entry) { return entry.first == key; });
    }
    [[nodiscard]] Value get(const std::string &key) const
    {
        for (const auto &entry : entries_)
            if (entry.first == key)
                return entry.second;
        return Value::missing();
    }
    [[nodiscard]] Value value() const { return Value::object(entries_); }

private:
    Value::Object entries_;
};

// ---- the M304 regular expressions, as hand-written matchers ----

// /\b\d{4}\b/g, every match.
std::vector<std::string> fourDigitWords(const std::string &seg)
{
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p + 4 <= seg.size()) {
        const bool before = p == 0 || !isWord(static_cast<unsigned char>(seg[p - 1]));
        const bool digits = isDigit(seg[p]) && isDigit(seg[p + 1]) && isDigit(seg[p + 2])
            && isDigit(seg[p + 3]);
        const bool after = p + 4 == seg.size() || !isWord(static_cast<unsigned char>(seg[p + 4]));
        if (before && digits && after) {
            out.push_back(seg.substr(p, 4));
            p += 4;
        } else {
            ++p;
        }
    }
    return out;
}

bool isSep3(char c) { return c == '.' || c == '-' || c == ' '; }

// /(\d\d\d\d)(\.|-| )(\d\d)(\.|-| )(\d\d)/
bool airedMatch(const std::string &seg, std::string &year, std::string &month, std::string &day)
{
    for (std::size_t p = 0; p + 10 <= seg.size(); ++p) {
        const auto d = [&](std::size_t i) { return isDigit(seg[p + i]); };
        if (d(0) && d(1) && d(2) && d(3) && isSep3(seg[p + 4]) && d(5) && d(6) && isSep3(seg[p + 7])
            && d(8) && d(9)) {
            year = seg.substr(p, 4);
            month = seg.substr(p + 5, 2);
            day = seg.substr(p + 8, 2);
            return true;
        }
    }
    return false;
}

// /S(\d{1,2})/i
bool seasonToken(const std::string &x, std::string &group)
{
    for (std::size_t p = 0; p + 1 < x.size(); ++p) {
        if (lowerChar(x[p]) == 's' && isDigit(x[p + 1])) {
            group = x.substr(p + 1, (p + 2 < x.size() && isDigit(x[p + 2])) ? 2 : 1);
            return true;
        }
    }
    return false;
}

// /(?<=\W|\d)E(\d{2})/gi, every match.
std::vector<std::string> episodeTokens(const std::string &x)
{
    std::vector<std::string> out;
    std::size_t p = 1;
    while (p + 2 < x.size()) {
        const auto prev = static_cast<unsigned char>(x[p - 1]);
        if (lowerChar(x[p]) == 'e' && isDigit(x[p + 1]) && isDigit(x[p + 2])
            && (!isWord(prev) || isDigit(prev))) {
            out.push_back(x.substr(p, 3));
            p += 3;
        } else {
            ++p;
        }
    }
    return out;
}

// /(\d\d?)x(\d\d?)/i
bool xStamp(const std::string &x, std::string &season, std::string &episode)
{
    const auto n = x.size();
    for (std::size_t p = 0; p < n; ++p) {
        for (const std::size_t first : {2u, 1u}) {
            if (p + first > n)
                continue;
            bool ok = true;
            for (std::size_t i = 0; i < first; ++i)
                ok = ok && isDigit(x[p + i]);
            if (!ok)
                continue;
            const auto at = p + first;
            if (at >= n || lowerChar(x[at]) != 'x')
                continue;
            if (at + 1 < n && isDigit(x[at + 1])) {
                season = x.substr(p, first);
                episode = x.substr(at + 1, (at + 2 < n && isDigit(x[at + 2])) ? 2 : 1);
                return true;
            }
        }
    }
    return false;
}

bool fullMatchClass(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return isAlpha(u) || isDigit(u) || (u >= 0x2C && u <= 0x3F) || c == '!' || c == '\''
        || c == '&' || c == ' ';
}

// seg.replace(/\.| |;|_/g, " ").match(/^([a-zA-Z0-9,-?!'& ]*) S(\d{1,2})E(\d{2})/i)[1]
bool seriesNameMatch(const std::string &segIn, std::string &group)
{
    std::string seg = segIn;
    for (auto &c : seg)
        if (c == '.' || c == ';' || c == '_')
            c = ' ';
    std::size_t run = 0;
    while (run < seg.size() && fullMatchClass(seg[run]))
        ++run;
    // Greedy group 1, backtracking from the longest prefix.
    for (std::size_t k = run + 1; k-- > 0;) {
        if (k >= seg.size() || seg[k] != ' ' || k + 1 >= seg.size() || lowerChar(seg[k + 1]) != 's')
            continue;
        const std::size_t q = k + 2;
        for (const std::size_t digits : {2u, 1u}) {
            bool ok = q + digits <= seg.size();
            for (std::size_t i = 0; ok && i < digits; ++i)
                ok = isDigit(seg[q + i]);
            if (!ok)
                continue;
            const std::size_t r = q + digits;
            if (r + 2 < seg.size() && lowerChar(seg[r]) == 'e' && isDigit(seg[r + 1])
                && isDigit(seg[r + 2])) {
                group = seg.substr(0, k);
                return true;
            }
        }
    }
    return false;
}

// /[^\da-zA-Z](\d\d?)\.(\d\d?)[^\da-zA-Z]/i
bool dotStamp(const std::string &seg, std::string &season, std::string &episode)
{
    const auto n = seg.size();
    const auto alnum = [&](std::size_t i) {
        return isDigit(seg[i]) || isAlpha(static_cast<unsigned char>(seg[i]));
    };
    for (std::size_t p = 0; p < n; ++p) {
        if (alnum(p))
            continue;
        for (const std::size_t a : {2u, 1u}) {
            std::size_t q = p + 1;
            bool ok = q + a <= n;
            for (std::size_t i = 0; ok && i < a; ++i)
                ok = isDigit(seg[q + i]);
            if (!ok)
                continue;
            q += a;
            if (q >= n || seg[q] != '.')
                continue;
            ++q;
            for (const std::size_t b : {2u, 1u}) {
                bool ok2 = q + b <= n;
                for (std::size_t i = 0; ok2 && i < b; ++i)
                    ok2 = isDigit(seg[q + i]);
                if (!ok2 || q + b >= n || alnum(q + b))
                    continue;
                season = seg.substr(p + 1, a);
                episode = seg.substr(q, b);
                return true;
            }
        }
    }
    return false;
}

// /\d\d\d\d?e/
bool stampTrailingE(const std::string &x)
{
    for (std::size_t p = 0; p + 3 < x.size(); ++p) {
        if (!(isDigit(x[p]) && isDigit(x[p + 1]) && isDigit(x[p + 2])))
            continue;
        if (x[p + 3] == 'e')
            return true;
        if (p + 4 < x.size() && isDigit(x[p + 3]) && x[p + 4] == 'e')
            return true;
    }
    return false;
}

// /s\d\d\d\d?/
bool stampLeadingS(const std::string &x)
{
    for (std::size_t p = 0; p + 3 < x.size(); ++p)
        if (x[p] == 's' && isDigit(x[p + 1]) && isDigit(x[p + 2]) && isDigit(x[p + 3]))
            return true;
    return false;
}

// /season(\.| )?(\d{1,2})/gi, first match: the digits.
bool seasonWord(const std::string &text, std::string &digits)
{
    for (std::size_t p = 0; p < text.size(); ++p) {
        if (!equalsIgnoreCase(text, p, "season"))
            continue;
        const auto after = p + 6;
        for (const bool separator : {true, false}) {
            std::size_t q = after;
            if (separator) {
                if (q >= text.size() || (text[q] != '.' && text[q] != ' '))
                    continue;
                ++q;
            }
            if (q < text.size() && isDigit(text[q])) {
                digits = text.substr(q, (q + 1 < text.size() && isDigit(text[q + 1])) ? 2 : 1);
                return true;
            }
        }
    }
    return false;
}

// /Season (\d{1,2}) - (\d{1,2})/i
bool seasonDashEpisode(const std::string &text, std::string &season, std::string &episode)
{
    for (std::size_t p = 0; p < text.size(); ++p) {
        if (!equalsIgnoreCase(text, p, "season "))
            continue;
        const auto q = p + 7;
        for (const std::size_t a : {2u, 1u}) {
            bool ok = q + a <= text.size();
            for (std::size_t i = 0; ok && i < a; ++i)
                ok = isDigit(text[q + i]);
            if (!ok || text.compare(q + a, 3, " - ") != 0)
                continue;
            const auto r = q + a + 3;
            if (r < text.size() && isDigit(text[r])) {
                season = text.substr(q, a);
                episode = text.substr(r, (r + 1 < text.size() && isDigit(text[r + 1])) ? 2 : 1);
                return true;
            }
        }
    }
    return false;
}

// /ep(isode)?(\.| )?(\d+)/gi, first match: the digits.
bool episodeWord(const std::string &text, std::string &digits)
{
    for (std::size_t p = 0; p < text.size(); ++p) {
        if (!equalsIgnoreCase(text, p, "ep"))
            continue;
        for (const bool isode : {true, false}) {
            std::size_t q = p + 2;
            if (isode) {
                if (!equalsIgnoreCase(text, q, "isode"))
                    continue;
                q += 5;
            }
            for (const bool separator : {true, false}) {
                std::size_t r = q;
                if (separator) {
                    if (r >= text.size() || (text[r] != '.' && text[r] != ' '))
                        continue;
                    ++r;
                }
                if (r < text.size() && isDigit(text[r])) {
                    std::size_t e = r;
                    while (e < text.size() && isDigit(text[e]))
                        ++e;
                    digits = text.substr(r, e - r);
                    return true;
                }
            }
        }
    }
    return false;
}

bool isDiskSep(char c) { return c == ' ' || c == '_' || c == '.' || c == '-'; }

// /[ _.-]*(?:cd|dvd|p(?:ar)?t|dis[ck]|d)[ _.-]*(\d)[^\d]/
bool diskNumber(const std::string &seg, std::string &digit)
{
    static const char *const words[] = {"cd", "dvd", "part", "pt", "disc", "disk", "d"};
    for (std::size_t k = 0; k < seg.size(); ++k) {
        for (const char *word : words) {
            const std::string_view w(word);
            if (seg.compare(k, w.size(), w) != 0)
                continue;
            std::size_t q = k + w.size();
            while (q < seg.size() && isDiskSep(seg[q]))
                ++q;
            if (q + 1 < seg.size() && isDigit(seg[q]) && !isDigit(seg[q + 1])) {
                digit = seg.substr(q, 1);
                return true;
            }
        }
    }
    return false;
}

bool nameWord(const std::string &word)
{
    return std::all_of(word.begin(), word.end(), [](char c) {
        return isAlpha(static_cast<unsigned char>(c)) || c == ',' || c == '?' || c == '!'
            || c == '\'' || c == '&';
    });
}

bool excludedWord(const std::string &word)
{
    static const char *const keywords[] = {"1080p", "720p", "480p", "blurayrip", "brrip", "divx",
                                           "dvdrip", "hdrip", "hdtv", "tvrip", "xvid", "camrip"};
    const auto w = lower(word);
    for (const char *keyword : keywords)
        if (w == keyword)
            return true;
    // excluded is a plain object: inherited Object.prototype names hit too.
    return w == "constructor" || w == "__proto__";
}

bool mediaExtension(const std::string &path)
{
    static const char *const extensions[] = {"mkv", "avi", "mp4", "wmv", "vp8", "mov", "mpg", "ts",
                                             "m3u8", "webm", "flac", "mp3", "wav", "wma", "aac",
                                             "ogg"};
    for (const char *extension : extensions) {
        const std::string e(extension);
        if (path.size() < e.size() + 1)
            continue;
        const auto at = path.size() - e.size();
        if (!equalsIgnoreCase(path, at, e))
            continue;
        // "." before the extension is any character but a line terminator.
        const auto before = static_cast<unsigned char>(path[at - 1]);
        if (before == '\n' || before == '\r')
            continue;
        if ((before == 0xA8 || before == 0xA9) && at >= 3
            && static_cast<unsigned char>(path[at - 3]) == 0xE2
            && static_cast<unsigned char>(path[at - 2]) == 0x80)
            continue;
        return true;
    }
    return false;
}

std::string jsTrim(const std::string &text)
{
    const auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && space(text[a]))
        ++a;
    while (b > a && space(text[b - 1]))
        --b;
    return text.substr(a, b - a);
}

Value propertyOf(const Value &value, std::string_view key)
{
    if (value.kind() != Value::Kind::Object)
        return Value::missing();
    const auto *found = value.find(key);
    return found ? *found : Value::missing();
}

bool strictEquals(const Value &a, const Value &b)
{
    if (a.kind() != b.kind())
        return false;
    switch (a.kind()) {
    case Value::Kind::Number:
        return a.asNumber() == b.asNumber();
    case Value::Kind::String:
        return a.asString() == b.asString();
    case Value::Kind::Boolean:
        return a.asBoolean() == b.asBoolean();
    case Value::Kind::Null:
    case Value::Kind::Missing:
        return true;
    default:
        return false;
    }
}

} // namespace

// ---- M304 ----

Value parseVideoName(std::string_view filePathView, const Value &optionsIn)
{
    const Value options = jsTruthy(optionsIn) ? optionsIn : Value::object({});
    const std::string filePath(filePathView);
    std::string slashed = filePath;
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    auto all = splitAny(slashed, "/");
    std::reverse(all.begin(), all.end());
    std::vector<std::string> segments;
    for (const auto &segment : all)
        if (!segment.empty() && segments.size() < 3)
            segments.push_back(segment);
    if (segments.empty())
        return Value::missing(); // segments[0].split: TypeError
    auto firstNameSplit = splitAny(segments[0], ". _");
    const std::string *s1 = segments.size() > 1 ? &segments[1] : nullptr;

    OrderedObject meta;
    const auto numberOf = [&](const std::string &key) {
        const auto value = meta.get(key);
        return value.kind() == Value::Kind::Number ? value.asNumber() : std::nan("");
    };
    const auto saneSeason = [&]() { return meta.has("season") && !std::isnan(jsNumber(meta.get("season"))); };
    const auto saneEpisode = [&]() {
        const auto episode = meta.get("episode");
        return episode.kind() == Value::Kind::Array && !episode.asArray().empty();
    };
    const auto nameTruthy = [&]() { return jsTruthy(meta.get("name")); };

    std::vector<const std::string *> firstTwo{&segments[0]};
    if (s1)
        firstTwo.push_back(s1);
    for (const auto *seg : firstTwo)
        for (const auto &word : fourDigitWords(*seg)) {
            const auto number = parseIntText(word);
            if (number >= 1900 && number <= 2030)
                meta.set("year", Value::number(number));
        }
    for (const auto *seg : firstTwo) {
        std::string year;
        std::string month;
        std::string day;
        if (airedMatch(*seg, year, month, day)) {
            const auto number = parseIntText(year);
            if (number >= 1900 && number <= 2030)
                meta.set("aired", Value::string(jsNumberToString(number) + "-" + month + "-" + day));
        }
    }
    for (const auto *seg : firstTwo) {
        for (const auto &x : splitAny(*seg, ". _")) {
            std::string group;
            if (seasonToken(x, group))
                meta.set("season", Value::number(parseIntText(group)));
            const auto episodes = episodeTokens(x);
            if (!episodes.empty()) {
                Value::Array list;
                for (const auto &token : episodes)
                    list.push_back(Value::number(parseIntText(token.substr(1))));
                meta.set("episode", Value::array(std::move(list)));
            }
            std::string season;
            std::string episode;
            if (xStamp(x, season, episode)) {
                meta.set("season", Value::number(parseIntText(season)));
                meta.set("episode", Value::array({Value::number(parseIntText(episode))}));
            }
        }
        std::string group;
        if (!nameTruthy() && jsTruthy(meta.get("season")) && jsTruthy(meta.get("episode"))
            && seriesNameMatch(*seg, group) && !group.empty())
            meta.set("name", Value::string(group));
    }
    {
        std::string season;
        std::string episode;
        const bool dot = dotStamp(segments[0], season, episode);
        if (!(saneSeason() && saneEpisode()) && dot && !jsTruthy(meta.get("year"))) {
            meta.set("season", Value::number(parseIntText(season)));
            meta.set("episode", Value::array({Value::number(parseIntText(episode))}));
        }
    }
    if (!(saneSeason() && saneEpisode()) && !jsTruthy(propertyOf(options, "strict"))) {
        std::reverse(firstNameSplit.begin(), firstNameSplit.end()); // in place, as the source
        std::optional<std::string> stamp;
        for (auto x : firstNameSplit) {
            if (stampTrailingE(x))
                x = slice(x, 0, -1);
            if (stampLeadingS(x))
                x = slice(x, 1, static_cast<long long>(x.size()));
            if (!isNaNText(x) && (x.size() == 3 || (!jsTruthy(meta.get("year")) && x.size() == 4))) {
                stamp = x;
                break;
            }
        }
        if (stamp) {
            bool accept = !jsTruthy(meta.get("year"));
            if (!accept) {
                const auto indexOf = [&](const std::string &value) {
                    const auto found = std::find(firstNameSplit.begin(), firstNameSplit.end(), value);
                    return found == firstNameSplit.end() ? -1L
                                                         : static_cast<long>(found - firstNameSplit.begin());
                };
                accept = indexOf(*stamp) < indexOf(jsNumberToString(numberOf("year")));
            }
            if (accept) {
                meta.set("episode", Value::array({Value::number(parseIntText(slice(*stamp, -2, static_cast<long long>(stamp->size()))))}));
                meta.set("season", Value::number(parseIntText(slice(*stamp, 0, -2))));
            }
        }
    }
    const auto joined = join(segments, "/");
    if (!saneSeason()) {
        std::string digits;
        if (seasonWord(joined, digits))
            meta.set("season", Value::number(parseIntText(digits)));
        std::string season;
        std::string episode;
        if (seasonDashEpisode(joined, season, episode)) {
            meta.set("season", Value::number(parseIntText(season)));
            meta.set("episode", Value::array({Value::number(parseIntText(episode))}));
        }
    }
    if (!saneEpisode()) {
        std::string digits;
        if (episodeWord(joined, digits))
            meta.set("episode", Value::array({Value::number(parseIntText(digits))}));
    }
    {
        std::string digit;
        if (diskNumber(segments[0], digit))
            meta.set("diskNumber", Value::number(parseIntText(digit)));
    }

    bool isSample = false;
    std::vector<std::string> order = segments;
    if (!jsTruthy(propertyOf(options, "fromInside")))
        std::reverse(order.begin(), order.end());
    for (auto seg : order) {
        if (seg == segments[0]) {
            auto dotted = splitAny(seg, ".");
            dotted.pop_back();
            seg = join(dotted, ".");
            if (seg.size() >= 2 && seg[0] == '[') {
                const auto close = seg.find(']', 1);
                if (close != std::string::npos)
                    seg = seg.substr(close + 1);
            }
        }
        const auto bracket = seg.find('[');
        if (bracket != std::string::npos)
            seg = seg.substr(0, bracket);
        const auto segSplit = splitAny(seg, ". -;_");
        std::vector<std::string> nameParts;
        isSample = isSample || equalsIgnoreCase(seg, 0, "sample") || equalsIgnoreCase(seg, 0, "etrg");
        if (!nameTruthy()) {
            for (std::size_t i = 0; i < segSplit.size(); ++i) {
                const auto &word = segSplit[i];
                const bool numeric = !isNaNText(word);
                const bool keep = nameWord(word) || (numeric && word.size() <= 2) || (numeric && i == 0);
                const auto w = lower(word);
                const bool marker = (w == "ep" || w == "episode" || w == "season")
                    && i + 1 < segSplit.size() && !isNaNText(segSplit[i + 1]);
                if (!keep || excludedWord(word) || marker)
                    break;
                nameParts.push_back(word);
            }
            if (nameParts.size() != 1 || isNaNText(nameParts[0])) {
                std::vector<std::string> words;
                for (const auto &part : nameParts)
                    if (!part.empty()) {
                        std::string word(1, upperChar(part[0]));
                        word += lower(part.substr(1));
                        words.push_back(word);
                    }
                meta.set("name", Value::string(join(words, " ")));
            }
        }
    }
    isSample = isSample || lower(s1 ? *s1 : std::string{}) == "sample";
    bool canBeMovie = false;
    if (jsTruthy(propertyOf(options, "strict"))) {
        canBeMovie = meta.has("year");
    } else {
        canBeMovie = meta.has("year") || meta.has("diskNumber");
        const auto lowered = lower(joined);
        for (const char *keyword : {"1080p", "720p", "480p", "blurayrip", "brrip", "divx", "dvdrip",
                                    "hdrip", "hdtv", "tvrip", "xvid", "camrip"})
            canBeMovie = canBeMovie || lowered.find(keyword) != std::string::npos;
    }
    if (nameTruthy() && jsTruthy(meta.get("aired")))
        meta.set("type", Value::string("series"));
    const auto type = [&]() {
        const auto value = meta.get("type");
        return value.kind() == Value::Kind::String ? value.asString() : std::string{};
    };
    if (nameTruthy() && saneSeason() && saneEpisode())
        meta.set("type", Value::string("series"));
    else if (nameTruthy() && canBeMovie)
        meta.set("type", Value::string("movie"));
    else if (type() != "movie" && nameTruthy() && saneSeason())
        meta.set("type", Value::string("extras"));
    else
        meta.set("type", Value::string("other"));
    const auto fileLength = propertyOf(options, "fileLength");
    if (jsTruthy(fileLength)) {
        const bool movie = type().find("movie") != std::string::npos;
        const bool series = type().find("series") != std::string::npos;
        if (jsNumber(fileLength) < 1024.0 * (movie ? 80 : 50) * 1024 && (movie || series) && !isSample)
            meta.set("type", Value::string("other"));
    }
    if (type() != "series" || jsTruthy(meta.get("aired"))) {
        meta.erase("episode");
        meta.erase("season");
    }
    if (type() == "series" && jsTruthy(meta.get("year"))) {
        if (!jsTruthy(meta.get("aired")))
            meta.set("aired", meta.get("year"));
        else
            meta.set("aired", meta.get("aired"));
        meta.erase("year");
    }
    meta.set("type", Value::string(type() + (isSample ? "-sample" : "")));
    const auto name = meta.get("name");
    if (!jsTruthy(name)) {
        // meta.name = meta.name && ...: undefined stays an (invisible) key.
        meta.set("name", name.kind() == Value::Kind::String ? name : Value::missing());
    } else {
        std::string text = jsTrim(lower(name.asString()));
        const auto open = text.rfind('(');
        if (open != std::string::npos && text.size() >= open + 3 && text.back() == ')')
            text = text.substr(0, open);
        std::string replaced;
        for (const char c : text) {
            if (c == '&')
                replaced += "and";
            else
                replaced.push_back(c);
        }
        std::string cleaned;
        bool inRun = false;
        for (const char c : replaced) {
            const bool keep = isDigit(c) || (c >= 'a' && c <= 'z') || c == ' ';
            if (keep) {
                cleaned.push_back(c);
                inRun = false;
            } else if (!inRun) {
                cleaned.push_back(' ');
                inRun = true;
            }
        }
        std::vector<std::string> words;
        for (const auto &word : splitAny(cleaned, " "))
            if (!word.empty())
                words.push_back(word);
        meta.set("name", Value::string(join(words, " ")));
    }
    const auto hints = propertyOf(options, "hints");
    if (jsTruthy(hints) && jsTruthy(propertyOf(hints, "imdb_id")))
        meta.set("imdb_id", propertyOf(hints, "imdb_id"));
    Value::Array tags;
    const auto lowered = lower(filePath);
    if (lowered.find("1080p") != std::string::npos) {
        tags.push_back(Value::string("hd"));
        tags.push_back(Value::string("1080p"));
    }
    if (lowered.find("720p") != std::string::npos)
        tags.push_back(Value::string("720p"));
    if (lowered.find("480p") != std::string::npos)
        tags.push_back(Value::string("480p"));
    if (isSample)
        tags.push_back(Value::string("sample"));
    meta.set("tag", Value::array(std::move(tags)));
    return meta.value();
}

// ---- M663 ----

double guessFileIdx(const Value &files, const Value &seriesInfoIn)
{
    if (files.kind() != Value::Kind::Array || !jsTruthy(seriesInfoIn))
        return -1;
    const auto &list = files.asArray();
    std::vector<std::size_t> media;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto path = propertyOf(list[i], "path");
        if (path.kind() == Value::Kind::String && mediaExtension(path.asString()))
            media.push_back(i);
    }
    if (media.empty())
        return -1;
    const auto season = propertyOf(seriesInfoIn, "season");
    const auto episode = propertyOf(seriesInfoIn, "episode");
    const bool series = jsTruthy(season) && jsTruthy(episode);
    std::vector<std::size_t> forEpisode;
    if (series) {
        for (const auto index : media) {
            const auto info = parseVideoName(propertyOf(list[index], "path").asString());
            if (info.isMissing()) { // parseVideoName threw: the filter returns -1 (truthy)
                forEpisode.push_back(index);
                continue;
            }
            const auto infoSeason = propertyOf(info, "season");
            const auto infoEpisode = propertyOf(info, "episode");
            if (infoSeason.kind() != Value::Kind::Null && std::isfinite(jsNumber(infoSeason))
                && strictEquals(infoSeason, season) && infoEpisode.kind() == Value::Kind::Array
                && std::any_of(infoEpisode.asArray().begin(), infoEpisode.asArray().end(),
                               [&](const Value &e) { return strictEquals(e, episode); }))
                forEpisode.push_back(index);
        }
    }
    const auto &pool = forEpisode.empty() ? media : forEpisode;
    std::optional<std::size_t> selected;
    for (const auto index : pool) {
        if (!selected
            || jsNumber(propertyOf(list[index], "length")) > jsNumber(propertyOf(list[*selected], "length")))
            selected = index;
    }
    return selected ? static_cast<double>(*selected) : -1;
}

// ---- JavaScript RegExp over std::regex ----

struct JsRegExp::Compiled final {
    std::regex regex;
    bool sticky = false;
};

std::optional<JsRegExp> JsRegExp::compile(std::string pattern, std::string flags)
{
    // Flag validity as in the oracle's Node: d g i m s u v y, each once, and
    // u and v are exclusive.
    const std::string valid = "dgimsuvy";
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (valid.find(flags[i]) == std::string::npos || flags.find(flags[i], i + 1) != std::string::npos)
            return std::nullopt;
    }
    if (flags.find('u') != std::string::npos && flags.find('v') != std::string::npos)
        return std::nullopt;
    JsRegExp result;
    result.pattern_ = pattern;
    result.flags_ = flags;
    std::string source = pattern;
    if (flags.find('s') != std::string::npos) {
        // dotAll: an unescaped "." outside a class matches any character.
        std::string rewritten;
        bool inClass = false;
        for (std::size_t i = 0; i < source.size(); ++i) {
            const char c = source[i];
            if (c == '\\' && i + 1 < source.size()) {
                rewritten += source.substr(i, 2);
                ++i;
                continue;
            }
            if (c == '[')
                inClass = true;
            else if (c == ']')
                inClass = false;
            rewritten += (c == '.' && !inClass) ? std::string("[\\s\\S]") : std::string(1, c);
        }
        source = rewritten;
    }
    auto syntax = std::regex_constants::ECMAScript;
    if (flags.find('i') != std::string::npos)
        syntax |= std::regex_constants::icase;
    // 'm' changes ^ and $ only at line breaks; MSVC's std::regex has no
    // multiline flag, and torrent file names carry no line breaks.
    try {
        auto compiled = std::make_shared<Compiled>();
        compiled->regex = std::regex(source, syntax);
        compiled->sticky = flags.find('y') != std::string::npos;
        result.compiled_ = std::move(compiled);
        result.evaluable_ = true;
    } catch (const std::regex_error &) {
        // JavaScript would throw a SyntaxError for most such patterns; a
        // JS-only construct (lookbehind, named groups) lands here too.
        return std::nullopt;
    }
    return result;
}

bool JsRegExp::test(std::string_view text) const
{
    if (!compiled_)
        return false;
    try {
        const std::string subject(text);
        if (compiled_->sticky)
            return std::regex_search(subject, compiled_->regex, std::regex_constants::match_continuous);
        return std::regex_search(subject, compiled_->regex);
    } catch (const std::regex_error &) {
        return false; // M80: an evaluation that does not finish counts as no match
    }
}

// ---- M172 fileMustInclude ----

FileMustIncludeResult matchFileMustInclude(const Value &alternatives, const Value &files)
{
    FileMustIncludeResult result;
    // (body.fileMustInclude || []).length, then .map.
    if (!jsTruthy(alternatives))
        return result;
    if (alternatives.kind() == Value::Kind::String) {
        result.error = "TypeError";
        return result;
    }
    if (alternatives.kind() != Value::Kind::Array) {
        if (jsTruthy(propertyOf(alternatives, "length")))
            result.error = "TypeError";
        return result;
    }
    if (alternatives.asArray().empty() || files.kind() != Value::Kind::Array)
        return result;

    // Each element after the map: a RegExp, or the element itself.
    struct Mapped final {
        Value element;
        std::optional<JsRegExp> regex;
    };
    std::vector<Mapped> mapped;
    for (const auto &element : alternatives.asArray()) {
        Mapped entry{element, std::nullopt};
        if (jsTruthy(element)) {
            if (element.kind() != Value::Kind::String) {
                result.error = "TypeError"; // (el || "").match is not a function
                return result;
            }
            const auto &text = element.asString();
            // /^\/(.*)\/(.*)$/ with "." excluding line terminators.
            if (text.size() >= 2 && text.front() == '/' && text.find('\n') == std::string::npos
                && text.find('\r') == std::string::npos) {
                const auto last = text.rfind('/');
                if (last > 0)
                    entry.regex = JsRegExp::compile(text.substr(1, last - 1), text.substr(last + 1));
            }
        }
        mapped.push_back(std::move(entry));
    }

    const auto &list = files.asArray();
    for (std::size_t idx = 0; idx < list.size(); ++idx) {
        const auto name = propertyOf(list[idx], "name");
        const std::string subject = name.kind() == Value::Kind::String ? name.asString()
                                                                       : jsNumberToString(jsNumber(name));
        for (const auto &entry : mapped) {
            bool matched = false;
            if (entry.regex) {
                matched = entry.regex->test(subject);
            } else if (entry.element.kind() == Value::Kind::String) {
                const auto regex = JsRegExp::compile(entry.element.asString());
                if (!regex) {
                    result.error = "SyntaxError";
                    return result;
                }
                matched = regex->test(subject);
            } else {
                // str.match(null | false | 0) builds new RegExp(String(el)).
                std::string text = "null";
                if (entry.element.kind() == Value::Kind::Boolean)
                    text = entry.element.asBoolean() ? "true" : "false";
                else if (entry.element.kind() == Value::Kind::Number)
                    text = jsNumberToString(entry.element.asNumber());
                matched = subject.find(text) != std::string::npos;
            }
            if (!matched)
                continue;
            result.index = idx;
            // find() returns the element; a falsy element does not stop the
            // outer search, so a later match overwrites the index.
            if (jsTruthy(entry.element))
                return result;
            break;
        }
    }
    return result;
}

CreateSelection createRouteSelection(const Value &body, const Value &files)
{
    CreateSelection selection;
    if (!jsTruthy(files)) // if (engineStats.files)
        return selection;
    const auto alternatives = propertyOf(body, "fileMustInclude");
    const auto match = matchFileMustInclude(alternatives, files);
    if (match.error) {
        selection.error = match.error;
        return selection;
    }
    if (match.index)
        selection.guessedFileIdx = Value::number(static_cast<double>(*match.index));
    const auto seriesInfo = propertyOf(body, "guessFileIdx");
    if (jsTruthy(seriesInfo) && selection.guessedFileIdx.isMissing())
        selection.guessedFileIdx = Value::number(guessFileIdx(files, seriesInfo));
    return selection;
}

} // namespace server1::policy
