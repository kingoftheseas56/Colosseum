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
bool batch(const ParsedStream &p) { return p.batch_release || p.season_pack; }
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
    if (p.hdr_format)
        add(hdrLabel(*p.hdr_format),
            *p.hdr_format == HdrFormat::Dv || *p.hdr_format == HdrFormat::DvHdr10 ? 6 : 5);
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
        const auto boost = std::min(*p.seeders / 10, quint32(10));
        if (boost > 0)
            add(QString("seeders=%1").arg(*p.seeders), boost);
        else if (p.stream.url.isEmpty() && !p.stream.info_hash.isEmpty() && *p.seeders == 0)
            add("zero-seeders-stale-meta", -20);
    }
    if (!p.stream.info_hash.isEmpty() && p.seeders == 0)
        add("zero-seeders-soft", -8);
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
            if (matched)
                add("preferred-language", 12);
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
    for (const auto &s : out.all) {
        const auto key = tierKey(s.tier);
        if (!out.by_tier.contains(key))
            out.by_tier.insert(key, s);
    }
    const bool hasSingle = o.episode_requested &&
                           std::any_of(out.all.begin(), out.all.end(), [](const ScoredStream &s) {
                               return primaryEligible(s) && !batch(s.parsed);
                           });
    for (const auto target :
         {Resolution::P1080, Resolution::P720, Resolution::UHD, Resolution::P480, Resolution::SD}) {
        for (const auto &s : out.all) {
            if (s.parsed.resolution != target || !primaryEligible(s) ||
                (hasSingle && batch(s.parsed)))
                continue;
            out.primary = s;
            out.selection_reasons.push_back(
                QString("primary-resolution-policy:%1 (1080p > 720p > 4K > SD)")
                    .arg(resolutionLabel(target)));
            if (o.episode_requested)
                out.selection_reasons.push_back(
                    hasSingle ? QString("single-episode-preferred-over-pack")
                              : QString("pack-fallback:no-eligible-single-episode"));
            if (s.parsed.dual_audio)
                out.selection_reasons.push_back("dual-audio-wins-score-ties");
            out.primary->reasons.push_back(
                {QString("primary-resolution-policy:%1 (1080p > 720p > 4K > SD)")
                     .arg(resolutionLabel(target)),
                 0});
            if (o.episode_requested)
                out.primary->reasons.push_back(
                    {hasSingle ? QString("single-episode-preferred-over-pack")
                               : QString("pack-fallback:no-eligible-single-episode"),
                     0});
            if (s.parsed.dual_audio)
                out.primary->reasons.push_back({"dual-audio-wins-score-ties", 0});
            return out;
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
