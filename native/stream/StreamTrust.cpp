// Adapted from Harbor trust.rs, commit 431c447977f8bc422d49ede47ad9b2681edb1649.
// Harbor is MIT licensed; see THIRD_PARTY_NOTICES.md.
// The stricter full-identity show gate follows Tankoban 2 AutoSourcePicker
// d6d804d6ce899c2c4aef37a2345e4d67de970acc. No network or UI state is used here.
#include "StreamTrust.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace colosseum::stream {
namespace {
constexpr quint64 MiB = 1024 * 1024;
constexpr quint64 GiB = 1024 * MiB;
const QRegularExpression shortFormatRx(QStringLiteral(
    R"((?i)\b(short|shorts|mini|mini[\s.\-_]?episode|ova|special|specials|skit|sketch|chibi|micro|webisode|vignette|interlude)\b)"));
const QRegularExpression placeholderRx(QStringLiteral(
    R"((?i)(?:🚫|⚠️?|❗|ℹ️?)\s*(?:no\s+streams?\s+(?:found|available)|streams?\s+filtered|streams?\s+blocked|filtered))"));
const QRegularExpression statusRx(QStringLiteral(
    R"((?i)\b(?:expires?\s+in|days?\s+left|premium\s+(?:active|expir(?:ed|ing))|api\s+limit|quota\s+used)\b)"));
const QRegularExpression videoExtRx(QStringLiteral(R"((?i)\.(mkv|mp4|m4v|avi|webm|mov|ts)(\?|$))"));
const QRegularExpression trailerRx(QStringLiteral(
    R"((?i)(?:^|[^a-z0-9])(?:trailer|teaser|tlr|trl|tra(?:iler)?|sneak[\s.\-_]?peek|preview|behind[\s.\-_]?the[\s.\-_]?scenes|featurette|making[\s.\-_]?of|deleted[\s.\-_]?scene|bloopers?|gag[\s.\-_]?reel|extras?|promo)(?:$|[^a-z0-9]))"));
const QRegularExpression fakeStudioRx(
    QStringLiteral(R"((?i)(?:^|[^a-z0-9])(?:dragon[\s._-]?money|telesynch)(?:$|[^a-z0-9]))"));
const QRegularExpression sequelTailRx(QStringLiteral(R"((?i)(?:\s|^)(\d{1,2}|[ivx]+)\s*$)"));
const QRegularExpression numberNameTailRx(
    QStringLiteral(R"((?i)(?:\bno\.?|\bnr\.?|\bnumber|#|n°|№)\s*(?:\d{1,2}|[ivx]+)\s*$)"));
const QRegularExpression yearParenRx(QStringLiteral(R"(\(\d{4}\))"));
const QRegularExpression partWordRx(QStringLiteral(R"((?i)\b(part|chapter|vol|volume)\b)"));
const QRegularExpression wordRx(QStringLiteral("[a-z0-9]+"));
const QRegularExpression
    badgeRx(QStringLiteral(R"((?i)seeders?|nyaasi|\b\d+(?:[.,]\d+)?\s*(?:GB|GiB|MB|MiB)\b)"));
const QSet<QString> stopwords = {"the",  "a",    "an",   "of",    "and", "in", "to",
                                 "for",  "on",   "at",   "by",    "is",  "or", "as",
                                 "from", "with", "into", "movie", "film"};
const QSet<QString> showStopwords = {"the", "a", "an", "of", "and", "to", "in", "season", "series"};
const QSet<QString> fakeGroups = {"LAMA",     "DJT",       "TELLY",      "RUDE",
                                  "TELESYNC", "TELESYNCH", "DRAGONMONEY"};
const QStringList romanNumbers = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"};
const QStringList numberWords = {"",    "one",   "two",   "three", "four", "five",
                                 "six", "seven", "eight", "nine",  "ten"};

QString normalized(const QString &s) {
    QString out;
    for (const auto c : s.toLower().normalized(QString::NormalizationForm_KD))
        if (c.unicode() < 0x0300 || c.unicode() > 0x036f)
            out.append(c);
    return out;
}
QStringList words(const QString &s, int minimum, const QSet<QString> &stop) {
    QStringList result;
    auto it = wordRx.globalMatch(normalized(s));
    while (it.hasNext()) {
        const auto w = it.next().captured();
        if (w.size() >= minimum && !stop.contains(w))
            result.append(w);
    }
    return result;
}
QStringList tokenize(const QString &s) { return words(s, 3, stopwords); }
int overlap(const QStringList &a, const QSet<QString> &b) {
    int hits = 0;
    for (const auto &w : a) {
        if (b.contains(w)) {
            ++hits;
            continue;
        }
        for (const auto &other : b)
            if (w.size() >= 4 && other.size() >= 4 &&
                (w.startsWith(other) || other.startsWith(w))) {
                ++hits;
                break;
            }
    }
    return hits;
}
std::optional<int> sequelMarker(QString title) {
    title.remove(yearParenRx).remove(partWordRx);
    title = title.trimmed();
    if (numberNameTailRx.match(title).hasMatch())
        return {};
    const auto m = sequelTailRx.match(title);
    if (!m.hasMatch())
        return {};
    const auto token = m.captured(1).toLower();
    bool ok = false;
    const int n = token.toInt(&ok);
    if (ok)
        return n >= 2 && n <= 20 ? std::optional<int>(n) : std::nullopt;
    const int roman = romanNumbers.indexOf(token);
    return roman >= 2 ? std::optional<int>(roman) : std::nullopt;
}
bool hasSequel(const QString &haystack, int sequel) {
    auto it = wordRx.globalMatch(haystack.toLower());
    while (it.hasNext()) {
        const auto token = it.next().captured();
        if (token == QString::number(sequel) ||
            (sequel < romanNumbers.size() && token == romanNumbers[sequel]) ||
            (sequel < numberWords.size() && token == numberWords[sequel]))
            return true;
    }
    return false;
}
bool titleTokensPresent(const QString &expected, const QString &parsed, bool guardShort) {
    const auto a = tokenize(expected), b = tokenize(parsed);
    if (a.isEmpty() || b.isEmpty())
        return true;
    const QSet<QString> as(a.begin(), a.end()), bs(b.begin(), b.end());
    const int forward = overlap(a, bs), reverse = overlap(b, as);
    if (guardShort && a.size() <= 2 && b.size() - forward > 2)
        return false;
    return static_cast<double>(forward) / a.size() >= 0.5 ||
           static_cast<double>(reverse) / b.size() >= 0.5 || forward >= 2;
}
bool titleMatches(const QString &expected, const QString &parsed, const ParsedStream &s,
                  const TrustOptions &o) {
    const auto e = sequelMarker(expected), p = sequelMarker(parsed);
    const int age = o.expected_year ? o.today.year() - *o.expected_year : 0;
    const int tolerance = age >= 30 ? 4 : age >= 15 ? 3 : age >= 5 ? 2 : 1;
    if (e && p && *e != *p)
        return false;
    if ((e && !p) || (!e && p && *p >= 2)) {
        if (!s.year || !o.expected_year || std::abs(*s.year - *o.expected_year) > tolerance)
            return false;
    }
    return titleTokensPresent(expected, parsed, true);
}
QString hintFilename(const ParsedStream &s) {
    const auto lower = s.stream.behavior_hints.value("filename");
    return lower.isString() ? lower.toString()
                            : s.stream.behavior_hints.value("fileName").toString();
}
QString identityText(const ParsedStream &s) {
    // Full blob is deliberately used: some add-ons put only a stats badge in title.
    return s.match_text + " " + s.filename + " " + hintFilename(s) + " " + s.stream.name + " " +
           s.stream.title + " " + s.stream.description + " " + s.parsed_title;
}
bool showTokensPresent(const QString &expected, const QString &candidate) {
    const auto required = words(expected, 2, showStopwords), actual = words(candidate, 1, {});
    const QSet<QString> lookup(actual.begin(), actual.end());
    for (const auto &w : required)
        if (!lookup.contains(w))
            return false;
    return true;
}
bool unreliableParsedTitle(const ParsedStream &s) {
    return s.parsed_title.isEmpty() || (badgeRx.match(s.parsed_title).hasMatch() &&
                                        (s.parsed_title.contains('|') ||
                                         s.parsed_title.contains("seeders", Qt::CaseInsensitive)));
}
QString parsedIdentity(const ParsedStream &s) {
    // Preserve Harbor's parsed-title matching for normal releases, but don't gate
    // exclusively on an add-on's stats-only display title (Tankoban 2 regression).
    if (unreliableParsedTitle(s)) {
        const auto hint = hintFilename(s);
        if (!hint.isEmpty())
            return hint;
        if (!s.filename.isEmpty())
            return s.filename;
        return identityText(s);
    }
    return s.parsed_title;
}
bool theater(Source s) {
    return s == Source::CAM || s == Source::TS || s == Source::HDTS || s == Source::TC;
}
QString resolutionLabel(Resolution r) {
    switch (r) {
    case Resolution::UHD:
        return "4K";
    case Resolution::P1080:
        return "1080p";
    case Resolution::P720:
        return "720p";
    case Resolution::P480:
        return "480p";
    case Resolution::SD:
        return "SD";
    }
    return "SD";
}
quint64 sizeFloor(Resolution r, bool cinema, bool older, bool series, bool anime) {
    const int index = r == Resolution::UHD     ? 0
                      : r == Resolution::P1080 ? 1
                      : r == Resolution::P720  ? 2
                      : r == Resolution::P480  ? 3
                                               : 4;
    const quint64 movie[5][3] = {{2560 * MiB, 1536 * MiB, 600 * MiB},
                                 {(GiB * 6 + 4) / 5, 700 * MiB, 250 * MiB},
                                 {600 * MiB, 400 * MiB, 120 * MiB},
                                 {250 * MiB, 150 * MiB, 50 * MiB},
                                 {200 * MiB, 100 * MiB, 25 * MiB}};
    const quint64 episode[5][3] = {{1024 * MiB, 600 * MiB, 200 * MiB},
                                   {400 * MiB, 250 * MiB, 100 * MiB},
                                   {200 * MiB, 120 * MiB, 40 * MiB},
                                   {80 * MiB, 50 * MiB, 12 * MiB},
                                   {50 * MiB, 30 * MiB, 8 * MiB}};
    const quint64 animeEpisode[5][3] = {{600 * MiB, 400 * MiB, 150 * MiB},
                                        {220 * MiB, 150 * MiB, 50 * MiB},
                                        {100 * MiB, 60 * MiB, 20 * MiB},
                                        {40 * MiB, 28 * MiB, 8 * MiB},
                                        {25 * MiB, 18 * MiB, 5 * MiB}};
    const int column = older ? 2 : cinema ? 0 : 1;
    return series ? (anime ? animeEpisode[index][column] : episode[index][column])
                  : movie[index][column];
}
QString rejectReason(const ParsedStream &s, const TrustOptions &o, bool cinema, bool older) {
    if (s.stream.url.isEmpty() && s.stream.info_hash.isEmpty() && s.stream.yt_id.isEmpty() &&
        s.stream.external_url.isEmpty() && s.stream.raw.value("nzbUrl").toString().isEmpty())
        return "no-playable-source";
    const QString display = s.stream.title + " " + s.stream.name + " " + s.stream.description;
    if (placeholderRx.match(display).hasMatch())
        return "addon-placeholder-banner";
    if (s.stream.info_hash.isEmpty() && !videoExtRx.match(s.stream.url).hasMatch() &&
        statusRx.match(display).hasMatch()) {
        if (s.stream.behavior_hints.value("videoSize").toDouble() == 0 &&
            s.stream.behavior_hints.value("filename").toString().isEmpty())
            return "addon-status-card";
    }
    const QString filename = hintFilename(s).toLower();
    const QString haystack =
        filename + " " + s.stream.title.toLower() + " " + s.stream.name.toLower();
    for (const auto *ext : {".exe", ".zip", ".rar", ".lnk", ".scr", ".bat", ".iso", ".img"})
        if (filename.endsWith(QLatin1String(ext)))
            return QString("suspicious-extension:") + QLatin1String(ext);
    if (trailerRx.match(haystack).hasMatch())
        return "trailer-or-extra";
    if (fakeGroups.contains(s.release_group_normalized) || fakeStudioRx.match(haystack).hasMatch())
        return "known-fake-group";
    if (s.size && *s.size < 5 * MiB)
        return "size-stub";
    const bool movie = o.kind == "movie", series = o.kind == "series";
    const bool shortFormat = shortFormatRx.match(haystack).hasMatch();
    if (movie && s.size && *s.size < sizeFloor(s.resolution, cinema, older, false, false))
        return "movie-stub-too-small-for-" + resolutionLabel(s.resolution);
    if (movie && cinema && !theater(s.source) && s.size) {
        const auto sizeMb = static_cast<quint64>(std::round(static_cast<double>(*s.size) / MiB));
        if (*s.size < 250 * MiB)
            return QString("new-release-virus-%1mb").arg(sizeMb);
        if (*s.size < 500 * MiB && !shortFormat)
            return QString("new-release-stub-%1mb").arg(sizeMb);
    }
    if (movie && !o.is_anime && (s.season_pack || s.season || s.episode))
        return "series-result-for-movie";
    if (o.strict && movie && !o.is_anime && cinema && o.expected_year && !s.year &&
        s.source == Source::Other && s.resolution == Resolution::SD)
        return "cinema-bare-untagged";
    const auto parsed = parsedIdentity(s);
    if (o.strict && movie && !o.expected_title.isEmpty() && !parsed.isEmpty() &&
        !titleMatches(o.expected_title, parsed, s, o))
        return "title-mismatch";
    if (o.strict && movie && cinema && s.year && o.expected_year && *s.year != *o.expected_year)
        return QString("cinema-year-mismatch:%1-vs-%2").arg(*s.year).arg(*o.expected_year);
    if (o.strict && movie && !o.expected_title.isEmpty()) {
        const auto sequel = sequelMarker(o.expected_title);
        if (sequel && *sequel >= 2 && !hasSequel(filename + " " + s.stream.title, *sequel))
            return "filename-missing-sequel";
    }
    if (o.strict && movie && cinema) {
        if (s.source == Source::BluRay || s.remux)
            return "fresh-cinema-fake-bluray";
        const bool web = s.source == Source::WebDl || s.source == Source::WEBRip ||
                         s.source == Source::BDRip || s.source == Source::HDRip;
        if (s.resolution == Resolution::UHD && web)
            return "fresh-cinema-fake-4k-web";
        if ((s.resolution == Resolution::P1080 || s.resolution == Resolution::P720) && web)
            return "fresh-cinema-fake-web";
        if (s.source == Source::HDTV &&
            (s.resolution == Resolution::UHD || s.resolution == Resolution::P1080))
            return "fresh-cinema-fake-hdtv";
    }
    if (series && s.size && !shortFormat &&
        *s.size < sizeFloor(s.resolution, cinema, older, true, o.is_anime))
        return "episode-stub-too-small-for-" + resolutionLabel(s.resolution);
    // A stats-only title has no title length: do not apply Harbor's short-title
    // vs long-title mismatch rule to the concatenated stats/filename blob. The
    // stricter whole-word identity gate below still requires the complete show.
    const bool badgeIdentityMatches =
        unreliableParsedTitle(s) && showTokensPresent(o.expected_title, identityText(s));
    if (o.strict && series && !o.is_anime && !o.expected_title.isEmpty() && !parsed.isEmpty() &&
        !badgeIdentityMatches && !titleMatches(o.expected_title, parsed, s, o))
        return "title-mismatch";
    if (o.strict && series && o.is_anime && !o.expected_titles.isEmpty() && !parsed.isEmpty()) {
        bool ok = false;
        for (const auto &title : o.expected_titles)
            if (titleTokensPresent(title, parsed, false)) {
                ok = true;
                break;
            }
        if (!ok)
            return "anime-title-mismatch";
    }
    if (o.strict && series && !o.expected_title.isEmpty()) {
        const auto full = identityText(s);
        bool match = showTokensPresent(o.expected_title, full);
        for (const auto &alias : o.expected_titles)
            if (!alias.trimmed().isEmpty() && showTokensPresent(alias, full))
                match = true;
        if (!match)
            return "show-identity-mismatch";
    }
    const auto fileIdx = s.stream.raw.value("fileIdx");
    const double fileIdxNumber = fileIdx.toDouble(-1);
    const bool hasFileIdx = fileIdx.isDouble() && std::isfinite(fileIdxNumber) &&
                            fileIdxNumber >= 0 && std::floor(fileIdxNumber) == fileIdxNumber;
    if (o.strict && !o.is_anime && !hasFileIdx && !s.season_pack) {
        if (o.expected_season && s.season && *o.expected_season != *s.season)
            return QString("season-mismatch:%1-vs-%2").arg(*s.season).arg(*o.expected_season);
        if (o.expected_episode && s.episode &&
            (*o.expected_episode < *s.episode ||
             *o.expected_episode > s.episode_end.value_or(*s.episode)))
            return QString("episode-mismatch:%1-vs-%2").arg(*s.episode).arg(*o.expected_episode);
    }
    if (s.scam_score >= 5 && !o.allow_cam && !older)
        return QString("scam-score-%1").arg(s.scam_score);
    return {};
}
} // namespace
TrustResult StreamTrust::apply(const QList<ParsedStream> &streams, const TrustOptions &options) {
    if (options.disabled)
        return {streams, {}};
    const auto days = options.release_date.daysTo(options.today);
    const bool datesValid = options.release_date.isValid() && options.today.isValid();
    const bool cinema = datesValid && days > -90 && days < 60;
    const bool older =
        datesValid ? days > 365 * 2
                   : (options.expected_year && options.today.year() - *options.expected_year > 2);
    TrustResult result;
    for (const auto &stream : streams) {
        const auto reason = rejectReason(stream, options, cinema, older);
        if (reason.isEmpty())
            result.keep.append(stream);
        else
            result.rejected.append({stream, reason});
    }
    return result;
}
} // namespace colosseum::stream
