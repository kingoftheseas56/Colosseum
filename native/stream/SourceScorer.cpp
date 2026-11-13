// Adapted from Harbor (MIT), scoring.rs at 431c447977f8bc422d49ede47ad9b2681edb1649.
// Copyright notice and license: THIRD_PARTY_NOTICES.md.
// Primary-selection guards derive from Tankoban 2 AutoSourcePicker at
// d6d804d6ce899c2c4aef37a2345e4d67de970acc; no debrid/browser-specific scoring.
#include "SourceScorer.h"
#include "StreamParser.h"
#include "StreamTrust.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace colosseum::stream {
namespace {
constexpr double MiB = 1024.0 * 1024.0;
constexpr double GiB = MiB * 1024.0;
bool theater(Source s) {
    return s == Source::CAM || s == Source::TS || s == Source::HDTS || s == Source::TC;
}
bool webish(Source s) {
    return s == Source::WebDl || s == Source::WEBRip || s == Source::BluRay || s == Source::BDRip;
}
std::optional<double> daysSince(const ScoreOptions &o) {
    if (!o.release_date.isValid() || !o.today.isValid())
        return std::nullopt;
    return static_cast<double>(o.release_date.daysTo(o.today));
}
bool trustedGroup(const QString &group) {
    static const QSet<QString> groups = {"FRDS",         "FRAMESTOR",
                                         "FORM",         "EVO",
                                         "RARBG",        "ETHEL",
                                         "FLUX",         "QXR",
                                         "MEGUSTA",      "ION10",
                                         "PSA",          "AMIABLE",
                                         "GALAXYRG",     "WEBDV",
                                         "RZEROX",       "SIC",
                                         "TGX",          "NTB",
                                         "NTG",          "TEPES",
                                         "GECKOS",       "SUCCESSFULCRAB",
                                         "SUBSPLEASE",   "ERAI",
                                         "ERAIRAWS",     "JUDAS",
                                         "ASW",          "EMBER",
                                         "ANE",          "CLEO",
                                         "BEATRICERAWS", "AKIHITO",
                                         "VODES",        "NANDESUKA",
                                         "SMOL",         "TENRAISENSEI",
                                         "GST",          "ANIMEKAIZOKU",
                                         "REINFORCE",    "RAWS",
                                         "OZR",          "PURGATORY",
                                         "SHK",          "KOTUWA",
                                         "KIRION",       "COMMIE",
                                         "DAMEDESUYO",   "MTBB",
                                         "GJM",          "SOFCJ"};
    return groups.contains(group);
}
bool lossyGroup(const QString &group) {
    static const QSet<QString> groups = {"YTS", "YIFY", "YTSAG", "YTS-AG"};
    return groups.contains(group.toUpper());
}
template <typename T> std::optional<T> percentile(QList<T> values, double q) {
    if (values.isEmpty())
        return std::nullopt;
    std::sort(values.begin(), values.end());
    return values[static_cast<qsizetype>(std::floor((values.size() - 1) * q))];
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
QString hdrLabel(HdrFormat h) {
    switch (h) {
    case HdrFormat::Hdr10:
        return "HDR10";
    case HdrFormat::Hdr10Plus:
        return "HDR10+";
    case HdrFormat::Dv:
        return "DV";
    case HdrFormat::DvHdr10:
        return "DV+HDR10";
    case HdrFormat::Hlg:
        return "HLG";
    }
    return {};
}
double resolutionPoints(Resolution r) {
    switch (r) {
    case Resolution::UHD:
        return 25;
    case Resolution::P1080:
        return 20;
    case Resolution::P720:
        return 8;
    case Resolution::P480:
        return 2;
    case Resolution::SD:
        return 0;
    }
    return 0;
}
Tier tierOf(const ParsedStream &s) {
    if (theater(s.source) || s.source == Source::SCR)
        return Tier::Rough;
    if (s.resolution == Resolution::UHD) {
        if (s.hdr_format == HdrFormat::Dv || s.hdr_format == HdrFormat::DvHdr10)
            return Tier::UhdDv;
        return s.hdr_format ? Tier::UhdHdr : Tier::Uhd;
    }
    if (s.resolution == Resolution::P1080)
        return s.hdr_format ? Tier::P1080Hdr : Tier::P1080;
    if (s.resolution == Resolution::P720)
        return Tier::P720;
    return Tier::SD;
}
QString tierKey(Tier tier) {
    switch (tier) {
    case Tier::UhdDv:
        return "4K_DV";
    case Tier::UhdHdr:
        return "4K_HDR";
    case Tier::Uhd:
        return "4K";
    case Tier::P1080Hdr:
        return "1080p_HDR";
    case Tier::P1080:
        return "1080p";
    case Tier::P720:
        return "720p";
    case Tier::SD:
        return "SD";
    case Tier::Rough:
        return "ROUGH";
    }
    return "SD";
}
std::optional<double> expectedMinimum(const ParsedStream &s, const ScoreOptions &o) {
    if (!o.runtime_minutes || *o.runtime_minutes <= 0)
        return std::nullopt;
    double mb = 2;
    switch (s.resolution) {
    case Resolution::UHD:
        mb = 60;
        break;
    case Resolution::P1080:
        mb = 18;
        break;
    case Resolution::P720:
        mb = 8;
        break;
    case Resolution::P480:
        mb = 3;
        break;
    case Resolution::SD:
        break;
    }
    return mb * *o.runtime_minutes * MiB;
}
double sizeMislabel(const ParsedStream &s, const std::optional<double> &expected) {
    if (!s.size || *s.size == 0 || !expected || theater(s.source) ||
        lossyGroup(s.release_group_normalized))
        return 0;
    const double ratio = static_cast<double>(*s.size) / *expected;
    return ratio < .25 ? -120 : ratio < .5 ? -60 : ratio < .75 ? -20 : 0;
}
double camInFilename(const ParsedStream &s) {
    if (theater(s.source) || (s.resolution != Resolution::P1080 && s.resolution != Resolution::UHD))
        return 0;
    static const QRegularExpression cam(QStringLiteral(
        "(?i)\\b(?:cam|hdcam|hd[\\s._-]?cam|tsrip|telesync|hdts|hd[\\s._-]?ts|telecine|hd[\\s._-]?"
        "tc|hc[\\s._-]?hdrip|hc[\\s._-]?cam|new[\\s._-]?cam|cleancam|hqcam)\\b"));
    auto text = s.stream.name + ' ' + s.stream.title + ' ' + s.stream.description + ' ' +
                s.filename + ' ' + s.stream.behavior_hints.value("filename").toString() + ' ' +
                s.stream.behavior_hints.value("fileName").toString();
    text.replace('_', '.');
    return cam.match(text).hasMatch() ? (s.resolution == Resolution::UHD ? -200 : -100) : 0;
}
ScoreReason undersized(const ParsedStream &s, const ScoreOptions &o) {
    const auto days = daysSince(o);
    if (o.media_kind == "series" || !days || !s.size || *days >= 90 || theater(s.source))
        return {};
    const double size = static_cast<double>(*s.size) / GiB;
    if (s.resolution == Resolution::UHD && size < 6)
        return {QString("4k-undersized-%1gb").arg(size, 0, 'f', 1), -250};
    if (s.resolution == Resolution::P1080 && size < 1.5)
        return {QString("1080p-undersized-%1gb").arg(size, 0, 'f', 1), -200};
    if (s.resolution == Resolution::P720 && size < .6)
        return {QString("720p-undersized-%1gb").arg(size, 0, 'f', 1), -80};
    return {};
}
ScoreReason tinyMovie(const ParsedStream &s, const ScoreOptions &o) {
    const auto days = daysSince(o);
    if (o.media_kind == "series" || !days || !s.size || *days >= 90 || theater(s.source))
        return {};
    const double size = static_cast<double>(*s.size) / MiB;
    if (size < 250)
        return {QString("new-release-virus-%1mb").arg(std::llround(size)), -250};
    const double floor =
        std::max(500.0, static_cast<double>(std::max(0, o.runtime_minutes.value_or(0))) * 5);
    if (size < floor)
        return {QString("new-release-no-label-%1mb").arg(std::llround(size)), -200};
    return {};
}
ScoreReason bandwidthPenalty(const ParsedStream &s, const ScoreOptions &o) {
    if (!o.bandwidth_mbps || !std::isfinite(*o.bandwidth_mbps) || *o.bandwidth_mbps <= 0)
        return {};
    const double budget = *o.bandwidth_mbps;
    if (s.size && *s.size > 0 && o.runtime_minutes && *o.runtime_minutes > 0) {
        const double required = static_cast<double>(*s.size) * 8 /
                                (static_cast<double>(*o.runtime_minutes) * 60) / 1000000;
        if (required > budget * 1.1)
            return {QString("bitrate-exceeds-budget:%1>%2Mbps")
                        .arg(std::llround(required))
                        .arg(std::llround(budget)),
                    required > budget * 1.5 ? -120.0 : -45.0};
        if (required > budget * .8)
            return {QString("bitrate-tight:%1/%2Mbps")
                        .arg(std::llround(required))
                        .arg(std::llround(budget)),
                    -12};
    }
    if (s.resolution == Resolution::UHD && budget < 25)
        return {"low-bandwidth-4k", -60};
    if (s.resolution == Resolution::P1080 && budget < 8)
        return {"low-bandwidth-1080p", -45};
    return {};
}
ScoreReason freshTheatrical(const ParsedStream &s, const ScoreOptions &o, bool validSize,
                            const CorpusStats &c) {
    const auto days = daysSince(o);
    if (o.media_kind == "series" || !days || *days >= 150)
        return {};
    const bool dominated = c.trusted_tracked_count >= 4 && c.theater_capture_fraction >= .4 &&
                           c.theater_capture_fraction > c.webish_fraction;
    if (!dominated && *days >= 30)
        return {};
    if (theater(s.source)) {
        if (dominated)
            return {"fresh-theater-cinema-window",
                    s.source == Source::CAM                                ? 95.0
                    : (s.source == Source::TS || s.source == Source::HDTS) ? 75.0
                                                                           : 65.0};
        return *days < 14 ? ScoreReason{"fresh-theater-mild-boost", 25} : ScoreReason{};
    }
    const bool bluray = s.source == Source::BluRay || s.remux;
    const bool high = s.source == Source::WebDl || s.source == Source::WEBRip || bluray ||
                      s.resolution == Resolution::P1080 || s.resolution == Resolution::UHD;
    if (!high)
        return {};
    if (dominated) {
        if (bluray)
            return {"fresh-fake-remux", -200};
        if (*days < 0)
            return {"fresh-fake-prerelease", -160};
        if (*days < 14)
            return {"fresh-fake-prebluray", -90};
        return {"fresh-fake-soft", -45};
    }
    if (bluray && *days < 14)
        return {"fresh-prebluray-suspect", -55};
    if (*days < 0 && !validSize)
        return {"fresh-prerelease-soft", -35};
    return {"fresh-soft-flag", -10};
}
bool hasPlayable(const ParsedStream &s) {
    return !s.stream.url.trimmed().isEmpty() || !s.stream.info_hash.trimmed().isEmpty() ||
           !s.stream.yt_id.trimmed().isEmpty() || !s.stream.external_url.trimmed().isEmpty();
}
bool primaryEligible(const ScoredStream &s) {
    const auto &p = s.parsed;
    // The parser's 3-point size warning uses movie-sized floors and is common
    // for legitimate anime episodes. Only the trust filter's hard threshold (5)
    // bars auto-pick; the smaller warning still reduces the numeric score.
    if (!std::isfinite(s.score) || s.score < 0 || !hasPlayable(p) || theater(p.source) ||
        p.source == Source::SCR || p.scam_score >= 5)
        return false;
    // A direct URL can still be playable when its optional torrent metadata is stale.
    if (!p.stream.info_hash.isEmpty() && p.stream.url.isEmpty() && p.seeders == 0)
        return false;
    // A caller's origin preference must not buy its way past a fraud signal.
    for (const auto &r : s.reasons) {
        if (r.delta < 0 &&
            (r.signal == "title-says-hires-filename-says-cam" ||
             r.signal.startsWith("fresh-fake-") || r.signal.startsWith("new-release-") ||
             r.signal.contains("-undersized-") || r.signal == "fresh-prebluray-suspect"))
            return false;
        if (r.signal == "size-mismatch" && r.delta <= -60)
            return false;
    }
    return true;
}
bool selectedEpisodeFile(const ParsedStream &p, const ScoreOptions &o) {
    if (!o.episode_requested || !o.expected_episode)
        return false;
    const auto index = p.stream.raw.value("fileIdx");
    if (!index.isDouble())
        return false;
    const double value = index.toDouble();
    if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
        value > std::numeric_limits<int>::max())
        return false;
    QString filename = p.stream.behavior_hints.value("filename").toString().trimmed();
    if (filename.isEmpty())
        filename = p.stream.behavior_hints.value("fileName").toString().trimmed();
    if (filename.isEmpty())
        return false;
    // Parse the selected file alone. The parent torrent's "Complete 001-1000"
    // description must not turn its explicitly selected E01 into a pack.
    Stream file;
    file.title = filename;
    file.behavior_hints.insert("filename", filename);
    const auto parsed = StreamParser::parse(file);
    if (parsed.batch_release || parsed.season_pack || !parsed.episode ||
        parsed.episode != o.expected_episode ||
        (parsed.episode_end && parsed.episode_end != parsed.episode))
        return false;
    return !o.expected_season || !parsed.season || parsed.season == o.expected_season;
}
bool batch(const ParsedStream &p, const ScoreOptions &o) {
    return (p.batch_release || p.season_pack) && !selectedEpisodeFile(p, o);
}

struct StreamingBand {
    int preference = 1; // known-friendly, unknown-size, then oversized fallback
    QString reason;
};
// Tunable primary-only streaming policy; GB here is decimal (1,000,000,000 bytes).
// The caps remain useful even with runtime, while the bitrate ceiling prevents a
// short film/episode under its size cap from being an unexpectedly heavy stream.
constexpr double kFilm1080pMaxGB = 15, kFilm720pMaxGB = 6, kFilm4kMaxGB = 30;
constexpr double kEpisode1080pMaxGB = 3, kEpisode720pMaxGB = 1.5, kEpisode4kMaxGB = 6;
constexpr double k1080pMaxMbps = 20, k720pMaxMbps = 8, k4kMaxMbps = 40;
StreamingBand streamingBand(const ParsedStream &p, const ScoreOptions &o) {
    if (!p.size || *p.size == 0)
        return {1, "streaming-size:unknown (kept behind known-friendly, before oversized)"};
    const bool episode = o.episode_requested || o.media_kind == "series";
    double capGB = episode ? kEpisode1080pMaxGB : kFilm1080pMaxGB;
    double capMbps = k1080pMaxMbps;
    switch (p.resolution) {
    case Resolution::UHD:
        capGB = episode ? kEpisode4kMaxGB : kFilm4kMaxGB;
        capMbps = k4kMaxMbps;
        break;
    case Resolution::P720:
        capGB = episode ? kEpisode720pMaxGB : kFilm720pMaxGB;
        capMbps = k720pMaxMbps;
        break;
    case Resolution::P480:
    case Resolution::SD:
        capGB = episode ? 1 : 3;
        capMbps = 5;
        break;
    case Resolution::P1080:
        break;
    }
    const double sizeGB = static_cast<double>(*p.size) / 1000000000.0;
    const bool runtimeKnown = o.runtime_minutes && *o.runtime_minutes > 0;
    const double bitrate = runtimeKnown
                               ? static_cast<double>(*p.size) * 8 /
                                     (static_cast<double>(*o.runtime_minutes) * 60) / 1000000
                               : 0;
    const bool friendly = sizeGB <= capGB && (!runtimeKnown || bitrate <= capMbps);
    QString detail = QString("size=%1GB limit=%2GB").arg(sizeGB, 0, 'f', 2).arg(capGB);
    if (runtimeKnown)
        detail += QString(" bitrate=%1Mbps limit=%2Mbps").arg(bitrate, 0, 'f', 2).arg(capMbps);
    else
        detail += " runtime=unknown; size-cap-only";
    return {friendly ? 0 : 2,
            QString("streaming-size:%1 (%2)").arg(friendly ? "within-band" : "outside-band", detail)};
}
bool unsupportedDvOnly(const ParsedStream &p, const ScoreOptions &o) {
    return !o.dolby_vision_supported && p.hdr_format == HdrFormat::Dv;
}
QStringList concreteLanguages(const QStringList &languages) {
    QStringList concrete;
    for (const auto &language : languages)
        if (language.compare("Multi", Qt::CaseInsensitive) != 0 && !language.isEmpty())
            concrete.push_back(language);
    return concrete;
}
bool matchesPreferred(const QString &language, const QStringList &preferred) {
    return std::any_of(preferred.begin(), preferred.end(), [&](const QString &p) {
        return !p.trimmed().isEmpty() && language.toLower().startsWith(p.trimmed().toLower());
    });
}
QString languagePattern(const QString &language) {
    // Full names/three-letter audio tags and flags, not ordinary two-letter
    // country tokens such as GB (which can be a byte-size unit).
    static const QMap<QString, QString> patterns = {
        {"English", R"((?:\b(?:English|Eng)\b|\[(?:EN|US|GB)\]|🇬🇧|🇺🇸))"},
        {"Russian", R"((?:\b(?:Russian|Rus)\b|\[RU\]|🇷🇺))"},
        {"Japanese", R"((?:\b(?:Japanese|Jpn|Jap)\b|\[(?:JA|JP)\]|🇯🇵))"},
        {"French", R"((?:\b(?:French|Fre|Fra)\b|\[FR\]|🇫🇷))"},
        {"German", R"((?:\b(?:German|Ger|Deu)\b|\[DE\]|🇩🇪))"},
        {"Spanish", R"((?:\b(?:Spanish|Spa)\b|\[ES\]|🇪🇸|🇲🇽))"},
        {"Italian", R"((?:\b(?:Italian|Ita)\b|\[IT\]|🇮🇹))"},
        {"Hindi", R"((?:\b(?:Hindi|Hin)\b|\[HI\]|🇮🇳))"},
        {"Korean", R"((?:\b(?:Korean|Kor)\b|\[(?:KO|KR)\]|🇰🇷))"},
        {"Chinese", R"((?:\b(?:Chinese|Chi|Zho|Mandarin|Cantonese)\b|\[(?:ZH|CN)\]|🇨🇳|🇹🇼))"},
        {"Ukrainian", R"((?:\b(?:Ukrainian|Ukr)\b|\[(?:UK|UA)\]|🇺🇦))"}};
    return patterns.value(language, "\\b" + QRegularExpression::escape(language) + "\\b");
}
qsizetype contextualLanguagePosition(const QString &text, const QString &language) {
    static const QMap<QString, QString> codes = {
        {"English", "EN|US|GB|AU|NZ"}, {"Russian", "RU"}, {"Japanese", "JA|JP"},
        {"French", "FR"}, {"German", "DE"}, {"Spanish", "ES|MX"}, {"Italian", "IT"},
        {"Hindi", "HI"}, {"Korean", "KO|KR"}, {"Chinese", "ZH|CN|TW|HK"},
        {"Ukrainian", "UK|UA"}, {"Portuguese", "PT|BR"}, {"Arabic", "AR|SA|AE|EG"},
        {"Dutch", "NL"}, {"Polish", "PL"}, {"Romanian", "RO"}, {"Swedish", "SV|SE"},
        {"Danish", "DA"}, {"Finnish", "FI"}, {"Czech", "CS|CZ"}, {"Hungarian", "HU"},
        {"Thai", "TH"}, {"Vietnamese", "VI"}, {"Hebrew", "HE|IL"}, {"Greek", "GR"},
        {"Indonesian", "ID"}, {"Malay", "MY"}, {"Filipino", "PH"}, {"Persian", "FA|IR"}};
    if (!codes.contains(language))
        return -1;
    const QRegularExpression token("(?<![A-Za-z0-9])(?:" + codes.value(language) +
                                       ")(?![A-Za-z0-9])",
                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numberBefore(R"(\d[\s.,]*$)");
    static const QRegularExpression numberAfter(R"(^[\s.,]*\d)");
    qsizetype first = -1;
    auto scan = [&](const QString &body, qsizetype offset) {
        auto matches = token.globalMatch(body);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (numberBefore.match(body.left(match.capturedStart())).hasMatch() ||
                numberAfter.match(body.mid(match.capturedEnd())).hasMatch())
                continue;
            const auto position = offset + match.capturedStart();
            if (first < 0 || position < first)
                first = position;
        }
    };
    // Only tag-shaped bracket lists: [EN/RU], [RU EN], (EN, FR). This must
    // not reinterpret prose identifiers, movie names or [6.45 GB] as audio.
    static const QRegularExpression groups(R"(\[([^\]\r\n]+)\]|\(([^)\r\n]+)\))");
    static const QRegularExpression codeList(R"(^\s*[A-Za-z]{2}(?:[\s,/|+;-]+[A-Za-z]{2})*\s*$)");
    auto brackets = groups.globalMatch(text);
    while (brackets.hasNext()) {
        const auto match = brackets.next();
        const int capture = match.capturedStart(1) >= 0 ? 1 : 2;
        if (codeList.match(match.captured(capture)).hasMatch())
            scan(match.captured(capture), match.capturedStart(capture));
    }
    static const QRegularExpression labeled(
        R"(\b(?:languages?|audio(?:\s+languages?)?|flags?)\s*[:=]\s*([^\r\n]+))",
        QRegularExpression::CaseInsensitiveOption);
    auto lines = labeled.globalMatch(text);
    while (lines.hasNext()) {
        const auto match = lines.next();
        scan(match.captured(1), match.capturedStart(1));
    }
    return first;
}
bool nonLatinLead(const QString &text) {
    for (const auto c : text) {
        if (!c.isLetter())
            continue;
        return c.script() != QChar::Script_Latin && c.script() != QChar::Script_Common;
    }
    return false;
}
bool preferredAudioSecondary(const ParsedStream &p, const ScoreOptions &o) {
    if (p.french_led && !matchesPreferred("French", o.preferred_languages))
        return true;
    const auto languages = concreteLanguages(p.audio_languages);
    const QString text = p.filename + '\n' + p.stream.title + '\n' + p.stream.description;
    qsizetype firstPreferred = -1, firstOther = -1;
    bool explicitPreferredAudio = false;
    for (const auto &language : languages) {
        const QString pattern = languagePattern(language);
        const QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
        const auto match = rx.match(text);
        const bool preferred = matchesPreferred(language, o.preferred_languages);
        qsizetype languagePosition = match.hasMatch() ? match.capturedStart() : -1;
        const auto codePosition = contextualLanguagePosition(text, language);
        if (codePosition >= 0 && (languagePosition < 0 || codePosition < languagePosition))
            languagePosition = codePosition;
        if (languagePosition >= 0) {
            auto &position = preferred ? firstPreferred : firstOther;
            if (position < 0 || languagePosition < position)
                position = languagePosition;
        }
        if (preferred) {
            const QRegularExpression primary(
                "(?:" + pattern + ")[ ._-]+(?:dub(?:bed)?|audio|primary|default)\\b|"
                "\\b(?:primary|default)[ ._:-]+(?:" + pattern + ")",
                QRegularExpression::CaseInsensitiveOption);
            explicitPreferredAudio = explicitPreferredAudio || primary.match(text).hasMatch();
        }
    }
    static const QRegularExpression dub(R"(\b(?:Dub(?:bed)?|DVO|AVO)\b|дубляж)",
                                         QRegularExpression::CaseInsensitiveOption);
    const auto dubMatch = dub.match(text);
    // A Russian-led "Dub + 2x DVO ... Original (Eng)" is not English-primary,
    // even when English is the only language whose name the parser recognized.
    if (dubMatch.hasMatch() && firstPreferred >= 0 && dubMatch.capturedStart() < firstPreferred &&
        !explicitPreferredAudio)
        return true;
    if (explicitPreferredAudio)
        return false;
    if (languages.size() <= 1)
        return false;
    const bool foreignLead = nonLatinLead(p.stream.title) || nonLatinLead(p.filename);
    static const QSet<QString> nonLatinLanguages = {
        "Russian", "Ukrainian", "Japanese", "Chinese", "Korean", "Hindi", "Arabic",
        "Hebrew", "Greek", "Thai", "Persian", "Bengali", "Tamil", "Telugu"};
    const bool latinPreferred = std::any_of(languages.begin(), languages.end(), [&](const QString &language) {
        return matchesPreferred(language, o.preferred_languages) && !nonLatinLanguages.contains(language);
    });
    if (foreignLead && latinPreferred)
        return true;
    const bool rutracker = (p.stream.addon_name + ' ' + text).contains("rutracker", Qt::CaseInsensitive);
    if (rutracker && (firstPreferred < 0 || firstOther < 0))
        return true; // Multiple tracks, but no evidence which is the default dub.
    // Parser list order is not track order. Require evidence from the release
    // text before awarding the full primary-language bonus to a multi-track row.
    return firstPreferred < 0 || (firstOther >= 0 && firstOther < firstPreferred);
}
// Primary-only quality gates; manual ranking keeps every retained row.
QString releaseText(const ParsedStream &p) {
    auto text = p.filename + ' ' + p.stream.title + ' ' + p.stream.description + ' ' +
                p.stream.behavior_hints.value("filename").toString() + ' ' +
                p.stream.behavior_hints.value("fileName").toString();
    return text.replace('_', '.');
}
QString upscaleSignal(const ParsedStream &p, const ScoreOptions &o) {
    static const QRegularExpression marker(
        R"(\b(?:AI|upscal(?:e|ed|ing)|conv|conversion|Topaz|Gigapixel)\b)",
        QRegularExpression::CaseInsensitiveOption);
    const auto text = releaseText(p);
    if (marker.match(text).hasMatch())
        return "upscale-or-conversion";
    static const QRegularExpression restoration(
        R"(\b(?:remaster(?:ed)?|restor(?:ed|ation)|rescan|35mm|16mm)\b)",
        QRegularExpression::CaseInsensitiveOption);
    // A conservative suspicion, not proof: pre-HD TV WEBRip with explicit episode
    // context. Films, disc scans/restorations and modern TV are deliberately exempt.
    const int year = p.year.value_or(o.release_date.isValid() ? o.release_date.year() : 0);
    if (o.media_kind == "series" && p.resolution == Resolution::UHD &&
        p.source == Source::WEBRip && (p.episode || o.episode_requested) &&
        year > 0 && year < 2000 && !restoration.match(text).hasMatch())
        return "suspected-preHD-TV-upscale";
    return {};
}
int sourcePreference(Source source) {
    switch (source) {
    case Source::BluRay: case Source::BDRip: case Source::REMUX:
    case Source::WebDl: case Source::WEBRip: return 0;
    case Source::HDTV: case Source::DVDRip: case Source::SDTV: return 2;
    default: return 1; // Unknown/HDRip is not asserted to be a premium source.
    }
}
bool healthy(const ParsedStream &p, const ScoreOptions &o) {
    // Direct streams do not depend on a torrent swarm.
    return p.stream.info_hash.isEmpty() || !p.stream.url.isEmpty() ||
           p.seeders.value_or(0) >= o.healthy_seeders;
}
bool frenchDeferred(const ParsedStream &p, const ScoreOptions &o) {
    return p.french_led && !o.preferred_languages.isEmpty() &&
           !matchesPreferred("French", o.preferred_languages);
}
int priority(const std::optional<int> &p) {
    return p && *p >= 0 ? *p : std::numeric_limits<int>::max();
}
} // namespace

CorpusStats SourceScorer::computeCorpusStats(const QList<ParsedStream> &streams,
                                             const ScoreOptions &o) {
    CorpusStats c;
    c.days_since_release = daysSince(o);
    int captures = 0, web = 0;
    QList<quint64> sizes;
    QList<quint32> seeders;
    for (const auto &s : streams) {
        if (!s.stream.url.isEmpty() || s.seeders.value_or(0) >= 30) {
            ++c.trusted_tracked_count;
            if (theater(s.source))
                ++captures;
            if (webish(s.source))
                ++web;
        }
        if (s.size)
            sizes.push_back(*s.size);
        if (s.seeders)
            seeders.push_back(*s.seeders);
    }
    c.trusted_tracked_fraction =
        static_cast<double>(c.trusted_tracked_count) / std::max<qsizetype>(streams.size(), 1);
    c.theater_capture_fraction =
        static_cast<double>(captures) / std::max(c.trusted_tracked_count, 1);
    c.webish_fraction = static_cast<double>(web) / std::max(c.trusted_tracked_count, 1);
    c.median_size = percentile(sizes, .5);
    c.p90_size = percentile(sizes, .9);
    c.p10_seeders = percentile(seeders, .1);
    c.p90_seeders = percentile(seeders, .9);
    return c;
}
ScoredStream SourceScorer::score(const ParsedStream &p, const ScoreOptions &o,
                                 const CorpusStats &c) {
    ScoredStream s;
    s.parsed = p;
    s.tier = tierOf(p);
    auto add = [&](const QString &signal, double delta) {
        if (delta != 0) {
            s.score += delta;
            s.reasons.push_back({signal, delta});
        }
    };
    auto addReason = [&](const ScoreReason &r) { add(r.signal, r.delta); };
    add(resolutionLabel(p.resolution), resolutionPoints(p.resolution));
    if (p.hdr_format) {
        if (!o.dolby_vision_supported && *p.hdr_format == HdrFormat::Dv)
            s.reasons.push_back({"dolby-vision-unsupported:primary-deferred-within-resolution", 0});
        else if (!o.dolby_vision_supported && *p.hdr_format == HdrFormat::DvHdr10)
            add("HDR10-fallback", 5);
        else
            add(hdrLabel(*p.hdr_format),
                *p.hdr_format == HdrFormat::Dv || *p.hdr_format == HdrFormat::DvHdr10 ? 6 : 5);
    }
    if (p.codec == Codec::Hevc)
        add("HEVC", 1);
    if (p.codec == Codec::Av1)
        add("AV1", 1);
    switch (p.audio.codec) {
    case AudioCodec::Atmos:
        add("Atmos", 3);
        break;
    case AudioCodec::TrueHd:
        add("TrueHD", 2);
        break;
    case AudioCodec::DtsHdMa:
        add("DTS-HD MA", 2);
        break;
    case AudioCodec::DdPlus:
        add("DD+", 1);
        break;
    default:
        break;
    }
    if (p.audio.channels >= 6)
        add(QString("%1.0 channels").arg(p.audio.channels), 2);
    if (p.seeders) {
        const double cap = std::isfinite(o.seeder_bonus_cap) ? std::max(0.0, o.seeder_bonus_cap) : 24.0;
        const double weight = std::isfinite(o.seeder_log_weight) ? std::max(0.0, o.seeder_log_weight) : 3.0;
        const double boost = std::min(cap, weight * std::log2(1.0 + *p.seeders));
        if (boost > 0)
            add(QString("seeders=%1").arg(*p.seeders), boost);
        else if (p.stream.url.isEmpty() && !p.stream.info_hash.isEmpty() && *p.seeders == 0)
            add("zero-seeders-stale-meta", -20);
    }
    if (!p.stream.info_hash.isEmpty() && p.seeders == 0)
        add("zero-seeders-soft", -8);
    const auto upscale = upscaleSignal(p, o);
    if (!upscale.isEmpty())
        add(upscale, -15);
    if (o.release_date.isValid() && p.year) {
        const auto diff = std::abs(static_cast<qint64>(*p.year) - o.release_date.year());
        if (diff != 0) {
            const bool recent =
                std::abs(daysSince(o).value_or(std::numeric_limits<double>::infinity())) < 365;
            add(QString(diff == 1 ? "year-off-by-1:%1vs%2%3" : "year-mismatch:%1vs%2%3")
                    .arg(*p.year)
                    .arg(o.release_date.year())
                    .arg(recent ? "-recent" : ""),
                diff == 1 ? (recent ? -75 : -18) : (recent ? -150 : -70));
        }
    }
    if (trustedGroup(p.release_group_normalized))
        add("group:" + p.release_group_normalized, 2);
    if (!o.preferred_release_group.isEmpty() &&
        p.release_group_normalized == o.preferred_release_group)
        add("prev-episode-group:" + p.release_group_normalized, 8);
    if (p.remux)
        add("REMUX", 3);
    switch (p.source) {
    case Source::CAM:
        add("CAM penalty", -80);
        break;
    case Source::TS:
    case Source::HDTS:
        add("Telesync penalty", -60);
        break;
    case Source::TC:
        add("Telecine penalty", -50);
        break;
    case Source::SCR:
        add("Screener penalty", -40);
        break;
    default:
        break;
    }
    if (p.proper || p.repack_iteration > 0)
        add(p.proper ? QString("PROPER") : QString("REPACK%1").arg(p.repack_iteration),
            std::min(2, std::max(1, p.repack_iteration)));
    if (!o.preferred_languages.isEmpty()) {
        if (p.audio_languages.isEmpty())
            add("language-unknown", -3);
        else {
            bool matched = false, multi = false;
            for (const auto &lang : p.audio_languages) {
                multi = multi || lang.compare("Multi", Qt::CaseInsensitive) == 0;
                for (const auto &pref : o.preferred_languages)
                    if (!pref.trimmed().isEmpty() &&
                        lang.toLower().startsWith(pref.trimmed().toLower()))
                        matched = true;
            }
            if (matched) {
                if (preferredAudioSecondary(p, o))
                    add("preferred-language-secondary", 3);
                else
                    add("preferred-language", 12);
            }
            else if (multi)
                add("multi-language", 4);
            else
                add("language-mismatch", -14);
        }
    }
    if (p.scam_score > 0)
        add("scam-penalty", -static_cast<double>(p.scam_score));
    if (!o.prefer_addon_id.isEmpty() && o.prefer_addon_id == p.stream.addon_id)
        add("origin-addon", 250);
    static const QRegularExpression strong("mediafusion|comet",
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trusted("mediafusion|comet|torrentio",
                                            QRegularExpression::CaseInsensitiveOption);
    if (strong.match(p.stream.addon_name).hasMatch())
        add("strong-addon", 8);
    else if (trusted.match(p.stream.addon_name).hasMatch())
        add("trusted-addon", 4);
    if (p.stream.addon_priority && *p.stream.addon_priority >= 0)
        add(QString("addon-priority-%1").arg(*p.stream.addon_priority),
            std::max(0.0, 12.0 - static_cast<double>(*p.stream.addon_priority) * 4));
    addReason(bandwidthPenalty(p, o));
    const auto minimum = expectedMinimum(p, o);
    const bool validSize = p.size && minimum && static_cast<double>(*p.size) >= *minimum;
    add("size-mismatch", sizeMislabel(p, minimum));
    add("title-says-hires-filename-says-cam", camInFilename(p));
    addReason(undersized(p, o));
    addReason(tinyMovie(p, o));
    addReason(freshTheatrical(p, o, validSize, c));
    if (s.reasons.isEmpty())
        s.reasons.push_back({"no-positive-quality-signals", 0});
    return s;
}
RankedPicker SourceScorer::rankAndPick(QList<ScoredStream> streams, const ScoreOptions &o) {
    // All rows remain score-ranked. Installed order breaks ties, never hides a
    // better score; respect_addon_order cannot override this library contract.
    std::stable_sort(
        streams.begin(), streams.end(), [](const ScoredStream &a, const ScoredStream &b) {
            const bool af = std::isfinite(a.score), bf = std::isfinite(b.score);
            if (af != bf)
                return af;
            if (af && a.score != b.score)
                return a.score > b.score;
            if (a.parsed.dual_audio != b.parsed.dual_audio)
                return a.parsed.dual_audio;
            const auto ap = priority(a.parsed.stream.addon_priority),
                       bp = priority(b.parsed.stream.addon_priority);
            if (ap != bp)
                return ap < bp;
            const auto ar = priority(a.parsed.stream.addon_return_idx),
                       br = priority(b.parsed.stream.addon_return_idx);
            if (ar != br)
                return ar < br;
            const auto ai = a.parsed.stream.input_index < 0 ? std::numeric_limits<int>::max()
                                                            : a.parsed.stream.input_index;
            const auto bi = b.parsed.stream.input_index < 0 ? std::numeric_limits<int>::max()
                                                            : b.parsed.stream.input_index;
            return ai < bi;
        });
    RankedPicker out;
    out.all = std::move(streams);
    if (out.all.isEmpty()) {
        out.selection_reasons.push_back("no-candidates: ranked candidate list is empty");
        return out;
    }
    for (auto &s : out.all) {
        // Diagnostic-only annotations: neither scores nor manual-list order change.
        const auto band = streamingBand(s.parsed, o);
        s.reasons.push_back({band.reason, 0});
        const int sourceTier = sourcePreference(s.parsed.source);
        s.reasons.push_back({sourceTier == 0 ? "source-quality:premium-disc-or-web" :
                             sourceTier == 2 ? "source-quality:capture-or-DVD" :
                                               "source-quality:unknown-or-other", 0});
        if (frenchDeferred(s.parsed, o))
            s.reasons.push_back({"french-led:deferred-when-non-french-alternative-in-band", 0});
        s.reasons.push_back({QString("torrent-health:floor=%1,status=%2")
            .arg(o.healthy_seeders).arg(healthy(s.parsed, o) ? "healthy-or-direct" : "below-floor-or-unknown"), 0});
        if ((s.parsed.batch_release || s.parsed.season_pack) && selectedEpisodeFile(s.parsed, o))
            s.reasons.push_back({"selected-episode-file:batch-torrent-treated-as-single", 0});
        const auto key = tierKey(s.tier);
        if (!out.by_tier.contains(key))
            out.by_tier.insert(key, s);
    }
    const bool hasSingle = o.episode_requested &&
                           std::any_of(out.all.begin(), out.all.end(), [&](const ScoredStream &s) {
                               return primaryEligible(s) && !batch(s.parsed, o);
                           });
    const QList<Resolution> resolutionOrder = o.target_resolution == Resolution::UHD
        ? QList<Resolution>{Resolution::UHD, Resolution::P1080, Resolution::P720, Resolution::P480, Resolution::SD}
        : QList<Resolution>{Resolution::P1080, Resolution::P720, Resolution::UHD, Resolution::P480, Resolution::SD};
    for (const auto target : resolutionOrder) {
        // Unsupported DV-only is a last resort within a resolution, even if its
        // file is smaller than an otherwise-eligible HDR10/SDR row.
        for (int dvPreference = 0; dvPreference < 2; ++dvPreference) {
            for (int bandPreference = 0; bandPreference < 3; ++bandPreference) {
                QList<const ScoredStream *> pool;
                for (const auto &s : out.all) {
                    if (s.parsed.resolution == target && primaryEligible(s) &&
                        !(hasSingle && batch(s.parsed, o)) &&
                        int(unsupportedDvOnly(s.parsed, o)) == dvPreference &&
                        streamingBand(s.parsed, o).preference == bandPreference)
                        pool.push_back(&s);
                }
                // Each gate acts only on the prior gate's survivors. Thus a large
                // premium encode cannot displace an in-band HDTV fallback.
                auto prefer = [&](const auto &predicate) {
                    if (std::any_of(pool.begin(), pool.end(), predicate))
                        pool.erase(std::remove_if(pool.begin(), pool.end(),
                            [&](const ScoredStream *s) { return !predicate(s); }), pool.end());
                };
                prefer([&](const ScoredStream *s) { return !frenchDeferred(s->parsed, o); });
                prefer([&](const ScoredStream *s) { return upscaleSignal(s->parsed, o).isEmpty(); });
                int bestSource = 3;
                for (const auto *s : pool)
                    bestSource = std::min(bestSource, sourcePreference(s->parsed.source));
                prefer([&](const ScoredStream *s) { return sourcePreference(s->parsed.source) == bestSource; });
                prefer([&](const ScoredStream *s) { return healthy(s->parsed, o); });
                if (!pool.isEmpty()) {
                    const auto &s = *pool.front(); // Pool preserves descending numeric score.
                    out.primary = s;
                    out.selection_reasons.push_back(
                        QString("primary-resolution-policy:%1 (%2)").arg(resolutionLabel(target),
                            o.target_resolution == Resolution::UHD ? "4K > 1080p > 720p > SD"
                                                                   : "1080p > 720p > 4K > SD"));
                    out.selection_reasons.push_back(
                        QString("quality-policy:french-led=%1,upscale=%2,healthy=%3,source-tier=%4")
                            .arg(frenchDeferred(s.parsed, o)).arg(!upscaleSignal(s.parsed, o).isEmpty())
                            .arg(healthy(s.parsed, o)).arg(bestSource));
                    if (o.episode_requested)
                        out.selection_reasons.push_back(
                            hasSingle ? QString("single-episode-preferred-over-pack")
                                      : QString("pack-fallback:no-eligible-single-episode"));
                    if (s.parsed.dual_audio)
                        out.selection_reasons.push_back("dual-audio-wins-score-ties");
                    out.selection_reasons.push_back(
                        bandPreference == 0 ? "streaming-size:preferred-known-friendly" :
                        bandPreference == 1 ? "streaming-size:fallback-unknown-no-friendly-candidate" :
                                              "streaming-size:fallback-oversized-no-friendly-candidate");
                    if (dvPreference)
                        out.selection_reasons.push_back("dolby-vision-unsupported:fallback-no-compatible-row-in-resolution");
                    for (const auto &explanation : out.selection_reasons)
                        out.primary->reasons.push_back({explanation, 0});
                    return out;
                }
            }
        }
    }
    out.selection_reasons.push_back(
        "no-eligible-primary: retained rows are unplayable, rough, known-zero-seed torrents, "
        "fraud-flagged, nonfinite-scored, or below the nonnegative-score floor");
    return out;
}
RankedPicker SourceScorer::evaluate(const QList<Stream> &streams, const TrustOptions &trust,
                                    const ScoreOptions &options) {
    QList<ParsedStream> parsed;
    parsed.reserve(streams.size());
    for (const auto &s : streams)
        parsed.push_back(StreamParser::parse(s));
    const auto trusted = StreamTrust::apply(parsed, trust);
    ScoreOptions o = options;
    if (o.media_kind.isEmpty())
        o.media_kind = trust.kind;
    if (!o.release_date.isValid())
        o.release_date = trust.release_date;
    o.episode_requested =
        o.episode_requested || (trust.kind == "series" && trust.expected_episode.has_value());
    if (!o.expected_episode)
        o.expected_episode = trust.expected_episode;
    if (!o.expected_season)
        o.expected_season = trust.expected_season;
    const auto corpus = computeCorpusStats(trusted.keep, o);
    QList<ScoredStream> scored;
    scored.reserve(trusted.keep.size());
    for (const auto &p : trusted.keep)
        scored.push_back(score(p, o, corpus));
    auto out = rankAndPick(std::move(scored), o);
    out.rejected = trusted.rejected;
    return out;
}
} // namespace colosseum::stream
