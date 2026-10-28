#include "server1/http/GuessFileIndex.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace server1::http {
namespace {
using policy::Value;

// Only source-owned, fixed regexes are used here. The patterns spell out ASCII
// case pairs rather than PCRE Unicode case folding, use ASCII digits/words,
// use \z for ECMAScript's non-multiline $, and exclude all four ECMAScript
// line terminators where the source has a dot. No user regex enters this port.
const QRegularExpression &regex(const char *pattern)
{
    thread_local std::map<std::string, QRegularExpression> expressions;
    const auto found = expressions.find(pattern);
    if (found != expressions.end()) return found->second;
    auto result = expressions.emplace(pattern, QRegularExpression(QString::fromLatin1(pattern)));
    if (!result.first->second.isValid()) throw std::logic_error("invalid internal media-name regular expression");
    return result.first->second;
}

QString utf8(const std::string &value)
{
    // A leading U+FEFF belongs to the JS string; QString::fromUtf8 drops it.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInitialBom);
    return decoder.decode(QByteArrayView(value.data(), static_cast<qsizetype>(value.size())));
}

bool test(const QString &text, const char *pattern)
{
    return regex(pattern).match(text).hasMatch();
}

bool jsSpace(QChar ch)
{
    const auto c = ch.unicode();
    return c == 0x0009 || c == 0x000a || c == 0x000b || c == 0x000c || c == 0x000d
        || c == 0x0020 || c == 0x00a0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
        || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000
        || c == 0xfeff;
}

QString jsTrim(QString text)
{
    qsizetype first = 0, last = text.size();
    while (first < last && jsSpace(text[first])) ++first;
    while (last > first && jsSpace(text[last - 1])) --last;
    return text.mid(first, last - first);
}

// The common bounded conversion helper intentionally excludes compound values
// and accepts strtod extensions. M304's isNaN and M663's > require JS's actual
// string numeric grammar, including whitespace and unsigned radix prefixes.
double stringNumber(QString text)
{
    text = jsTrim(std::move(text));
    if (text.isEmpty()) return 0;
    if (text == "Infinity" || text == "+Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
    static const auto decimal = regex("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?\\z");
    if (decimal.match(text).hasMatch()) {
        bool ok = false;
        const auto result = text.toDouble(&ok);
        // Qt reports overflow/underflow through ok but still returns the value.
        return result;
    }
    if (text.size() > 2 && text[0] == '0') {
        const auto kind = text[1].toLower();
        const int base = kind == 'x' ? 16 : kind == 'o' ? 8 : kind == 'b' ? 2 : 0;
        if (base) {
            std::uint64_t significand = 0;
            qsizetype bits = 0;
            bool guard = false, sticky = false;
            const int digitBits = base == 16 ? 4 : base == 8 ? 3 : 1;
            for (qsizetype i = 2; i < text.size(); ++i) {
                const auto c = text[i].toLower().unicode();
                const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0 || digit >= base) return std::numeric_limits<double>::quiet_NaN();
                for (int bit = digitBits - 1; bit >= 0; --bit) {
                    const bool set = (digit & (1 << bit)) != 0;
                    if (!bits && !set) continue;
                    ++bits;
                    if (bits <= 53) significand = significand * 2 + set;
                    else if (bits == 54) guard = set;
                    else sticky = sticky || set;
                }
            }
            // Round once to binary64, ties-to-even, without long-double double
            // rounding or fixed-width integer overflow on long radix strings.
            if (guard && (sticky || (significand & 1))) ++significand;
            if (bits <= 53) return static_cast<double>(significand);
            return std::ldexp(static_cast<double>(significand), static_cast<int>(std::min<qsizetype>(bits - 53, 2048)));
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double decimalPrefix(QString text)
{
    text = jsTrim(std::move(text));
    const auto match = regex("^[+-]?[0-9]+").match(text);
    if (!match.hasMatch()) return std::numeric_limits<double>::quiet_NaN();
    return stringNumber(match.captured());
}

Value field(const Value &value, const char *key)
{
    if (value.isMissing() || value.isNull())
        throw std::invalid_argument(std::string("Cannot read properties of ")
                                    + (value.isNull() ? "null" : "undefined") + " (reading '" + key + "')");
    if (const auto *found = value.find(key)) return *found;
    if (std::string_view(key) == "length") {
        if (value.kind() == Value::Kind::String)
            return Value::number(utf8(value.asString()).size());
        if (value.kind() == Value::Kind::Array) return Value::number(value.asArray().size());
        if (value.kind() == Value::Kind::Bytes) return Value::number(value.asBytes().size());
    }
    return Value::missing();
}

QString primitiveString(const Value &value);

QString numberString(double value)
{
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0) return "0";
    char buffer[128];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), std::abs(value), std::chars_format::general);
    if (converted.ec != std::errc()) throw std::logic_error("number formatting failed");
    auto shortest = QString::fromLatin1(buffer, converted.ptr - buffer);
    int exponent = 0;
    const auto e = shortest.indexOf('e');
    if (e >= 0) {
        exponent = shortest.mid(e + 1).toInt();
        shortest = shortest.left(e);
    }
    const auto dot = shortest.indexOf('.');
    const int decimalPosition = static_cast<int>(dot >= 0 ? dot : shortest.size()) + exponent;
    shortest.remove('.');
    QString result;
    // ECMAScript uses fixed notation for [1e-6, 1e21), independently of the
    // shortest-round-trip formatter's choice of fixed/scientific notation.
    if (decimalPosition > 0 && decimalPosition <= 21) {
        result = shortest;
        if (decimalPosition >= result.size()) result += QString(decimalPosition - result.size(), '0');
        else result.insert(decimalPosition, '.');
    } else if (decimalPosition <= 0 && decimalPosition > -6) {
        result = "0." + QString(-decimalPosition, '0') + shortest;
    } else {
        result = shortest.left(1);
        if (shortest.size() > 1) result += '.' + shortest.mid(1);
        const int power = decimalPosition - 1;
        result += 'e';
        if (power >= 0) result += '+';
        result += QString::number(power);
    }
    return value < 0 ? '-' + result : result;
}

Value primitive(const Value &value)
{
    if (value.kind() == Value::Kind::Object) {
        // An own, non-callable toString masks Object.prototype.toString.
        if (value.find("toString"))
            throw std::invalid_argument("Cannot convert object to primitive value");
        return Value::string("[object Object]");
    }
    if (value.kind() == Value::Kind::Array) {
        QStringList elements;
        for (const auto &element : value.asArray())
            elements.push_back(element.isMissing() || element.isNull() ? QString() : primitiveString(element));
        return Value::string(elements.join(',').toStdString());
    }
    if (value.kind() == Value::Kind::Bytes)
        return Value::string(policy::bytesToString(value.asBytes()));
    return value;
}

QString primitiveString(const Value &input)
{
    const auto value = primitive(input);
    switch (value.kind()) {
    case Value::Kind::Missing: return "undefined";
    case Value::Kind::Null: return "null";
    case Value::Kind::Boolean: return value.asBoolean() ? "true" : "false";
    case Value::Kind::Number: return numberString(value.asNumber());
    case Value::Kind::String: return utf8(value.asString());
    default: throw std::logic_error("primitive conversion did not produce a primitive");
    }
}

double number(const Value &value)
{
    if (value.kind() == Value::Kind::String) return stringNumber(utf8(value.asString()));
    return policy::jsNumber(value);
}

bool greater(const Value &left, const Value &right)
{
    const auto a = primitive(left), b = primitive(right);
    if (a.kind() == Value::Kind::String && b.kind() == Value::Kind::String)
        return QString::compare(utf8(a.asString()), utf8(b.asString())) > 0;
    return number(a) > number(b);
}

Value stringValue(const QString &value) { return Value::string(value.toStdString()); }

bool numericEqual(const Value &value, double expected)
{
    return value.kind() == Value::Kind::Number && value.asNumber() == expected;
}

const QStringList movieKeywords = {"1080p", "720p", "480p", "blurayrip", "brrip", "divx",
                                  "dvdrip", "hdrip", "hdtv", "tvrip", "xvid", "camrip"};
} // namespace

Value parseVideoName(const std::string &filePath)
{
    const QString path = utf8(filePath);
    auto segments = QString(path).replace('\\', '/').split('/', Qt::SkipEmptyParts);
    std::reverse(segments.begin(), segments.end());
    if (segments.size() > 3) segments = segments.mid(0, 3);
    if (segments.empty()) throw std::invalid_argument("Cannot read properties of undefined (reading 'split')");
    const auto nameSplitRegex = regex("[. _]");
    auto firstNameSplit = segments[0].split(nameSplitRegex, Qt::KeepEmptyParts);
    std::optional<double> year, season, diskNumber;
    std::optional<QString> aired, name;
    std::vector<double> episode;
    auto saneSeason = [&] { return season && !std::isnan(*season); };
    auto saneEpisode = [&] { return !episode.empty(); };
    const auto firstTwo = segments.mid(0, 2);

    for (const auto &seg : firstTwo) {
        auto matches = regex("\\b[0-9]{4}\\b").globalMatch(seg);
        while (matches.hasNext()) {
            const auto value = decimalPrefix(matches.next().captured());
            if (value >= 1900 && value <= 2030) year = value;
        }
    }
    for (const auto &seg : firstTwo) {
        const auto match = regex("([0-9]{4})([. -])([0-9]{2})([. -])([0-9]{2})").match(seg);
        if (match.hasMatch()) {
            const auto value = decimalPrefix(match.captured(1));
            if (value >= 1900 && value <= 2030)
                aired = QString::number(static_cast<int>(value)) + '-' + match.captured(3) + '-' + match.captured(5);
        }
    }
    for (const auto &seg : firstTwo) {
        for (const auto &part : seg.split(nameSplitRegex, Qt::KeepEmptyParts)) {
            auto match = regex("[sS]([0-9]{1,2})").match(part);
            if (match.hasMatch()) season = decimalPrefix(match.captured(1));
            auto matches = regex("(?<=[^A-Za-z0-9_]|[0-9])[eE]([0-9]{2})").globalMatch(part);
            if (matches.hasNext()) {
                episode.clear();
                while (matches.hasNext()) episode.push_back(decimalPrefix(matches.next().captured(1)));
            }
            match = regex("([0-9][0-9]?)[xX]([0-9][0-9]?)").match(part);
            if (match.hasMatch()) {
                season = decimalPrefix(match.captured(1));
                episode = {decimalPrefix(match.captured(2))};
            }
        }
        const auto match = regex("^([a-zA-Z0-9,-?!'& ]*) [sS]([0-9]{1,2})[eE]([0-9]{2})")
                               .match(QString(seg).replace(regex("[. ;_]"), " "));
        if ((!name || name->isEmpty()) && season && *season != 0 && !std::isnan(*season)
            && saneEpisode() && match.hasMatch() && !match.captured(1).isEmpty())
            name = match.captured(1);
    }
    const auto dotStamp = regex("[^0-9a-zA-Z]([0-9][0-9]?)\\.([0-9][0-9]?)[^0-9a-zA-Z]").match(segments[0]);
    if (!(saneSeason() && saneEpisode()) && dotStamp.hasMatch() && !year) {
        season = decimalPrefix(dotStamp.captured(1));
        episode = {decimalPrefix(dotStamp.captured(2))};
    }
    if (!(saneSeason() && saneEpisode())) {
        std::reverse(firstNameSplit.begin(), firstNameSplit.end());
        std::optional<QString> stamp;
        for (auto part : firstNameSplit) {
            if (test(part, "[0-9][0-9][0-9][0-9]?e")) part.chop(1);
            if (test(part, "s[0-9][0-9][0-9][0-9]?")) part = part.mid(1);
            if (!std::isnan(stringNumber(part)) && (part.size() == 3 || (!year && part.size() == 4))) {
                stamp = part;
                break;
            }
        }
        if (stamp && !stamp->isEmpty()
            && (!year || firstNameSplit.indexOf(*stamp) < firstNameSplit.indexOf(QString::number(static_cast<int>(*year))))) {
            episode = {decimalPrefix(stamp->right(2))};
            season = decimalPrefix(stamp->left(stamp->size() - 2));
        }
    }
    const auto joined = segments.join('/');
    if (!saneSeason()) {
        auto match = regex("[sS][eE][aA][sS][oO][nN]([.]| )?([0-9]{1,2})").match(joined);
        if (match.hasMatch()) season = decimalPrefix(match.captured(2));
        match = regex("[sS][eE][aA][sS][oO][nN] ([0-9]{1,2}) - ([0-9]{1,2})").match(joined);
        if (match.hasMatch()) {
            season = decimalPrefix(match.captured(1));
            episode = {decimalPrefix(match.captured(2))};
        }
    }
    if (!saneEpisode()) {
        const auto match = regex("[eE][pP]([iI][sS][oO][dD][eE])?([.]| )?([0-9]+)").match(joined);
        if (match.hasMatch()) episode = {decimalPrefix(match.captured(3))};
    }
    const auto disk = regex("[ _.-]*(?:cd|dvd|p(?:ar)?t|dis[ck]|d)[ _.-]*([0-9])[^0-9]").match(segments[0]);
    if (disk.hasMatch()) diskNumber = decimalPrefix(disk.captured(1));

    bool isSample = false;
    auto nameSegments = segments;
    std::reverse(nameSegments.begin(), nameSegments.end());
    for (auto seg : nameSegments) {
        if (seg == segments[0]) {
            auto pieces = seg.split('.', Qt::KeepEmptyParts);
            if (!pieces.empty()) pieces.removeLast();
            seg = pieces.join('.');
            const auto prefix = regex("^\\[([^\\n\\r\\x{2028}\\x{2029}]*?)\\]").match(seg);
            if (prefix.hasMatch()) seg = seg.mid(prefix.capturedLength());
        }
        const auto squareBracket = seg.indexOf('[');
        if (squareBracket >= 0) seg = seg.left(squareBracket);
        const auto parts = seg.split(regex("[. ;_-]"), Qt::KeepEmptyParts);
        isSample = isSample || test(seg, "^[sS][aA][mM][pP][lL][eE]") || test(seg, "^[eE][tT][rR][gG]");
        if (!name || name->isEmpty()) {
            QStringList nameParts;
            for (qsizetype i = 0; i < parts.size(); ++i) {
                const auto &word = parts[i];
                const auto lower = word.toLower();
                const bool numeric = !std::isnan(stringNumber(word));
                const bool nextNumeric = i + 1 < parts.size() && !std::isnan(stringNumber(parts[i + 1]));
                if (!(test(word, "^[a-zA-Z,?!'&]*\\z") || (numeric && word.size() <= 2) || (numeric && i == 0))
                    || movieKeywords.contains(lower)
                    || ((lower == "ep" || lower == "episode" || lower == "season") && nextNumeric)) break;
                nameParts.push_back(word);
            }
            if (nameParts.size() != 1 || std::isnan(stringNumber(nameParts[0]))) {
                QStringList nonempty;
                for (const auto &word : nameParts)
                    if (!word.isEmpty()) nonempty.push_back(word.left(1).toUpper() + word.mid(1).toLower());
                name = nonempty.join(' ');
            }
        }
    }
    isSample = isSample || (segments.size() > 1 && segments[1].toLower() == "sample");
    bool canBeMovie = year.has_value() || diskNumber.has_value();
    const auto lowerJoined = joined.toLower();
    for (const auto &keyword : movieKeywords) canBeMovie = canBeMovie || lowerJoined.contains(keyword);
    const bool hasName = name && !name->isEmpty();
    QString type;
    // The earlier name+aired assignment in M304 is overwritten by this chain.
    if (hasName && saneSeason() && saneEpisode()) type = "series";
    else if (hasName && canBeMovie) type = "movie";
    else if (hasName && saneSeason()) type = "extras";
    else type = "other";
    if (type != "series" || aired) { season.reset(); episode.clear(); }
    std::optional<Value> airedValue;
    if (aired) airedValue = stringValue(*aired);
    if (type == "series" && year) {
        if (!airedValue) airedValue = Value::number(*year);
        year.reset();
    }
    if (isSample) type += "-sample";
    if (name && !name->isEmpty()) {
        auto normalized = jsTrim(name->toLower());
        normalized.replace(regex("\\([^\\(]+\\)\\z"), "");
        normalized.replace('&', "and");
        normalized.replace(regex("[^0-9a-z ]+"), " ");
        name = normalized.split(' ', Qt::SkipEmptyParts).join(' ');
    }

    Value::Object result;
    if (year) result.emplace_back("year", Value::number(*year));
    if (airedValue) result.emplace_back("aired", *airedValue);
    if (season) result.emplace_back("season", Value::number(*season));
    if (!episode.empty()) {
        Value::Array values;
        for (const auto value : episode) values.push_back(Value::number(value));
        result.emplace_back("episode", Value::array(std::move(values)));
    }
    if (diskNumber) result.emplace_back("diskNumber", Value::number(*diskNumber));
    if (name) result.emplace_back("name", stringValue(*name));
    result.emplace_back("type", stringValue(type));
    Value::Array tags;
    if (test(path, "1080[pP]")) { tags.push_back(Value::string("hd")); tags.push_back(Value::string("1080p")); }
    if (test(path, "720[pP]")) tags.push_back(Value::string("720p"));
    if (test(path, "480[pP]")) tags.push_back(Value::string("480p"));
    if (isSample) tags.push_back(Value::string("sample"));
    result.emplace_back("tag", Value::array(std::move(tags)));
    return Value::object(std::move(result));
}

Value guessFileIndex(const Value &files, const Value &seriesInfo)
{
    if (!policy::jsTruthy(files) || files.kind() != Value::Kind::Array || !policy::jsTruthy(seriesInfo))
        return Value::number(-1);
    // The source's dots are deliberately unescaped. No newline suffix is accepted.
    static const auto media = regex("[^\\n\\r\\x{2028}\\x{2029}](?:[mM][kK][vV]|[aA][vV][iI]|[mM][pP]4|[wW][mM][vV]|[vV][pP]8|[mM][oO][vV]|[mM][pP][gG]|[tT][sS]|[mM]3[uU]8|[wW][eE][bB][mM]|[fF][lL][aA][cC]|[mM][pP]3|[wW][aA][vV]|[wW][mM][aA]|[aA][aA][cC]|[oO][gG][gG])\\z");
    std::vector<std::size_t> mediaFiles, episodeFiles;
    const auto &entries = files.asArray();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto path = field(entries[i], "path");
        if (path.isMissing() || path.isNull())
            throw std::invalid_argument(std::string("Cannot read properties of ")
                                        + (path.isNull() ? "null" : "undefined") + " (reading 'match')");
        if (path.kind() != Value::Kind::String) throw std::invalid_argument("file.path.match is not a function");
        if (media.match(utf8(path.asString())).hasMatch()) mediaFiles.push_back(i);
    }
    if (mediaFiles.empty()) return Value::number(-1);
    const auto wantedSeason = field(seriesInfo, "season"), wantedEpisode = field(seriesInfo, "episode");
    if (policy::jsTruthy(wantedSeason) && policy::jsTruthy(wantedEpisode)) {
        for (const auto index : mediaFiles) {
            try {
                const auto info = parseVideoName(field(entries[index], "path").asString());
                const auto season = field(info, "season"), episode = field(info, "episode");
                if (season.kind() != Value::Kind::Number || !std::isfinite(season.asNumber())
                    || !numericEqual(wantedSeason, season.asNumber()) || episode.kind() != Value::Kind::Array) continue;
                for (const auto &number : episode.asArray())
                    if (numericEqual(wantedEpisode, number.asNumber())) { episodeFiles.push_back(index); break; }
            } catch (const std::invalid_argument &) {
                // M663's catch returns -1, which is truthy to Array.filter.
                episodeFiles.push_back(index);
            }
        }
    }
    const auto &candidates = episodeFiles.empty() ? mediaFiles : episodeFiles;
    auto selected = candidates.front();
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const auto next = candidates[i];
        if (greater(field(entries[next], "length"), field(entries[selected], "length"))) selected = next;
    }
    return Value::number(static_cast<double>(selected));
}

} // namespace server1::http
